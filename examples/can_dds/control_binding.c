/*
 * (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software. Licensee has the right to distribute object form only
 * for use with RTI products. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose. RTI is under no
 * obligation to maintain or support the Software. RTI shall not be liable for any
 * incidental or consequential damages arising out of the use or inability to use
 * the software.
 */

#include "control_binding.h"
#include "controllerSupport.h"
#include "control_resources.h"
#include <string.h>

_Static_assert(sizeof(((DDS_InstanceHandle_t *)0)->octet) == 16,
               "control IDL publication handle is the Micro 16-byte handle");
_Static_assert((int)CONNECTION_UP == (int)PGW_CONTROL_CONNECTION_UP &&
               (int)CONNECTION_DOWN == (int)PGW_CONTROL_CONNECTION_DOWN &&
               (int)INPUT_ENABLE == (int)PGW_CONTROL_INPUT_ENABLE &&
               (int)INPUT_DISABLE == (int)PGW_CONTROL_INPUT_DISABLE &&
               (int)OUTPUT_ENABLE == (int)PGW_CONTROL_OUTPUT_ENABLE &&
               (int)OUTPUT_DISABLE == (int)PGW_CONTROL_OUTPUT_DISABLE &&
               (int)ROUTE_PAUSE == (int)PGW_CONTROL_ROUTE_PAUSE &&
               (int)ROUTE_RESUME == (int)PGW_CONTROL_ROUTE_RESUME,
               "IDL actions and core action values must remain aligned");
_Static_assert((int)BOOLEAN_VALUE == (int)PGW_CONTROL_SCALAR_BOOLEAN &&
               (int)INT32_VALUE == (int)PGW_CONTROL_SCALAR_INT32 &&
               (int)UINT32_VALUE == (int)PGW_CONTROL_SCALAR_UINT32 &&
               (int)INT64_VALUE == (int)PGW_CONTROL_SCALAR_INT64 &&
               (int)UINT64_VALUE == (int)PGW_CONTROL_SCALAR_UINT64 &&
               (int)DOUBLE_VALUE == (int)PGW_CONTROL_SCALAR_DOUBLE,
               "IDL telemetry scalar and core values must remain aligned");

static PGW_Status read_command(DDS_DataReader *reader, PGW_ControlCommand *out,
                               PGW_ControlCorrelation *correlation)
{
    PGW_ControlService_Command sample = {0};
    struct DDS_SampleInfo info = DDS_SampleInfo_INITIALIZER;
    DDS_ReturnCode_t rc = DDS_DataReader_take_next_sample(reader, &sample, &info);
    if (rc == DDS_RETCODE_NO_DATA) return PGW_NO_DATA;
    if (rc != DDS_RETCODE_OK) return PGW_IO_ERROR;
    if (!info.valid_data) return PGW_NO_DATA;
    out->resource_id = (uint32_t)sample.resource;
    out->action = (PGW_ControlAction)sample.action;
    memcpy(correlation->publication_handle, info.publication_handle.octet,
           sizeof(correlation->publication_handle));
    correlation->publication_sequence_high =
        (int32_t)info.publication_sequence_number.high;
    correlation->publication_sequence_low =
        (uint32_t)info.publication_sequence_number.low;
    return PGW_OK;
}

static PGW_Status register_state(DDS_DataWriter *writer, uint32_t resource_id,
                                 DDS_InstanceHandle_t *out)
{
    PGW_ControlService_State sample = {0};
    if (!out || resource_id >= PGW_CONTROL_RESOURCE_COUNT) return PGW_INVALID;
    sample.resource = (PGW_ControlService_Resource)resource_id;
    *out = DDS_DataWriter_register_instance(writer, &sample);
    return DDS_InstanceHandle_equals(out, &DDS_HANDLE_NIL) ? PGW_FATAL : PGW_OK;
}

#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
static PGW_Status register_telemetry(DDS_DataWriter *writer,
    const PGW_ControlTelemetryMetric *metric, DDS_InstanceHandle_t *out)
{
    PGW_ControlService_Telemetry sample = {0};
    if (!metric || !out || metric->resource_id >= PGW_CONTROL_RESOURCE_COUNT ||
        metric->telemetry_kind >= PGW_CONTROL_TELEMETRY_KIND_COUNT)
        return PGW_INVALID;
    sample.resource = (PGW_ControlService_Resource)metric->resource_id;
    sample.kind = (PGW_ControlService_TelemetryKind)metric->telemetry_kind;
    sample.scalar_type = (PGW_Control_TelemetryScalarType)metric->scalar_type;
    size_t unit_length = strlen(metric->unit);
    if (unit_length > sizeof(sample.unit) - 1) return PGW_INVALID;
    memcpy(sample.unit, metric->unit, unit_length + 1);
    *out = DDS_DataWriter_register_instance(writer, &sample);
    return DDS_InstanceHandle_equals(out, &DDS_HANDLE_NIL) ? PGW_FATAL : PGW_OK;
}
#endif

static PGW_Status write_state(DDS_DataWriter *writer, const PGW_ControlState *state,
                              const DDS_InstanceHandle_t *handle)
{
    if (!state || !handle || state->resource_id >= PGW_CONTROL_RESOURCE_COUNT)
        return PGW_INVALID;
    PGW_ControlService_State sample = {
        (PGW_ControlService_Resource)state->resource_id,
        (PGW_Control_ResourceStatus)state->status,
        state->command_capabilities,
        state->telemetry_capabilities
    };
    DDS_ReturnCode_t rc = DDS_DataWriter_write(writer, &sample, handle);
    if (rc == DDS_RETCODE_OK) return PGW_OK;
    if (rc == DDS_RETCODE_OUT_OF_RESOURCES || rc == DDS_RETCODE_TIMEOUT)
        return PGW_BACKPRESSURE;
    if (rc == DDS_RETCODE_BAD_PARAMETER ||
        rc == DDS_RETCODE_PRECONDITION_NOT_MET) return PGW_INVALID;
    return PGW_IO_ERROR;
}

#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
static PGW_Status write_telemetry(DDS_DataWriter *writer,
    const PGW_ControlTelemetry *telemetry, const DDS_InstanceHandle_t *handle)
{
    if (!telemetry || !handle ||
        telemetry->resource_id >= PGW_CONTROL_RESOURCE_COUNT ||
        telemetry->telemetry_kind >= PGW_CONTROL_TELEMETRY_KIND_COUNT ||
        !memchr(telemetry->unit, '\0', sizeof(telemetry->unit)))
        return PGW_INVALID;
    PGW_ControlService_Telemetry sample = {0};
    sample.resource = (PGW_ControlService_Resource)telemetry->resource_id;
    sample.kind = (PGW_ControlService_TelemetryKind)telemetry->telemetry_kind;
    sample.scalar_type = (PGW_Control_TelemetryScalarType)telemetry->scalar.type;
    memcpy(sample.unit, telemetry->unit, sizeof(sample.unit));
    switch (telemetry->scalar.type) {
        case PGW_CONTROL_SCALAR_BOOLEAN:
            sample.value._u.boolean_value = telemetry->scalar.value.boolean_value;
            break;
        case PGW_CONTROL_SCALAR_INT32:
            sample.value._u.int32_value = telemetry->scalar.value.int32_value;
            break;
        case PGW_CONTROL_SCALAR_UINT32:
            sample.value._u.uint32_value = telemetry->scalar.value.uint32_value;
            break;
        case PGW_CONTROL_SCALAR_INT64:
            sample.value._u.int64_value = telemetry->scalar.value.int64_value;
            break;
        case PGW_CONTROL_SCALAR_UINT64:
            sample.value._u.uint64_value = telemetry->scalar.value.uint64_value;
            break;
        case PGW_CONTROL_SCALAR_DOUBLE:
            sample.value._u.double_value = telemetry->scalar.value.double_value;
            break;
    }
    sample.value._d = sample.scalar_type;
    DDS_ReturnCode_t rc = DDS_DataWriter_write(writer, &sample, handle);
    if (rc == DDS_RETCODE_OK) return PGW_OK;
    if (rc == DDS_RETCODE_OUT_OF_RESOURCES || rc == DDS_RETCODE_TIMEOUT)
        return PGW_BACKPRESSURE;
    if (rc == DDS_RETCODE_BAD_PARAMETER ||
        rc == DDS_RETCODE_PRECONDITION_NOT_MET) return PGW_INVALID;
    return PGW_IO_ERROR;
}
#endif

static PGW_Status write_result(DDS_DataWriter *writer, const PGW_ControlResult *result)
{
    if (!result) return PGW_INVALID;
    PGW_ControlService_Result sample = {0};
    sample.outcome = (PGW_Control_CommandOutcome)result->outcome;
    memcpy(sample.publication_handle, result->correlation.publication_handle,
           sizeof(sample.publication_handle));
    sample.publication_sequence_high = result->correlation.publication_sequence_high;
    sample.publication_sequence_low = result->correlation.publication_sequence_low;
    DDS_ReturnCode_t rc = DDS_DataWriter_write(writer, &sample, &DDS_HANDLE_NIL);
    if (rc == DDS_RETCODE_OK) return PGW_OK;
    if (rc == DDS_RETCODE_OUT_OF_RESOURCES || rc == DDS_RETCODE_TIMEOUT)
        return PGW_BACKPRESSURE;
    if (rc == DDS_RETCODE_BAD_PARAMETER ||
        rc == DDS_RETCODE_PRECONDITION_NOT_MET) return PGW_INVALID;
    return PGW_IO_ERROR;
}

static DDS_ReturnCode_t write_command(DDS_DataWriter *writer,
                                      const PGW_ControlCommand *command)
{
    if (!command || command->resource_id >= PGW_CONTROL_RESOURCE_COUNT ||
        (unsigned)command->action > PGW_CONTROL_ROUTE_RESUME)
        return DDS_RETCODE_BAD_PARAMETER;
    PGW_ControlService_Command sample = {
        (PGW_ControlService_Resource)command->resource_id,
        (PGW_Control_Action)command->action
    };
    return DDS_DataWriter_write(writer, &sample, &DDS_HANDLE_NIL);
}

static PGW_Status read_state(DDS_DataReader *reader, PGW_ControlState *out)
{
    PGW_ControlService_State sample = {0};
    struct DDS_SampleInfo info = DDS_SampleInfo_INITIALIZER;
    DDS_ReturnCode_t rc = DDS_DataReader_take_next_sample(reader, &sample, &info);
    if (rc == DDS_RETCODE_NO_DATA) return PGW_NO_DATA;
    if (rc != DDS_RETCODE_OK) return PGW_IO_ERROR;
    if (!info.valid_data) return PGW_NO_DATA;
    out->resource_id = (uint32_t)sample.resource;
    out->status = (PGW_ControlResourceStatus)sample.status;
    out->command_capabilities = sample.command_capabilities;
    out->telemetry_capabilities = sample.telemetry_capabilities;
    return PGW_OK;
}

static PGW_Status read_result(DDS_DataReader *reader, PGW_ControlResult *out)
{
    PGW_ControlService_Result sample = {0};
    struct DDS_SampleInfo info = DDS_SampleInfo_INITIALIZER;
    DDS_ReturnCode_t rc = DDS_DataReader_take_next_sample(reader, &sample, &info);
    if (rc == DDS_RETCODE_NO_DATA) return PGW_NO_DATA;
    if (rc != DDS_RETCODE_OK) return PGW_IO_ERROR;
    if (!info.valid_data) return PGW_NO_DATA;
    out->outcome = (PGW_ControlOutcome)sample.outcome;
    memcpy(out->correlation.publication_handle, sample.publication_handle,
           sizeof(out->correlation.publication_handle));
    out->correlation.publication_sequence_high = sample.publication_sequence_high;
    out->correlation.publication_sequence_low = sample.publication_sequence_low;
    return PGW_OK;
}

#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
static PGW_Status read_telemetry(DDS_DataReader *reader, PGW_ControlTelemetry *out)
{
    PGW_ControlService_Telemetry sample = {0};
    struct DDS_SampleInfo info = DDS_SampleInfo_INITIALIZER;
    DDS_ReturnCode_t rc = DDS_DataReader_take_next_sample(reader, &sample, &info);
    if (rc == DDS_RETCODE_NO_DATA) return PGW_NO_DATA;
    if (rc != DDS_RETCODE_OK) return PGW_IO_ERROR;
    if (!info.valid_data) return PGW_NO_DATA;
    out->resource_id = (uint32_t)sample.resource;
    out->telemetry_kind = (uint32_t)sample.kind;
    if (!memchr(sample.unit, '\0', sizeof(sample.unit))) return PGW_INVALID;
    memcpy(out->unit, sample.unit, sizeof(out->unit));
    out->scalar.type = (PGW_ControlScalarType)sample.scalar_type;
    switch (out->scalar.type) {
        case PGW_CONTROL_SCALAR_BOOLEAN:
            out->scalar.value.boolean_value = sample.value._u.boolean_value;
            break;
        case PGW_CONTROL_SCALAR_INT32:
            out->scalar.value.int32_value = sample.value._u.int32_value;
            break;
        case PGW_CONTROL_SCALAR_UINT32:
            out->scalar.value.uint32_value = sample.value._u.uint32_value;
            break;
        case PGW_CONTROL_SCALAR_INT64:
            out->scalar.value.int64_value = sample.value._u.int64_value;
            break;
        case PGW_CONTROL_SCALAR_UINT64:
            out->scalar.value.uint64_value = sample.value._u.uint64_value;
            break;
        case PGW_CONTROL_SCALAR_DOUBLE:
            out->scalar.value.double_value = sample.value._u.double_value;
            break;
        default:
            return PGW_INVALID;
    }
    return PGW_OK;
}
#endif

const PGW_DDSControlTypeI PGW_example_control_types = {
    .version = PGW_CONTROL_ABI_VERSION,
    .size = sizeof(PGW_DDSControlTypeI),
    .take_command = read_command,
    .register_state = register_state,
    .write_state = write_state,
    .write_result = write_result,
    .write_command = write_command,
    .take_state = read_state,
    .take_result = read_result,
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
    .register_telemetry = register_telemetry,
    .write_telemetry = write_telemetry,
    .take_telemetry = read_telemetry
#else
    .register_telemetry = NULL,
    .write_telemetry = NULL,
    .take_telemetry = NULL
#endif
};

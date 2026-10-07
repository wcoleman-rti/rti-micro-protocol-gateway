/*
 * (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software. Licensee has the right to distribute object form only
 * for use with RTI products. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose. RTI is under no
 * obligation to maintain or support the software. RTI shall not be liable for any
 * incidental or consequential damages arising out of the use or inability to use
 * the software.
 */

#include <pgw/dds/connext_micro.h>
#include <pgw/can.h>
#include <pgw/signal.h>
#include <pgw/signal_dds.h>
#include <pgw_codec.h>
#include "signalsSupport.h"
#include <math.h>
#include <string.h>

bool PGW_signal_validate_dds(const void *opaque)
{
    if (!opaque) return false;
    const PGW_DDS_Signal *wire = opaque;
    const PGW_SignalDescriptor *descriptor = PGW_codec_signal_find(wire->id);
    if (!descriptor) return false;
    switch (wire->value._d) {
    case VALUE_BOOLEAN:
        return descriptor->kind == PGW_VALUE_BOOLEAN &&
            (wire->value._u.boolean_value == DDS_BOOLEAN_FALSE ||
             wire->value._u.boolean_value == DDS_BOOLEAN_TRUE);
    case VALUE_INT64:
        return descriptor->kind == PGW_VALUE_INT64;
    case VALUE_DOUBLE:
        return descriptor->kind == PGW_VALUE_DOUBLE &&
            isfinite(wire->value._u.real_value);
    default:
        return false;
    }
}

PGW_Status PGW_signal_from_dds(const void *opaque, void *out, size_t size)
{
    if (!opaque || !out || size != sizeof(PGW_Signal)) return PGW_INVALID;
    const PGW_DDS_Signal *wire = opaque;
    const PGW_SignalDescriptor *descriptor = PGW_codec_signal_find(wire->id);
    if (!descriptor) return PGW_INVALID;
    PGW_Signal value = {.id = wire->id};
    switch (wire->value._d) {
    case VALUE_BOOLEAN:
        if (wire->value._u.boolean_value != DDS_BOOLEAN_FALSE &&
            wire->value._u.boolean_value != DDS_BOOLEAN_TRUE) return PGW_INVALID;
        value.value.kind = PGW_VALUE_BOOLEAN;
        value.value.data.boolean = wire->value._u.boolean_value != DDS_BOOLEAN_FALSE;
        break;
    case VALUE_INT64:
        value.value.kind = PGW_VALUE_INT64;
        value.value.data.integer = wire->value._u.integer_value;
        break;
    case VALUE_DOUBLE:
        value.value.kind = PGW_VALUE_DOUBLE;
        value.value.data.real = wire->value._u.real_value;
        if (!isfinite(value.value.data.real)) return PGW_INVALID;
        break;
    default:
        return PGW_INVALID;
    }
    if (value.value.kind != descriptor->kind) return PGW_INVALID;
    *(PGW_Signal *)out = value;
    return PGW_OK;
}

PGW_Status PGW_signal_bind_view(void *state, const PGW_SampleRepresentation *source)
{
    (void)state;
    if (!source || !source->access ||
        source->access->version != PGW_ABI_VERSION ||
        source->access->size != sizeof(PGW_SampleAccessI) ||
        !source->access->view || !source->view_contract)
        return PGW_UNSUPPORTED;
    const PGW_SampleViewDescriptor *view = source->view_contract;
    if (view->kind == PGW_SAMPLE_VIEW_CANONICAL &&
        view->value_size == sizeof(PGW_Signal) &&
        view->type_identity == &PGW_CAN_SIGNAL_VALUE_IDENTITY &&
        view->context_identity == &PGW_CAN_METADATA_IDENTITY)
        return PGW_OK;
    if (view->kind == PGW_SAMPLE_VIEW_NATIVE && !view->value_size &&
        view->type_identity == PGW_signal_dds_type_binding_powertrain.type_identity() &&
        view->context_identity == &PGW_DDS_METADATA_IDENTITY)
        return PGW_OK;
    return PGW_UNSUPPORTED;
}

PGW_Status PGW_signal_from_view(const PGW_SampleView *view, void *out, size_t size)
{
    if (!view || !view->value || !out || size != sizeof(PGW_Signal))
        return PGW_INVALID;
    if ((view->context == NULL) != (view->context_identity == NULL))
        return PGW_INVALID;
    if (view->kind == PGW_SAMPLE_VIEW_CANONICAL) {
        if (view->value_size != sizeof(PGW_Signal) ||
            view->type_identity != &PGW_CAN_SIGNAL_VALUE_IDENTITY ||
            view->context_identity != &PGW_CAN_METADATA_IDENTITY)
            return PGW_UNSUPPORTED;
        memcpy(out, view->value, sizeof(PGW_Signal));
        return PGW_OK;
    }
    if (view->kind == PGW_SAMPLE_VIEW_NATIVE) {
        if (view->type_identity !=
                PGW_signal_dds_type_binding_powertrain.type_identity() ||
            view->context_identity != &PGW_DDS_METADATA_IDENTITY)
            return PGW_UNSUPPORTED;
        const PGW_DDSMetadata *metadata = view->context;
        if (!metadata || !metadata->valid_data) return PGW_INVALID;
        return PGW_signal_from_dds(view->value, out, size);
    }
    return PGW_UNSUPPORTED;
}

PGW_Status PGW_signal_write_view(void *state, DDS_DataWriter *writer,
                                const PGW_SampleView *view,
                                const struct DDS_Time_t *time,
                                DDS_ReturnCode_t *write_result)
{
    (void)state;
    if (!writer || !view || !write_result) return PGW_INVALID;
    if ((view->context == NULL) != (view->context_identity == NULL))
        return PGW_INVALID;
    PGW_Signal converted;
    const PGW_Signal *native;
    if (view->kind == PGW_SAMPLE_VIEW_CANONICAL && view->value &&
        view->value_size == sizeof(PGW_Signal) &&
        view->type_identity == &PGW_CAN_SIGNAL_VALUE_IDENTITY &&
        view->context_identity == &PGW_CAN_METADATA_IDENTITY) {
        native = view->value;
    } else {
        PGW_Status status = PGW_signal_from_view(view, &converted, sizeof(converted));
        if (status != PGW_OK) return status;
        native = &converted;
    }
    PGW_DDS_Signal wire;
    if (!PGW_DDS_Signal_initialize(&wire)) return PGW_FATAL;
    *write_result = PGW_signal_to_dds(native, &wire);
    if (*write_result != DDS_RETCODE_OK) return PGW_INVALID;
    if (time) {
        *write_result = PGW_DDS_SignalDataWriter_write_w_timestamp(
            PGW_DDS_SignalDataWriter_narrow(writer), &wire, &DDS_HANDLE_NIL, time);
    } else {
        *write_result = PGW_DDS_SignalDataWriter_write(
            PGW_DDS_SignalDataWriter_narrow(writer), &wire, &DDS_HANDLE_NIL);
    }
    return PGW_OK;
}

DDS_ReturnCode_t PGW_signal_to_dds(const void *opaque, void *out)
{
    if (!opaque || !out) return DDS_RETCODE_BAD_PARAMETER;
    const PGW_Signal *value = opaque;
    PGW_DDS_Signal *wire = out;
    const PGW_SignalDescriptor *descriptor = PGW_codec_signal_find(value->id);
    if (!descriptor || value->value.kind != descriptor->kind)
        return DDS_RETCODE_BAD_PARAMETER;
    wire->id = value->id;
    switch (value->value.kind) {
    case PGW_VALUE_BOOLEAN:
        wire->value._d = VALUE_BOOLEAN;
        wire->value._u.boolean_value = value->value.data.boolean
            ? DDS_BOOLEAN_TRUE : DDS_BOOLEAN_FALSE;
        break;
    case PGW_VALUE_INT64:
        wire->value._d = VALUE_INT64;
        wire->value._u.integer_value = value->value.data.integer;
        break;
    case PGW_VALUE_DOUBLE:
        if (!isfinite(value->value.data.real)) return DDS_RETCODE_BAD_PARAMETER;
        wire->value._d = VALUE_DOUBLE;
        wire->value._u.real_value = value->value.data.real;
        break;
    default:
        return DDS_RETCODE_BAD_PARAMETER;
    }
    return DDS_RETCODE_OK;
}

static PGW_Status register_category(void *opaque, DDS_DataWriter *writer,
                                   const char *category)
{
    PGW_DDS_Signal *scratch = opaque;
    if (!scratch || !writer) return PGW_INVALID;
    struct DDS_Time_t epoch = {0, 0};
    for (size_t i = 0; i < PGW_codec_signal_count; ++i) {
        const PGW_SignalDescriptor *descriptor = &PGW_codec_signals[i];
        if (category && strcmp(category, descriptor->category)) continue;
        scratch->id = descriptor->id;
        scratch->value._d = (PGW_DDS_ValueKind)descriptor->kind;
        DDS_InstanceHandle_t handle =
            PGW_DDS_SignalDataWriter_register_instance_w_timestamp(
                PGW_DDS_SignalDataWriter_narrow(writer), scratch, &epoch);
        if (DDS_InstanceHandle_equals(&handle, &DDS_HANDLE_NIL)) return PGW_FATAL;
    }
    return PGW_OK;
}

PGW_Status PGW_signal_register_keys_powertrain(void *scratch, DDS_DataWriter *writer)
{
    return register_category(scratch, writer, "powertrain");
}

PGW_Status PGW_signal_register_keys_auxiliary(void *scratch, DDS_DataWriter *writer)
{
    return register_category(scratch, writer, "auxiliary");
}

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
#include <pgw/signal.h>
#include <pgw_codec.h>
#include "signalsSupport.h"
#include <math.h>
#include <string.h>

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

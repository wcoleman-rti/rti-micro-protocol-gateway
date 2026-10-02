#include <pgw/signal_dds.h>
#include "signalsSupport.h"
#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct {
    struct PGW_DDS_SignalSeq data;
    struct DDS_SampleInfoSeq info;
    PGW_DDS_Signal scratch;
} PGW_SignalDDSState;

static PGW_Status initialize(PGW_Arena *arena, size_t capacity, void **out)
{
    PGW_SignalDDSState *state;
    if (!out || !capacity || capacity > INT_MAX) return PGW_INVALID;
    PGW_Status status = PGW_Arena_allocate(arena, sizeof(*state), _Alignof(PGW_SignalDDSState),
                                          (void **)&state);
    if (status != PGW_OK) return status;
    memset(state, 0, sizeof(*state));
    if (!PGW_DDS_SignalSeq_initialize(&state->data) ||
        !DDS_SampleInfoSeq_initialize(&state->info) ||
        !PGW_DDS_Signal_initialize(&state->scratch)) return PGW_FATAL;
    *out = state;
    return PGW_OK;
}

static DDS_ReturnCode_t take_samples(void *opaque, DDS_DataReader *reader, size_t count)
{
    PGW_SignalDDSState *state = opaque;
    if (!state || count > INT_MAX) return DDS_RETCODE_BAD_PARAMETER;
    return PGW_DDS_SignalDataReader_take(PGW_DDS_SignalDataReader_narrow(reader),
        &state->data, &state->info, (DDS_Long)count, DDS_ANY_SAMPLE_STATE,
        DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
}

static size_t length(void *opaque)
{
    return (size_t)PGW_DDS_SignalSeq_get_length(&((PGW_SignalDDSState *)opaque)->data);
}

static const void *data(void *opaque, size_t index)
{
    if (index > INT_MAX) return NULL;
    return PGW_DDS_SignalSeq_get_reference(&((PGW_SignalDDSState *)opaque)->data, (DDS_Long)index);
}

static const struct DDS_SampleInfo *info(void *opaque, size_t index)
{
    if (index > INT_MAX) return NULL;
    return DDS_SampleInfoSeq_get_reference(&((PGW_SignalDDSState *)opaque)->info, (DDS_Long)index);
}

static DDS_ReturnCode_t return_samples(void *opaque, DDS_DataReader *reader)
{
    PGW_SignalDDSState *state = opaque;
    return PGW_DDS_SignalDataReader_return_loan(PGW_DDS_SignalDataReader_narrow(reader),
                                              &state->data, &state->info);
}

static PGW_Status copy_native(const void *opaque, void *out, size_t size)
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

static DDS_ReturnCode_t write_sample(void *opaque, DDS_DataWriter *writer,
                                     const void *native, const struct DDS_Time_t *time)
{
    if (!opaque || !native) return DDS_RETCODE_BAD_PARAMETER;
    PGW_SignalDDSState *state = opaque;
    const PGW_Signal *value = native;
    const PGW_SignalDescriptor *descriptor = PGW_codec_signal_find(value->id);
    if (!descriptor || value->value.kind != descriptor->kind) return DDS_RETCODE_BAD_PARAMETER;
    state->scratch.id = value->id;
    switch (value->value.kind) {
    case PGW_VALUE_BOOLEAN:
        state->scratch.value._d = VALUE_BOOLEAN;
        state->scratch.value._u.boolean_value = value->value.data.boolean
            ? DDS_BOOLEAN_TRUE : DDS_BOOLEAN_FALSE;
        break;
    case PGW_VALUE_INT64:
        state->scratch.value._d = VALUE_INT64;
        state->scratch.value._u.integer_value = value->value.data.integer;
        break;
    case PGW_VALUE_DOUBLE:
        if (!isfinite(value->value.data.real)) return DDS_RETCODE_BAD_PARAMETER;
        state->scratch.value._d = VALUE_DOUBLE;
        state->scratch.value._u.real_value = value->value.data.real;
        break;
    default:
        return DDS_RETCODE_BAD_PARAMETER;
    }
    if (time) return PGW_DDS_SignalDataWriter_write_w_timestamp(
        PGW_DDS_SignalDataWriter_narrow(writer), &state->scratch, &DDS_HANDLE_NIL, time);
    return PGW_DDS_SignalDataWriter_write(PGW_DDS_SignalDataWriter_narrow(writer),
                                        &state->scratch, &DDS_HANDLE_NIL);
}

static PGW_Status register_category(void *opaque, DDS_DataWriter *writer, const char *category)
{
    PGW_SignalDDSState *state = opaque;
    struct DDS_Time_t epoch = {0, 0};
    for (size_t i = 0; i < PGW_codec_signal_count; ++i) {
        const PGW_SignalDescriptor *descriptor = &PGW_codec_signals[i];
        if (category && strcmp(category, descriptor->category)) continue;
        state->scratch.id = descriptor->id;
        state->scratch.value._d = (PGW_DDS_ValueKind)descriptor->kind;
        DDS_InstanceHandle_t handle = PGW_DDS_SignalDataWriter_register_instance_w_timestamp(
            PGW_DDS_SignalDataWriter_narrow(writer), &state->scratch, &epoch);
        if (DDS_InstanceHandle_equals(&handle, &DDS_HANDLE_NIL)) return PGW_FATAL;
    }
    return PGW_OK;
}

static PGW_Status register_keys(void *opaque, DDS_DataWriter *writer)
{
    return register_category(opaque, writer, NULL);
}

static const PGW_Schema schema = {
    PGW_CODEC_SCHEMA_NAME, PGW_CODEC_SCHEMA_VERSION, PGW_CODEC_SCHEMA_FINGERPRINT
};
static const PGW_Representation representation = {
    &schema, "signal.native", sizeof(PGW_Signal), _Alignof(PGW_Signal), NULL
};
#define PGW_SIGNAL_BINDING(register_function) { \
    &representation, "Signal", sizeof(PGW_Signal), initialize, \
    take_samples, length, data, info, return_samples, copy_native, write_sample, register_function \
}
const PGW_DDSBinding PGW_signal_dds_binding = PGW_SIGNAL_BINDING(register_keys);
#define PGW_SIGNAL_DEFINE_CATEGORY(name) \
    static PGW_Status register_##name(void *opaque, DDS_DataWriter *writer) \
    { return register_category(opaque, writer, #name); } \
    const PGW_DDSBinding PGW_signal_dds_binding_##name = PGW_SIGNAL_BINDING(register_##name);
PGW_CODEC_CATEGORIES(PGW_SIGNAL_DEFINE_CATEGORY)
#undef PGW_SIGNAL_DEFINE_CATEGORY
#undef PGW_SIGNAL_BINDING

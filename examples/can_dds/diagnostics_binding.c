#include "diagnostics_binding.h"
#include "diagnosticsSupport.h"
#include "schema_fingerprints.h"
#include <string.h>
_Static_assert(PGW_COUNT_TOTAL == 12, "Regenerate management IDL after counter schema changes");
_Static_assert(sizeof(((PGWManagement_Snapshot *)0)->counters) /
               sizeof(((PGWManagement_Snapshot *)0)->counters[0]) == PGW_COUNT_TOTAL,
               "Generated management array must match the core counter inventory");
typedef struct {
    struct PGWManagement_SnapshotSeq data;
    struct DDS_SampleInfoSeq info;
    PGWManagement_Snapshot scratch;
} State;
static PGW_Status initialize(PGW_Arena *arena, size_t capacity, void **out)
{
    State *state;
    PGW_Status rc = PGW_Arena_allocate(arena, sizeof(*state), _Alignof(State),
                                      (void **)&state);
    (void)capacity;
    if (rc != PGW_OK) return rc;
    memset(state, 0, sizeof(*state));
    if (!PGWManagement_SnapshotSeq_initialize(&state->data) ||
        !DDS_SampleInfoSeq_initialize(&state->info) ||
        !PGWManagement_Snapshot_initialize(&state->scratch)) return PGW_FATAL;
    *out = state;
    return PGW_OK;
}
static DDS_ReturnCode_t take_samples(void *opaque, DDS_DataReader *reader, size_t count)
{
    State *state = opaque;
    return PGWManagement_SnapshotDataReader_take(PGWManagement_SnapshotDataReader_narrow(reader),
        &state->data, &state->info, (DDS_Long)count, DDS_ANY_SAMPLE_STATE,
        DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
}
static size_t length(void *opaque)
{
    return (size_t)PGWManagement_SnapshotSeq_get_length(&((State *)opaque)->data);
}
static const void *data(void *opaque, size_t index)
{
    return PGWManagement_SnapshotSeq_get_reference(&((State *)opaque)->data, (DDS_Long)index);
}
static const struct DDS_SampleInfo *info(void *opaque, size_t index)
{
    return DDS_SampleInfoSeq_get_reference(&((State *)opaque)->info, (DDS_Long)index);
}
static DDS_ReturnCode_t return_samples(void *opaque, DDS_DataReader *reader)
{
    State *state = opaque;
    return PGWManagement_SnapshotDataReader_return_loan(PGWManagement_SnapshotDataReader_narrow(reader),
                                                       &state->data, &state->info);
}
static PGW_Status copy_native(const void *opaque, void *out, size_t size)
{
    const PGWManagement_Snapshot *value = opaque;
    PGW_CounterSnapshot *snapshot = out;
    if (size != sizeof(*snapshot) || value->entity_kind != 1) return PGW_INVALID;
    snapshot->version = value->version;
    snapshot->entity_id = value->entity_id;
    snapshot->sequence = value->snapshot_sequence;
    snapshot->collected_ns = value->collected_ns;
    for (size_t i = 0; i < PGW_COUNT_TOTAL; ++i) snapshot->values[i] = value->counters[i];
    return PGW_OK;
}
static DDS_ReturnCode_t write_sample(void *opaque, DDS_DataWriter *writer,
                                     const void *native, const struct DDS_Time_t *time)
{
    State *state = opaque;
    const PGW_CounterSnapshot *snapshot = native;
    if (snapshot->entity_id < 1 || snapshot->entity_id > 4)
        return DDS_RETCODE_BAD_PARAMETER;
    state->scratch.entity_kind = 1;
    state->scratch.version = snapshot->version;
    state->scratch.entity_id = snapshot->entity_id;
    state->scratch.snapshot_sequence = snapshot->sequence;
    state->scratch.collected_ns = snapshot->collected_ns;
    for (size_t i = 0; i < PGW_COUNT_TOTAL; ++i) state->scratch.counters[i] = snapshot->values[i];
    if (time) return DDS_RETCODE_UNSUPPORTED;
    return PGWManagement_SnapshotDataWriter_write(PGWManagement_SnapshotDataWriter_narrow(writer),
                                                  &state->scratch, &DDS_HANDLE_NIL);
}
static PGW_Status register_keys(void *opaque, DDS_DataWriter *writer)
{
    State *state = opaque;
    state->scratch.entity_kind = 1;
    for (DDS_UnsignedLong id = 1; id <= 4; ++id) {
        DDS_InstanceHandle_t handle;
        state->scratch.entity_id = id;
        handle = PGWManagement_SnapshotDataWriter_register_instance(
            PGWManagement_SnapshotDataWriter_narrow(writer), &state->scratch);
        if (DDS_InstanceHandle_equals(&handle, &DDS_HANDLE_NIL)) return PGW_FATAL;
    }
    return PGW_OK;
}
static PGW_Status native_copy(const PGW_Sample *sample, void *out, size_t size)
{
    if (!sample || !out || size != sizeof(PGW_CounterSnapshot)) return PGW_INVALID;
    memcpy(out, sample, size);
    return PGW_OK;
}
static const PGW_SampleAccessI access_i = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), native_copy, NULL
};
static const PGW_Schema schema = {
    "pgw.diagnostics", 1, PGW_DIAGNOSTICS_SCHEMA_FINGERPRINT
};
static const PGW_Representation representation = {
    &schema, "diagnostics.native", sizeof(PGW_CounterSnapshot),
    _Alignof(PGW_CounterSnapshot), &access_i
};
const PGW_DDSBinding PGW_diagnostics_binding = {
    &representation, "Snapshot", sizeof(PGW_CounterSnapshot), initialize,
    take_samples, length, data, info, return_samples, copy_native,
    write_sample, register_keys
};
PGW_Status PGW_DDS_export_snapshot(PGW_StreamWriter *writer,
                                 const PGW_CounterSnapshot *snapshot)
{
    PGW_SampleSeq seq;
    PGW_SampleRef reference = (const PGW_Sample *)snapshot;
    PGW_WriteResult result = PGW_WRITE_FATAL;
    PGW_WriteResultSeq result_sequence;
    PGW_Status status;
    if (!writer || !snapshot || !PGW_SampleSeq_initialize(&seq)) return PGW_INVALID;
    if (!PGW_SampleSeq_loan_contiguous(&seq, &reference, 1, 1)) {
        PGW_SampleSeq_finalize(&seq);
        return PGW_LOAN_ERROR;
    }
    if (!PGW_WriteResultSeq_initialize(&result_sequence)) {
        status = PGW_INVALID;
        goto cleanup;
    }
    if (!PGW_WriteResultSeq_loan_contiguous(&result_sequence, &result, 0, 1)) {
        PGW_WriteResultSeq_finalize(&result_sequence);
        status = PGW_LOAN_ERROR;
        goto cleanup;
    }
    status = writer->iface->write(writer->state, &seq, &result_sequence);
    PGW_WriteResultSeq_unloan(&result_sequence);
    PGW_WriteResultSeq_finalize(&result_sequence);
cleanup:
    PGW_SampleSeq_unloan(&seq);
    PGW_SampleSeq_finalize(&seq);
    if (status != PGW_OK) return status;
    return result == PGW_WRITE_ACCEPTED ? PGW_OK :
        result == PGW_WRITE_BACKPRESSURE ? PGW_BACKPRESSURE : PGW_INVALID;
}

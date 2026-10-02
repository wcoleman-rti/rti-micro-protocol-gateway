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

#include "probe_binding.h"
#include "probeSupport.h"
#include "schema_fingerprints.h"
#include <string.h>
typedef struct {
    struct PGWTest_ProbeSeq data;
    struct DDS_SampleInfoSeq info;
    PGWTest_Probe scratch;
} State;
static PGW_Status initialize(PGW_Arena *arena, size_t capacity, void **out)
{
    State *state;
    PGW_Status rc = PGW_Arena_allocate(arena, sizeof(*state), _Alignof(State),
                                      (void **)&state);
    (void)capacity;
    if (rc != PGW_OK) return rc;
    memset(state, 0, sizeof(*state));
    if (!PGWTest_ProbeSeq_initialize(&state->data) ||
        !DDS_SampleInfoSeq_initialize(&state->info) ||
        !PGWTest_Probe_initialize(&state->scratch)) return PGW_FATAL;
    *out = state;
    return PGW_OK;
}
static DDS_ReturnCode_t take_samples(void *opaque, DDS_DataReader *reader, size_t count)
{
    State *state = opaque;
    return PGWTest_ProbeDataReader_take(PGWTest_ProbeDataReader_narrow(reader),
        &state->data, &state->info, (DDS_Long)count, DDS_ANY_SAMPLE_STATE,
        DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
}
static size_t length(void *opaque)
{
    return (size_t)PGWTest_ProbeSeq_get_length(&((State *)opaque)->data);
}
static const void *data(void *opaque, size_t index)
{
    return PGWTest_ProbeSeq_get_reference(&((State *)opaque)->data, (DDS_Long)index);
}
static const struct DDS_SampleInfo *info(void *opaque, size_t index)
{
    return DDS_SampleInfoSeq_get_reference(&((State *)opaque)->info, (DDS_Long)index);
}
static DDS_ReturnCode_t return_samples(void *opaque, DDS_DataReader *reader)
{
    State *state = opaque;
    return PGWTest_ProbeDataReader_return_loan(PGWTest_ProbeDataReader_narrow(reader),
                                              &state->data, &state->info);
}
static PGW_Status copy_native(const void *opaque, void *out, size_t size)
{
    const PGWTest_Probe *value = opaque;
    if (size != sizeof(PGW_ProbeValue)) return PGW_INVALID;
    *(PGW_ProbeValue *)out = (PGW_ProbeValue){value->id, value->reading};
    return PGW_OK;
}
static DDS_ReturnCode_t write_sample(void *opaque, DDS_DataWriter *writer,
                                     const void *native, const struct DDS_Time_t *time)
{
    State *state = opaque;
    const PGW_ProbeValue *value = native;
    state->scratch.id = value->id;
    state->scratch.reading = value->reading;
    if (time) return PGWTest_ProbeDataWriter_write_w_timestamp(
        PGWTest_ProbeDataWriter_narrow(writer), &state->scratch, &DDS_HANDLE_NIL, time);
    return PGWTest_ProbeDataWriter_write(PGWTest_ProbeDataWriter_narrow(writer),
                                       &state->scratch, &DDS_HANDLE_NIL);
}
static PGW_Status register_keys(void *opaque, DDS_DataWriter *writer)
{
    State *state = opaque;
    DDS_InstanceHandle_t handle;
    struct DDS_Time_t epoch = {0, 0};
    state->scratch.id = 1;
    /* Epoch registration permits subsequently preserved portable timestamps. */
    handle = PGWTest_ProbeDataWriter_register_instance_w_timestamp(
        PGWTest_ProbeDataWriter_narrow(writer), &state->scratch, &epoch);
    return DDS_InstanceHandle_equals(&handle, &DDS_HANDLE_NIL) ? PGW_FATAL : PGW_OK;
}
static const PGW_Schema schema = {
    "pgw.probe", 1, PGW_PROBE_SCHEMA_FINGERPRINT
};
static const PGW_Representation representation = {
    &schema, "probe.native", sizeof(PGW_ProbeValue), _Alignof(PGW_ProbeValue), NULL
};
const PGW_DDSBinding PGW_probe_binding = {
    &representation, "Probe", sizeof(PGW_ProbeValue), initialize,
    take_samples, length, data, info, return_samples, copy_native,
    write_sample, register_keys
};

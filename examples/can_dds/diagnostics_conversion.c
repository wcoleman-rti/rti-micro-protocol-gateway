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

#include "diagnostics_binding.h"
#include "diagnosticsSupport.h"
#include <pgw/diagnostics.h>
#include <string.h>

_Static_assert(PGW_COUNT_TOTAL == 12,
               "Regenerate management IDL after counter schema changes");
_Static_assert(sizeof(((PGWManagement_Snapshot *)0)->counters) /
               sizeof(((PGWManagement_Snapshot *)0)->counters[0]) == PGW_COUNT_TOTAL,
               "Generated management array must match the core counter inventory");

PGW_Status PGW_diagnostics_sample_copy(const PGW_Sample *sample, void *out,
                                       size_t size)
{
    if (!sample || !out || size != sizeof(PGW_CounterSnapshot)) return PGW_INVALID;
    memcpy(out, sample, size);
    return PGW_OK;
}

PGW_Status PGW_diagnostics_from_dds(const void *opaque, void *out, size_t size)
{
    if (!opaque || !out || size != sizeof(PGW_CounterSnapshot)) return PGW_INVALID;
    const PGWManagement_Snapshot *value = opaque;
    PGW_CounterSnapshot *snapshot = out;
    if (value->entity_kind != 1) return PGW_INVALID;
    snapshot->version = value->version;
    snapshot->entity_id = value->entity_id;
    snapshot->sequence = value->snapshot_sequence;
    snapshot->collected_ns = value->collected_ns;
    for (size_t i = 0; i < PGW_COUNT_TOTAL; ++i)
        snapshot->values[i] = value->counters[i];
    return PGW_OK;
}

DDS_ReturnCode_t PGW_diagnostics_to_dds(const void *opaque, void *out)
{
    if (!opaque || !out) return DDS_RETCODE_BAD_PARAMETER;
    const PGW_CounterSnapshot *snapshot = opaque;
    PGWManagement_Snapshot *sample = out;
    if (snapshot->entity_id < 1 || snapshot->entity_id > 4)
        return DDS_RETCODE_BAD_PARAMETER;
    sample->entity_kind = 1;
    sample->version = snapshot->version;
    sample->entity_id = snapshot->entity_id;
    sample->snapshot_sequence = snapshot->sequence;
    sample->collected_ns = snapshot->collected_ns;
    for (size_t i = 0; i < PGW_COUNT_TOTAL; ++i)
        sample->counters[i] = snapshot->values[i];
    return DDS_RETCODE_OK;
}

PGW_Status PGW_diagnostics_register_keys(void *opaque, DDS_DataWriter *writer)
{
    if (!opaque || !writer) return PGW_INVALID;
    PGWManagement_Snapshot *scratch = opaque;
    scratch->entity_kind = 1;
    for (DDS_UnsignedLong id = 1; id <= 4; ++id) {
        DDS_InstanceHandle_t handle;
        scratch->entity_id = id;
        handle = PGWManagement_SnapshotDataWriter_register_instance(
            PGWManagement_SnapshotDataWriter_narrow(writer), scratch);
        if (DDS_InstanceHandle_equals(&handle, &DDS_HANDLE_NIL)) return PGW_FATAL;
    }
    return PGW_OK;
}

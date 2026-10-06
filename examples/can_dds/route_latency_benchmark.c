/*
 * (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software. Licensee has the right to distribute object form only
 * for use with RTI products. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose.
 */

#define _POSIX_C_SOURCE 200809L
#include "route_latency_benchmark.h"
#include "pgw/dds/connext_micro.h"
#include "pgw/signal.h"
#include "osapi/osapi_thread.h"
#include <stdio.h>
#include <time.h>

bool PGW_example_route_latency_clock(void *context, uint64_t *out)
{
    struct timespec now;
    (void)context;
    if (!out || clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
    *out = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
        (uint64_t)now.tv_nsec;
    return true;
}

static int run_can_route_batches(PGW_Service *service, PGW_Connection *can,
                                 PGW_CANMemory *transport,
                                 const PGW_CANFrame *frame, size_t batches)
{
    for (size_t i = 0; i < batches; ++i) {
        if (PGW_CANMemory_inject(transport, frame) != PGW_OK ||
            PGW_CAN_poll(can, 4) != PGW_OK ||
            PGW_Service_step(service) != PGW_OK)
            return 1;
    }
    return 0;
}

static int verify_routed_dds_state(PGW_Connection *companion)
{
    PGW_StreamReader reader;
    PGW_SampleSeq samples;
    PGW_SampleRef references[8];
    if (PGW_DDSConnextMicroConnection.reader(
            companion, "state_powertrain", &reader) != PGW_OK ||
        !PGW_SampleSeq_initialize(&samples) ||
        !PGW_SampleSeq_loan_contiguous(&samples, references, 0, 8))
        return 1;
    bool found = false;
    for (unsigned attempt = 0; attempt < 100 && !found; ++attempt) {
        PGW_Status status = reader.iface->read(reader.state, &samples, 8);
        if (status == PGW_OK) {
            for (RTI_INT32 i = 0; i < PGW_SampleSeq_get_length(&samples); ++i) {
                PGW_Signal value;
                if (reader.representation->access->copy_value(
                        *PGW_SampleSeq_get_reference(&samples, i),
                        &value, sizeof(value)) != PGW_OK) {
                    (void)reader.iface->return_loan(reader.state, &samples);
                    (void)PGW_SampleSeq_unloan(&samples);
                    (void)PGW_SampleSeq_finalize(&samples);
                    return 1;
                }
                if (value.id == 1001 && value.value.kind == PGW_VALUE_DOUBLE &&
                    value.value.data.real == 1000.0)
                    found = true;
            }
            if (reader.iface->return_loan(reader.state, &samples) != PGW_OK) {
                (void)PGW_SampleSeq_unloan(&samples);
                (void)PGW_SampleSeq_finalize(&samples);
                return 1;
            }
        } else if (status != PGW_NO_DATA) {
            (void)PGW_SampleSeq_unloan(&samples);
            (void)PGW_SampleSeq_finalize(&samples);
            return 1;
        }
        if (!found) OSAPI_Thread_sleep(5);
    }
    if (!PGW_SampleSeq_unloan(&samples) ||
        !PGW_SampleSeq_finalize(&samples))
        return 1;
    return found ? 0 : 1;
}

int PGW_example_benchmark_routed_translation(
    PGW_Service *service, PGW_Route *route,
    const PGW_Representation *canonical_source, PGW_Connection *can,
    PGW_Connection *gateway, PGW_Connection *companion,
    PGW_CANMemory *transport, const PGW_CANFrame *frame)
{
    const size_t batches_per_path = 1000;
    const PGW_Representation *source = route->reader.representation;
    if (!source || !source->access || !source->access->view ||
        !canonical_source || !canonical_source->access ||
        !canonical_source->access->copy_value ||
        route->writer.iface->bind(route->writer.state, source) != PGW_OK ||
        run_can_route_batches(service, can, transport, frame, 32))
        return 1;

    PGW_SampleAccessI fallback_access = *canonical_source->access;
    fallback_access.view = NULL;
    PGW_Representation fallback = *canonical_source;
    fallback.access = &fallback_access;
    fallback.view_contract = NULL;
    route->reader.representation = canonical_source;
    if (route->writer.iface->bind(route->writer.state, &fallback) != PGW_OK ||
        run_can_route_batches(service, can, transport, frame, 32) ||
        route->writer.iface->bind(route->writer.state, source) != PGW_OK) {
        route->reader.representation = source;
        return 1;
    }
    route->reader.representation = source;

    PGW_RouteLatencySnapshot before, view_after, fallback_after;
    PGW_DDSStatistics dds_before, dds_view, dds_fallback;
    if (PGW_Route_latency_snapshot(route, &before) != PGW_OK ||
        PGW_DDS_statistics(gateway, "state_powertrain", &dds_before) != PGW_OK ||
        run_can_route_batches(service, can, transport, frame, batches_per_path) ||
        PGW_Route_latency_snapshot(route, &view_after) != PGW_OK ||
        PGW_DDS_statistics(gateway, "state_powertrain", &dds_view) != PGW_OK)
        return 1;
    route->reader.representation = canonical_source;
    if (route->writer.iface->bind(route->writer.state, &fallback) != PGW_OK ||
        run_can_route_batches(service, can, transport, frame, batches_per_path) ||
        PGW_Route_latency_snapshot(route, &fallback_after) != PGW_OK ||
        PGW_DDS_statistics(gateway, "state_powertrain", &dds_fallback) != PGW_OK ||
        verify_routed_dds_state(companion)) {
        route->reader.representation = source;
        (void)route->writer.iface->bind(route->writer.state, source);
        return 1;
    }
    route->reader.representation = source;
    if (route->writer.iface->bind(route->writer.state, source) != PGW_OK)
        return 1;

    uint64_t view_batches = view_after.timed_batches - before.timed_batches;
    uint64_t fallback_batches =
        fallback_after.timed_batches - view_after.timed_batches;
    uint64_t view_samples = view_after.samples - before.samples;
    uint64_t fallback_samples = fallback_after.samples - view_after.samples;
    uint64_t view_accepted = dds_view.accepted - dds_before.accepted;
    uint64_t fallback_accepted = dds_fallback.accepted - dds_view.accepted;
    uint64_t view_total = view_after.total_ns - before.total_ns;
    uint64_t fallback_total = fallback_after.total_ns - view_after.total_ns;
    if (view_batches != batches_per_path ||
        fallback_batches != batches_per_path ||
        view_samples != batches_per_path * 4 ||
        fallback_samples != batches_per_path * 4 ||
        view_accepted != view_samples || fallback_accepted != fallback_samples)
        return 1;
    printf("Gateway route batch benchmark: batches_per_path=%zu "
           "write_view_average_ns=%llu canonical_average_ns=%llu "
           "samples_per_batch=4 accepted_per_path=%llu\n",
           batches_per_path,
           (unsigned long long)(view_total / view_batches),
           (unsigned long long)(fallback_total / fallback_batches),
           (unsigned long long)view_accepted);
    return 0;
}

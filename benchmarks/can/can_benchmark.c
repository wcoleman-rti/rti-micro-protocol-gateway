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

#define _POSIX_C_SOURCE 200809L
#include "pgw/can_memory.h"
#include "pgw_codec.h"
#include "allocation.h"
#include "osapi/osapi_heap.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { CAN_ARENA_BYTES = 16384 };

typedef struct {
    uint64_t buckets[64];
    uint64_t count, minimum, maximum, sum;
} Histogram;

static uint64_t now_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static bool service_clock(void *context, uint64_t *nanoseconds)
{
    (void)context;
    if (!nanoseconds) return false;
    *nanoseconds = now_ns();
    return *nanoseconds != 0;
}

static void observe(Histogram *h, uint64_t duration)
{
    unsigned bucket = 0;
    for (uint64_t n = duration; n > 1; n >>= 1) ++bucket;
    ++h->buckets[bucket];
    ++h->count;
    if (duration < h->minimum) h->minimum = duration;
    if (duration > h->maximum) h->maximum = duration;
    assert(h->sum <= UINT64_MAX - duration);
    h->sum += duration;
}

static uint64_t percentile(const Histogram *h, uint64_t percent)
{
    uint64_t target = (h->count * percent + 99) / 100, count = 0;
    if (!h->count) return 0;
    for (unsigned i = 0; i < 64; ++i) {
        count += h->buckets[i];
        if (count >= target)
            return i == 63 ? UINT64_MAX : (UINT64_C(1) << (i + 1)) - 1;
    }
    return 0;
}

static PGW_CodecStatus decode(void *unused, size_t index, const uint8_t *data,
                             size_t length, PGW_Signal *out, size_t capacity,
                             size_t *count)
{
    const PGW_MessageDescriptor *m = &PGW_codec_messages[index];
    (void)unused;
    return PGW_codec_decode(m->frame_id, m->extended, m->fd, data, length,
                            out, capacity, count);
}

static PGW_CodecStatus patch(void *unused, uint32_t id, const PGW_Value *value,
                            uint8_t *bytes, size_t length)
{
    PGW_Signal signal = {id, *value};
    (void)unused;
    return PGW_codec_patch(&signal, bytes, length);
}

static void allocation_controls(void)
{
    void *(*volatile malloc_fn)(size_t) = malloc;
    void *(*volatile calloc_fn)(size_t, size_t) = calloc;
    void *(*volatile realloc_fn)(void *, size_t) = realloc;
    void *(*volatile aligned_fn)(size_t, size_t) = aligned_alloc;
    int (*volatile posix_fn)(void **, size_t, size_t) = posix_memalign;
    void *p;
    PGW_allocation_monitor(true);
    p = malloc_fn(16); assert(p);
    p = realloc_fn(p, 32); assert(p); free(p);
    p = calloc_fn(1, 16); assert(p); free(p);
    p = aligned_fn(16, 32); assert(p); free(p);
    assert(!posix_fn(&p, 16, 32)); free(p);
    assert(PGW_allocation_calls() == 5);
    p = OSAPI_Heap_allocate(1, 32); assert(p); OSAPI_Heap_free(p);
    uint64_t before = PGW_osapi_allocation_calls();
    assert(before);
    p = OSAPI_Heap_realloc(NULL, 32); assert(p); OSAPI_Heap_free(p);
    assert(PGW_osapi_allocation_calls() > before);
    before = PGW_osapi_allocation_calls();
    char *buffer = NULL;
    OSAPI_Heap_allocate_buffer(&buffer, 32, OSAPI_ALIGNMENT_DEFAULT);
    assert(buffer);
    OSAPI_Heap_free_buffer(buffer);
    assert(PGW_osapi_allocation_calls() > before);
    PGW_allocation_monitor(false);
}

static int usage(const char *name)
{
    fprintf(stderr, "usage: %s [steps:1..100000000] [timing:0|1] [metadata:0|1]\n", name);
    return 2;
}

int main(int argc, char **argv)
{
    uint64_t steps = 100000, checked_frames = 0;
    bool timing = true, metadata = true;
    if (argc > 4) return usage(argv[0]);
    if (argc > 1) {
        char *end;
        errno = 0;
        steps = strtoull(argv[1], &end, 10);
        if (errno || !argv[1][0] || *end || argv[1][0] == '-' ||
            !steps || steps > 100000000) return usage(argv[0]);
    }
    if (argc > 2) {
        if ((argv[2][0] != '0' && argv[2][0] != '1') || argv[2][1])
            return usage(argv[0]);
        timing = argv[2][0] == '1';
    }
    if (argc > 3) {
        if ((argv[3][0] != '0' && argv[3][0] != '1') || argv[3][1])
            return usage(argv[0]);
        metadata = argv[3][0] == '1';
    }
    allocation_controls();
    uint64_t init_start = now_ns();
    assert(init_start);
    const size_t initialization_heap_bytes = (size_t)CAN_ARENA_BYTES * 2;
    void *storage = malloc(initialization_heap_bytes);
    if (!storage) return 3;
    PGW_Arena arenas[2] = {
        {storage, CAN_ARENA_BYTES, 0},
        {(unsigned char *)storage + CAN_ARENA_BYTES, CAN_ARENA_BYTES, 0}
    };
    PGW_CANFrame rx[2][2], tx[2][1], baseline = {0}, expected = {0};
    PGW_CANMemory memory[2];
    PGW_Connection *connections[2];
    PGW_Schema schema = {PGW_codec_schema.name, PGW_codec_schema.version,
                         PGW_codec_schema.fingerprint};
    PGW_CANCategory categories[2] = {
        {"powertrain", 4, &schema}, {"auxiliary", 4, &schema}
    };
    PGW_CANCategorySeq category_sequence;
    PGW_CANConfig config = {0};
    assert(PGW_CANCategorySeq_initialize(&category_sequence));
    assert(PGW_CANCategorySeq_loan_contiguous(&category_sequence, categories, 2, 2));
    assert(PGW_CANMapping_initialize(&config.mapping,
        PGW_codec_messages, PGW_codec_message_count,
        PGW_codec_signals, PGW_codec_signal_count,
        NULL, decode, patch) == PGW_OK);
    assert(PGW_CANConfig_set_categories(&config, &category_sequence) == PGW_OK);
    config.receive_budget = 1; config.write_capacity = 4;
    config.disable_metadata_capture = !metadata;
    size_t bound = 0;
    assert(PGW_CAN_storage_size(&config, &bound) == PGW_OK);
    assert(bound <= CAN_ARENA_BYTES);
    for (size_t i = 0; i < 2; ++i) {
        assert(PGW_CANMemory_initialize(&memory[i], rx[i], 2, tx[i], 1) == PGW_OK);
        config.entity_id = (uint32_t)i + 1;
        config.transport = PGW_CANMemory_transport(&memory[i]);
        assert(PGW_CANAdapter.create(&config, &arenas[i], &connections[i]) == PGW_OK);
    }
    PGW_SampleRef references[4];
    PGW_WriteResult results[4];
    PGW_SampleSeq route_sample_storage;
    PGW_WriteResultSeq route_result_storage;
    PGW_RouteSeq route_sequence;
    PGW_Route route = {.id = 1};
    assert(PGW_CANAdapter.connection->reader(connections[0], "powertrain",
                                             &route.reader) == PGW_OK);
    assert(PGW_CANAdapter.connection->writer(connections[1], "powertrain",
                                             &route.writer) == PGW_OK);
    assert(PGW_SampleSeq_initialize(&route_sample_storage));
    assert(PGW_SampleSeq_loan_contiguous(&route_sample_storage, references, 0, 4));
    assert(PGW_WriteResultSeq_initialize(&route_result_storage));
    assert(PGW_WriteResultSeq_loan_contiguous(&route_result_storage, results, 0, 4));
    assert(PGW_Route_initialize_storage(&route, &route_sample_storage,
                                        &route_result_storage) == PGW_OK);
    assert(PGW_RouteSeq_initialize(&route_sequence));
    assert(PGW_RouteSeq_loan_contiguous(&route_sequence, &route, 1, 1));
    PGW_Service service = {
        .route_budget = 1, .sample_budget = 4,
        .clock_ns = service_clock
    };
    assert(PGW_Service_set_routes(&service, &route_sequence) == PGW_OK);
    assert(PGW_Service_initialize(&service) == PGW_OK);
    PGW_StreamReader baseline_reader;
    PGW_SampleSeq baseline_loan;
    PGW_SampleRef baseline_references[4];
    assert(PGW_CANAdapter.connection->reader(connections[1], "powertrain",
                                             &baseline_reader) == PGW_OK);
    assert(PGW_SampleSeq_initialize(&baseline_loan));
    assert(PGW_SampleSeq_loan_contiguous(&baseline_loan, baseline_references, 0, 4));
    uint64_t init_ns = now_ns() - init_start;
    Histogram histogram = {.minimum = UINT64_MAX};
    baseline.id = 0x100; baseline.length = 8;
    const uint8_t original[] = {0xe8, 0x03, 0xff, 0x0a, 0xa5, 2, 0xcc, 0xdd};
    memcpy(baseline.data, original, sizeof(original));
    baseline.timestamp = (PGW_Timestamp){true, true, 42, 123};
    expected = baseline;
    PGW_allocation_monitor(true);
    uint64_t start = now_ns();
    assert(start);
    /* The first received baseline and the first routed traffic are monitored. */
    assert(PGW_CANMemory_inject(&memory[1], &baseline) == PGW_OK);
    assert(PGW_CAN_poll(connections[1], 1) == PGW_OK);
    assert(baseline_reader.iface->read(baseline_reader.state, &baseline_loan, 4) == PGW_OK);
    assert(PGW_SampleSeq_get_length(&baseline_loan) == 4);
    for (RTI_INT32 i = 0; i < 4; ++i) {
        PGW_Timestamp timestamp;
        PGW_Status status = baseline_reader.representation->access->source_timestamp(
            *PGW_SampleSeq_get_reference(&baseline_loan, i), &timestamp);
        assert(status == (metadata ? PGW_OK : PGW_NO_DATA));
        assert(timestamp.valid == metadata);
        if (metadata) assert(timestamp.seconds == 42 && timestamp.nanoseconds == 123);
    }
    assert(baseline_reader.iface->return_loan(baseline_reader.state, &baseline_loan) == PGW_OK);
    bool queued = false;
    for (uint64_t i = 0; i < steps; ++i) {
        PGW_CANFrame ingress = {0}, want = baseline, sent;
        uint16_t speed = (uint16_t)(1000 + i % 1000);
        ingress.id = 0x100; ingress.length = 8;
        ingress.data[0] = want.data[0] = (uint8_t)speed;
        ingress.data[1] = want.data[1] = (uint8_t)(speed >> 8);
        ingress.data[2] = 0xff; ingress.data[3] = 0xe0;
        ingress.data[4] = 0; ingress.data[5] = (uint8_t)(i % 4);
        ingress.data[6] = 0x11; ingress.data[7] = 0x22;
        ingress.timestamp = (PGW_Timestamp){true, true, 43,
                                           (uint32_t)(i % 1000000000)};
        want.data[2] = 0xff; want.data[3] = 0xea;
        want.data[4] = 0xa4; want.data[5] = (uint8_t)(i % 4);
        assert(PGW_CANMemory_inject(&memory[0], &ingress) == PGW_OK);
        uint64_t before = timing ? now_ns() : 0;
        assert(PGW_Service_step(&service) == PGW_OK);
        uint64_t after = timing ? now_ns() : 0;
        if (timing) {
            assert(before && after >= before);
            observe(&histogram, after - before);
        }
        bool blocked = i % 8 == 7;
        assert(PGW_WriteResultSeq_get_length(&route.results) == 4);
        for (RTI_INT32 j = 0; j < PGW_WriteResultSeq_get_length(&route.results); ++j)
            assert(*PGW_WriteResultSeq_get_reference(&route.results, j) ==
                   (blocked ? PGW_WRITE_BACKPRESSURE : PGW_WRITE_ACCEPTED));
        if (!blocked) {
            assert(!queued);
            expected = want;
            queued = true;
        }
        assert(memory[1].tx_count == 1);
        if (i % 8 != 6) {
            assert(PGW_CANMemory_take_sent(&memory[1], &sent) == PGW_OK);
            assert(sent.id == expected.id && sent.flags == 0 && sent.length == 8);
            assert(!memcmp(sent.data, expected.data, 8));
            assert(!sent.timestamp.valid);
            ++checked_frames;
            queued = false;
        }
    }
    if (queued) {
        PGW_CANFrame sent;
        assert(PGW_CANMemory_take_sent(&memory[1], &sent) == PGW_OK);
        assert(!memcmp(sent.data, expected.data, 8));
        ++checked_frames;
    }
    uint64_t end = now_ns();
    assert(end >= start);
    uint64_t elapsed = end - start;
    PGW_CANStats input_stats, output_stats;
    PGW_CounterSnapshot counters;
    assert(PGW_CAN_stats(connections[0], &input_stats) == PGW_OK);
    assert(PGW_CAN_stats(connections[1], &output_stats) == PGW_OK);
    PGW_Counters_snapshot(&route.counters, 1, 1, end, &counters);
    uint64_t blocked_frames = steps / 8;
    assert(input_stats.received_frames == steps &&
           input_stats.decoded_samples == steps * 4 &&
           !input_stats.receive_drops && !input_stats.invalid_commands);
    assert(output_stats.received_frames == 1 && output_stats.decoded_samples == 4 &&
           output_stats.accepted_commands == (steps - blocked_frames) * 4 &&
           output_stats.backpressure_commands == blocked_frames * 4 &&
           !output_stats.invalid_commands && !output_stats.receive_drops);
    assert(checked_frames == steps - blocked_frames);
    assert(memory[1].tx_backpressure == blocked_frames);
    assert(counters.values[PGW_COUNT_RECEIVED] == steps * 4 &&
           counters.values[PGW_COUNT_ACCEPTED] == checked_frames * 4 &&
           counters.values[PGW_COUNT_BACKPRESSURE] == blocked_frames * 4 &&
           counters.values[PGW_COUNT_LOANS] == 0 &&
           counters.values[PGW_COUNT_INVALID] == 0 &&
           counters.values[PGW_COUNT_FATAL] == 0);
    assert(PGW_Service_stop(&service) == PGW_OK);
    uint64_t libc_allocations = PGW_allocation_calls();
    uint64_t osapi_allocations = PGW_osapi_allocation_calls();
    assert(!libc_allocations && !osapi_allocations);
    PGW_allocation_monitor(false);
    assert(PGW_Service_finalize(&service) == PGW_OK);
    assert(PGW_RouteSeq_unloan(&route_sequence));
    assert(PGW_RouteSeq_finalize(&route_sequence));
    assert(PGW_SampleSeq_unloan(&route_sample_storage));
    assert(PGW_SampleSeq_finalize(&route_sample_storage));
    assert(PGW_WriteResultSeq_unloan(&route_result_storage));
    assert(PGW_WriteResultSeq_finalize(&route_result_storage));
    assert(PGW_SampleSeq_unloan(&baseline_loan));
    assert(PGW_SampleSeq_finalize(&baseline_loan));
    for (size_t i = 0; i < 2; ++i)
        assert(PGW_CANAdapter.connection->close(connections[i]) == PGW_OK);
    assert(PGW_CANConfig_finalize(&config) == PGW_OK);
    assert(PGW_CANCategorySeq_unloan(&category_sequence));
    assert(PGW_CANCategorySeq_finalize(&category_sequence));
    free(storage);
    size_t fixture_bytes = sizeof(storage) + sizeof(arenas) + sizeof(rx) +
        sizeof(tx) + sizeof(memory) + sizeof(connections) + sizeof(categories) +
        sizeof(schema) + sizeof(config) + sizeof(route) + sizeof(service) +
        sizeof(references) + sizeof(results) + sizeof(histogram) +
        sizeof(baseline_reader) + sizeof(baseline_loan) + sizeof(baseline_references);
    printf("{\"format_version\":1,\"workload\":\"memory-can-decode-core-patch-backpressure-v1\","
        "\"steps\":%" PRIu64 ",\"samples\":%" PRIu64 ",\"timing\":%s,\"metadata\":%s,"
        "\"elapsed_ns\":%" PRIu64 ",\"initialization_ns\":%" PRIu64 ","
        "\"samples_per_second\":%.3f,\"sample_rate_basis\":\"four routed command attempts per step\","
        "\"gateway_runtime_allocations\":%" PRIu64 ",\"osapi_runtime_allocations\":%" PRIu64 ","
        "\"allocation_coverage\":\"wrapped libc malloc/calloc/realloc/aligned_alloc/posix_memalign and OSAPI allocate/realloc/allocate_buffer; memory CAN and core; all eight controls checked\","
        "\"received_frames\":%" PRIu64 ",\"decoded_signals\":%" PRIu64 ","
        "\"routed_samples\":%" PRIu64 ",\"transmit_accepted_commands\":%" PRIu64 ","
        "\"transmit_accepted_frames\":%" PRIu64 ",\"backpressure_commands\":%" PRIu64 ","
        "\"command_bytes_checked\":%" PRIu64 ",\"invalid_commands\":0,\"rx_dropped_signals\":0,"
        "\"outstanding_loans\":0,\"receive_frame_budget\":1,\"route_sample_capacity\":4,"
        "\"category_capacity\":4,\"transport_rx_capacity\":2,\"transport_tx_capacity\":1,"
        "\"queue_high_water\":%zu,\"arena_bytes_used\":%zu,\"arena_bytes_bound\":%zu,"
        "\"gateway_static_fixture_bytes\":%zu,\"immutable_descriptor_bytes\":%zu,"
        "\"arena_reserved_bytes\":%zu,\"gateway_initialization_heap_bytes\":%zu,"
        "\"gateway_initialization_heap_blocks\":1,\"arena_backing\":\"initialization-only malloc; no declared backing type\","
        "\"schema_fingerprint\":\"%s\","
        "\"baseline_frames\":1,\"backpressure_period_steps\":8,"
        "\"metadata_capture\":{\"receive_timestamp\":%s,\"frame_context\":%s,"
        "\"configurable\":true,\"write_preservation\":false,\"timestamp_access_checks\":4,"
        "\"meaning\":\"injected portable RX timestamps, not native CAN sender timestamps\"},"
        "\"step_latency_ns\":{\"boundary\":\"PGW_Service_step entry to return: CAN RX dequeue/decode, core forward, CAN stage/send, loan return; not wire latency\","
        "\"count\":%" PRIu64 ",\"min\":%" PRIu64 ",\"max\":%" PRIu64 ",\"mean\":%.3f,"
        "\"p50_upper\":%" PRIu64 ",\"p95_upper\":%" PRIu64 ",\"p99_upper\":%" PRIu64 ","
        "\"histogram\":\"64 power-of-two buckets, upper-bound percentiles; bucket zero covers 0..1 ns\"}}\n",
        steps, steps * 4, timing ? "true" : "false", metadata ? "true" : "false",
        elapsed, init_ns,
        elapsed ? steps * 4.0 * 1e9 / elapsed : 0.0,
        libc_allocations, osapi_allocations,
        input_stats.received_frames + output_stats.received_frames,
        input_stats.decoded_samples + output_stats.decoded_samples, steps * 4,
        output_stats.accepted_commands, checked_frames, output_stats.backpressure_commands,
        checked_frames * 8, input_stats.queue_high_water,
        input_stats.mutable_bytes + output_stats.mutable_bytes, bound * 2,
        fixture_bytes, PGW_codec_signal_count * sizeof(PGW_SignalDescriptor) +
            PGW_codec_message_count * sizeof(PGW_MessageDescriptor),
        initialization_heap_bytes, initialization_heap_bytes, PGW_codec_schema.fingerprint,
        metadata ? "true" : "false", metadata ? "true" : "false",
        histogram.count, histogram.count ? histogram.minimum : 0,
        histogram.maximum, histogram.count ? (double)histogram.sum / histogram.count : 0.0,
        percentile(&histogram, 50), percentile(&histogram, 95), percentile(&histogram, 99));
    return 0;
}

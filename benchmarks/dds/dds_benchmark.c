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
#include "pgw/dds/connext_micro.h"
#include "pgw/can_memory.h"
#include "pgw/runtime.h"
#include "pgw/compiled_config.h"
#include "pgw_codec.h"
#include "graph.h"
#include "probe_binding.h"
#include "diagnostics_binding.h"
#include "ddsAppgen.h"
#include "allocation.h"
#include "osapi/osapi_heap.h"
#include "osapi/osapi_thread.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

extern const PGW_DDSConfig pgw_config_gateway, pgw_config_companion;
typedef struct { uint64_t buckets[64], count, minimum, maximum, sum; } Histogram;
typedef struct { PGW_Signal value; } SignalSample;
typedef struct { PGW_ProbeValue value; PGW_Timestamp timestamp; } ProbeSample;
static atomic_bool frozen;
static atomic_uint_fast64_t runtime_arena_calls;
PGW_Status __real_PGW_Arena_allocate(PGW_Arena *, size_t, size_t, void **);
PGW_Status __wrap_PGW_Arena_allocate(PGW_Arena *arena, size_t bytes,
                                    size_t alignment, void **out)
{
    if (atomic_load(&frozen)) atomic_fetch_add(&runtime_arena_calls, 1);
    return __real_PGW_Arena_allocate(arena, bytes, alignment, out);
}
static uint64_t now_ns(void)
{
    struct timespec time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}
static void observe(Histogram *histogram, uint64_t duration)
{
    unsigned bucket = 0;
    for (uint64_t value = duration; value > 1; value >>= 1) ++bucket;
    ++histogram->buckets[bucket];
    ++histogram->count;
    if (duration < histogram->minimum) histogram->minimum = duration;
    if (duration > histogram->maximum) histogram->maximum = duration;
    assert(histogram->sum <= UINT64_MAX - duration);
    histogram->sum += duration;
}
static uint64_t percentile(const Histogram *histogram, uint64_t percent)
{
    uint64_t target = (histogram->count * percent + 99) / 100, count = 0;
    if (!histogram->count) return 0;
    for (unsigned i = 0; i < 64; ++i) {
        count += histogram->buckets[i];
        if (count >= target) return i == 63 ? UINT64_MAX : (UINT64_C(1) << (i + 1)) - 1;
    }
    return 0;
}
static void print_buckets(const Histogram *histogram)
{
    putchar('[');
    for (unsigned i = 0; i < 64; ++i)
        printf("%s%" PRIu64, i ? "," : "", histogram->buckets[i]);
    putchar(']');
}
static PGW_Status signal_copy(const PGW_Sample *sample, void *out, size_t size)
{
    if (size != sizeof(PGW_Signal)) return PGW_INVALID;
    *(PGW_Signal *)out = ((const SignalSample *)sample)->value;
    return PGW_OK;
}
static PGW_Status probe_copy(const PGW_Sample *sample, void *out, size_t size)
{
    if (size != sizeof(PGW_ProbeValue)) return PGW_INVALID;
    *(PGW_ProbeValue *)out = ((const ProbeSample *)sample)->value;
    return PGW_OK;
}
static PGW_Status probe_time(const PGW_Sample *sample, PGW_Timestamp *out)
{
    *out = ((const ProbeSample *)sample)->timestamp;
    return PGW_OK;
}
static const PGW_SampleAccessI signal_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), signal_copy, NULL, NULL};
static const PGW_SampleAccessI probe_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), probe_copy, probe_time, NULL};
static PGW_CodecStatus decode(void *context, size_t index, const uint8_t *bytes,
                             size_t length, PGW_Signal *values, size_t capacity, size_t *count)
{
    const PGW_MessageDescriptor *message = &PGW_codec_messages[index];
    (void)context;
    return PGW_codec_decode(message->frame_id, message->extended, message->fd,
                            bytes, length, values, capacity, count);
}
static PGW_CodecStatus patch(void *context, uint32_t id, const PGW_Value *value,
                            uint8_t *bytes, size_t length)
{
    PGW_Signal sample = {id, *value};
    (void)context;
    return PGW_codec_patch(&sample, bytes, length);
}
static void allocation_controls(void)
{
    void *(*volatile malloc_fn)(size_t) = malloc;
    void *(*volatile calloc_fn)(size_t, size_t) = calloc;
    void *(*volatile realloc_fn)(void *, size_t) = realloc;
    void *(*volatile aligned_fn)(size_t, size_t) = aligned_alloc;
    int (*volatile posix_fn)(void **, size_t, size_t) = posix_memalign;
    void *pointer;
    PGW_allocation_monitor(true);
    pointer = malloc_fn(16); assert(pointer);
    pointer = realloc_fn(pointer, 32); assert(pointer); free(pointer);
    pointer = calloc_fn(1, 16); assert(pointer); free(pointer);
    pointer = aligned_fn(16, 32); assert(pointer); free(pointer);
    assert(!posix_fn(&pointer, 16, 32)); free(pointer);
    assert(PGW_allocation_calls() == 5);
    pointer = OSAPI_Heap_allocate(1, 32); assert(pointer); OSAPI_Heap_free(pointer);
    uint64_t before = PGW_osapi_allocation_calls(); assert(before);
    pointer = OSAPI_Heap_realloc(NULL, 32); assert(pointer); OSAPI_Heap_free(pointer);
    assert(PGW_osapi_allocation_calls() > before);
    before = PGW_osapi_allocation_calls();
    char *buffer = NULL;
    OSAPI_Heap_allocate_buffer(&buffer, 32, OSAPI_ALIGNMENT_DEFAULT);
    assert(buffer); OSAPI_Heap_free_buffer(buffer);
    assert(PGW_osapi_allocation_calls() > before);
    PGW_allocation_monitor(false);
    unsigned char storage[16];
    PGW_Arena arena = {storage, sizeof(storage), 0};
    atomic_store(&frozen, true);
    assert(PGW_Arena_allocate(&arena, 1, 1, &pointer) == PGW_OK);
    assert(atomic_load(&runtime_arena_calls) == 1);
    atomic_store(&runtime_arena_calls, 0);
    atomic_store(&frozen, false);
}
/* This lock is shared with the DDS test harness across all project build trees. */
static int reserve_domain(void)
{
    int lock = open(PGW_BENCHMARK_DOMAIN_LOCK, O_CREAT | O_RDWR, 0600);
    int sockets[250];
    size_t count = 0;
    if (lock < 0 || flock(lock, LOCK_EX)) {
        if (lock >= 0) close(lock);
        return -1;
    }
    unsigned base = 7400u + 250u * PGW_BENCHMARK_DDS_DOMAIN;
    bool available = true;
    for (unsigned port = base; port < base + 250u && port <= 65535u; ++port) {
        struct sockaddr_in address = {.sin_family = AF_INET,
            .sin_port = htons((uint16_t)port), .sin_addr = {.s_addr = htonl(INADDR_ANY)}};
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {available = false; break;}
        sockets[count++] = fd;
        if (bind(fd, (struct sockaddr *)&address, sizeof(address))) {available = false; break;}
    }
    for (size_t i = 0; i < count; ++i) close(sockets[i]);
    if (!available) {close(lock); return -1;}
    return lock;
}
static uint64_t counter(const PGW_Route *route, PGW_CounterId id)
{
    PGW_CounterSnapshot snapshot;
    assert(PGW_Counters_snapshot(&route->counters, route->id, 0, 0, &snapshot));
    return snapshot.values[id];
}
static bool wait_session_dispatches(const PGW_Session *session,
                                   uint64_t expected)
{
    for (unsigned attempt = 0; attempt < 2000; ++attempt) {
        if (atomic_load_explicit(&session->dispatched_routes,
                                 memory_order_acquire) >= expected)
            return true;
        OSAPI_Thread_sleep(1);
    }
    return false;
}
static void wait_matches(PGW_Connection *gateway, PGW_Connection *companion)
{
    for (unsigned attempt = 0; attempt < 5000; ++attempt) {
        PGW_DDSStatistics state, command, probe;
        assert(PGW_DDS_statistics(gateway, "state_powertrain", &state) == PGW_OK);
        assert(PGW_DDS_statistics(companion, "command_powertrain", &command) == PGW_OK);
        assert(PGW_DDS_statistics(gateway, "probe", &probe) == PGW_OK);
        bool matched = state.matched == 1 && command.matched == 1 && probe.matched == 1;
#if PGW_DDS_DIAGNOSTICS
        PGW_DDSStatistics diagnostic;
        assert(PGW_DDS_statistics(gateway, "diagnostics", &diagnostic) == PGW_OK);
        matched = matched && diagnostic.matched == 1;
#endif
        if (matched) return;
        OSAPI_Thread_sleep(1);
    }
    assert(!"DDS matching timeout");
}
static void initialize_sequence(PGW_SampleSeq *sequence, PGW_SampleRef *refs, size_t capacity)
{
    assert(PGW_SampleSeq_initialize(sequence));
    assert(capacity <= INT32_MAX);
    assert(PGW_SampleSeq_loan_contiguous(sequence, refs, 0, (RTI_INT32)capacity));
}
static void finalize_sequence(PGW_SampleSeq *sequence)
{
    assert(PGW_SampleSeq_unloan(sequence));
    assert(PGW_SampleSeq_finalize(sequence));
}
static void write_one(PGW_StreamWriter *writer, PGW_SampleSeq *sequence,
                      PGW_SampleRef *slot, const void *sample, PGW_WriteResultSeq *results)
{
    *slot = (const PGW_Sample *)sample;
    assert(PGW_SampleSeq_set_length(sequence, 1));
    assert(writer->iface->write(writer->state, sequence, results) == PGW_OK);
    assert(PGW_WriteResultSeq_get_length(results) == 1);
    assert(*PGW_WriteResultSeq_get_reference(results, 0) == PGW_WRITE_ACCEPTED);
    assert(PGW_SampleSeq_set_length(sequence, 0));
}
static int usage(const char *name)
{
    fprintf(stderr, "usage: %s [batches:1..100000] [timing:0|1] [probe-timestamp-preservation:0|1]\n", name);
    return 2;
}
int main(int argc, char **argv)
{
    uint64_t batches = 1000;
    bool timing = true, preserve = false;
    if (argc > 4) return usage(argv[0]);
    if (argc > 1) {
        char *end;
        errno = 0;
        batches = strtoull(argv[1], &end, 10);
        if (errno || !argv[1][0] || argv[1][0] == '-' || *end ||
            !batches || batches > 100000)
            return usage(argv[0]);
    }
    for (int i = 2; i < argc; ++i)
        if ((argv[i][0] != '0' && argv[i][0] != '1') || argv[i][1]) return usage(argv[0]);
    if (argc > 2) timing = argv[2][0] == '1';
    if (argc > 3) preserve = argv[3][0] == '1';
    int domain_lock = reserve_domain();
    if (domain_lock < 0) {
        fprintf(stderr, "SKIP: private DDS domain %d unavailable\n", PGW_BENCHMARK_DDS_DOMAIN);
        return 77;
    }
    assert(PGW_Runtime_initialize());
    allocation_controls();
    uint64_t initialization_start = now_ns();
    PGW_allocation_monitor(true);
    const size_t storage_capacity = 524288;
    void *storage = malloc(storage_capacity);
    assert(storage);
    PGW_Arena arena = {storage, storage_capacity, 0};
    PGW_Connection *gateway, *companion, *can;
    PGW_DDSEndpointConfig endpoints[16];
    PGW_DDSConfig gateway_config = pgw_config_gateway;
    PGW_DDSEndpointConfigSeq endpoint_sequence;
    RTI_INT32 endpoint_count = PGW_DDSEndpointConfigSeq_get_length(&gateway_config.endpoints);
    assert(endpoint_count > 0 && endpoint_count <= 16);
    for (RTI_INT32 i = 0; i < endpoint_count; ++i)
        endpoints[i] = *PGW_DDSEndpointConfigSeq_get_reference(&gateway_config.endpoints, i);
    assert(PGW_DDSEndpointConfigSeq_initialize(&endpoint_sequence));
    assert(PGW_DDSEndpointConfigSeq_loan_contiguous(&endpoint_sequence, endpoints,
                                                    endpoint_count, endpoint_count));
    assert(PGW_DDSEndpointConfigSeq_set_length(&endpoint_sequence, endpoint_count));
    gateway_config.endpoints = endpoint_sequence;
    gateway_config.endpoints_initialized = true;
    for (RTI_INT32 i = 0; i < endpoint_count; ++i)
        if (!strcmp(endpoints[i].name, "probe")) endpoints[i].preserve_source_timestamp = preserve;
    assert(PGW_DDS_register_model(APPGEN_get_library_seq()) == PGW_OK);
    assert(PGW_DDS_create(&gateway_config, &arena, &gateway) == PGW_OK);
    assert(PGW_DDS_create(&pgw_config_companion, &arena, &companion) == PGW_OK);
    PGW_DDSResources gateway_resources, companion_resources;
    PGW_DDSHistoryResources history, reader_history;
    assert(PGW_DDS_effective_resources(gateway, &gateway_resources) == PGW_OK);
    assert(PGW_DDS_effective_resources(companion, &companion_resources) == PGW_OK);
    assert(PGW_DDS_effective_history(gateway, "state_powertrain", &history) == PGW_OK);
    assert(PGW_DDS_effective_history(companion, "state_powertrain", &reader_history) == PGW_OK);
    PGW_CANMemory memory;
    PGW_CANFrame rx[8], tx[1];
    assert(PGW_CANMemory_initialize(&memory, rx, 8, tx, 1) == PGW_OK);
    PGW_Schema schema = {PGW_codec_schema.name, PGW_codec_schema.version, PGW_codec_schema.fingerprint};
    PGW_CANCategory categories[2];
    PGW_CANCategorySeq category_sequence;
    PGW_CANConfig can_config = {.entity_id = 1, .transport = PGW_CANMemory_transport(&memory),
        .receive_budget = pgw_config_can_receive_budget,
        .write_capacity = pgw_config_can_write_capacity};
    assert(PGW_CANCategorySeq_initialize(&category_sequence));
    assert(PGW_CANCategorySeq_loan_contiguous(&category_sequence, categories, 2, 2));
    assert(PGW_example_can_categories(&schema, &category_sequence) == PGW_OK);
    assert(PGW_CANMapping_initialize(&can_config.mapping, PGW_codec_messages,
        PGW_codec_message_count, PGW_codec_signals, PGW_codec_signal_count,
        NULL, decode, patch) == PGW_OK);
    assert(PGW_CANConfig_set_categories(&can_config, &category_sequence) == PGW_OK);
    size_t can_bound;
    assert(PGW_CAN_storage_size(&can_config, &can_bound) == PGW_OK);
    assert(PGW_CANAdapter.create(&can_config, &arena, &can) == PGW_OK);
    PGW_Route routes[4] = {{0}};
    PGW_RouteSeq route_sequence;
    PGW_SampleRef route_refs[4][8];
    PGW_WriteResult results[4][8];
    PGW_SampleSeq route_sample_storage[4];
    PGW_WriteResultSeq route_result_storage[4];
    for (RTI_INT32 i = 0; i < 4; ++i) {
        initialize_sequence(&route_sample_storage[i], route_refs[i], 8);
        assert(PGW_WriteResultSeq_initialize(&route_result_storage[i]));
        assert(PGW_WriteResultSeq_loan_contiguous(&route_result_storage[i], results[i], 0, 8));
        assert(PGW_Route_initialize_storage(&routes[i], &route_sample_storage[i],
                                            &route_result_storage[i]) == PGW_OK);
    }
    assert(PGW_RouteSeq_initialize(&route_sequence));
    assert(PGW_RouteSeq_loan_contiguous(&route_sequence, routes, 4, 4));
    assert(PGW_example_attach_routes(can, gateway, &route_sequence) == PGW_OK);
    assert(PGW_CompiledSessionSeq_get_length(&pgw_config_sessions) == 1);
    const PGW_CompiledSession *compiled_session =
        PGW_CompiledSessionSeq_get_reference(&pgw_config_sessions, 0);
    assert(compiled_session && compiled_session->route_count == 4);
    PGW_Session session = {.name = compiled_session->name};
    assert(PGW_Session_set_routes(&session, &route_sequence) == PGW_OK);
    PGW_SessionSeq session_sequence;
    assert(PGW_SessionSeq_initialize(&session_sequence));
    assert(PGW_SessionSeq_loan_contiguous(&session_sequence, &session, 1, 1));
    PGW_Service service = {.sample_budget = pgw_config_sample_budget,
        .clock_ns = PGW_Runtime_monotonic_clock};
    assert(PGW_Service_set_sessions(&service, &session_sequence) == PGW_OK);
    assert(PGW_SessionSeq_unloan(&session_sequence));
    assert(PGW_SessionSeq_finalize(&session_sequence));
    assert(PGW_Service_initialize(&service) == PGW_OK);
    assert(PGW_Service_start(&service) == PGW_OK);
    PGW_StreamReader state_reader, probe_reader;
    PGW_StreamWriter command_writer, probe_writer;
    assert(PGW_DDSConnextMicroConnection.reader(companion, "state_powertrain", &state_reader) == PGW_OK);
    assert(PGW_DDSConnextMicroConnection.writer(companion, "command_powertrain", &command_writer) == PGW_OK);
    assert(PGW_DDSConnextMicroConnection.reader(companion, "probe", &probe_reader) == PGW_OK);
    assert(PGW_DDSConnextMicroConnection.writer(gateway, "probe", &probe_writer) == PGW_OK);
    PGW_Representation signal_rep = {&schema, "benchmark.signal", sizeof(SignalSample),
                                     _Alignof(SignalSample), &signal_access, NULL};
    PGW_Representation probe_rep = *PGW_probe_binding.representation;
    probe_rep.access = &probe_access;
    assert(command_writer.iface->bind(command_writer.state, &signal_rep) == PGW_OK);
    assert(probe_writer.iface->bind(probe_writer.state, &probe_rep) == PGW_OK);
    PGW_SampleSeq state_loan, probe_loan, command_sequence, probe_sequence;
    PGW_WriteResultSeq write_results;
    PGW_WriteResult write_result;
    PGW_SampleRef state_refs[8], probe_ref, command_ref, probe_write_ref;
    initialize_sequence(&state_loan, state_refs, 8);
    initialize_sequence(&probe_loan, &probe_ref, 1);
    initialize_sequence(&command_sequence, &command_ref, 1);
    initialize_sequence(&probe_sequence, &probe_write_ref, 1);
    assert(PGW_WriteResultSeq_initialize(&write_results));
    assert(PGW_WriteResultSeq_loan_contiguous(&write_results, &write_result, 0, 1));
#if PGW_DDS_DIAGNOSTICS
    PGW_StreamWriter exporter;
    PGW_StreamReader diagnostic_reader;
    PGW_SampleSeq diagnostic_loan;
    PGW_SampleRef diagnostic_refs[4];
    assert(PGW_DDSConnextMicroConnection.writer(gateway, "diagnostics", &exporter) == PGW_OK);
    assert(exporter.iface->bind(exporter.state, PGW_diagnostics_binding.representation) == PGW_OK);
    assert(PGW_DDSConnextMicroConnection.reader(companion, "diagnostics", &diagnostic_reader) == PGW_OK);
    initialize_sequence(&diagnostic_loan, diagnostic_refs, 4);
#endif
    size_t route_capacities[] = {8, 8, 8, 8};
    PGW_SizeSeq capacity_sequence;
    PGW_CoreResourceReport core_resources;
    assert(PGW_SizeSeq_initialize(&capacity_sequence));
    assert(PGW_SizeSeq_loan_contiguous(&capacity_sequence, route_capacities, 4, 4));
    assert(PGW_core_resource_report(&capacity_sequence, 1, false, 0,
                                    &core_resources) == PGW_OK);
    assert(PGW_SizeSeq_unloan(&capacity_sequence));
    assert(PGW_SizeSeq_finalize(&capacity_sequence));
    uint64_t initialization_ns = now_ns() - initialization_start;
    uint64_t init_libc = PGW_allocation_calls(), init_osapi = PGW_osapi_allocation_calls();
    PGW_allocation_monitor(false);
    size_t ready_bytes = arena.used;
    Histogram local = {.minimum = UINT64_MAX}, roundtrip = {.minimum = UINT64_MAX};
    uint64_t peer_states = 0, peer_probes = 0, peer_diagnostics = 0, exports = 0;
    uint64_t sent_frames = 0, blocked = 0, polls = 0;
    atomic_store(&frozen, true);
    PGW_allocation_monitor(true);
    uint64_t matching_start = now_ns();
    wait_matches(gateway, companion);
    uint64_t matching_ns = now_ns() - matching_start;
    uint64_t discovery_libc = PGW_allocation_calls(), discovery_osapi = PGW_osapi_allocation_calls();
    uint64_t start = now_ns();
#if PGW_DDS_DIAGNOSTICS
    const uint64_t diagnostic_period_ns = UINT64_C(100000000);
    uint64_t next_diagnostic_ns = start + diagnostic_period_ns;
#endif
    for (uint64_t batch = 0; batch < batches; ++batch) {
        uint16_t raw = (uint16_t)(1000 + batch % 1000);
        PGW_CANFrame baseline = {.id = 256, .length = 8,
            .data = {(uint8_t)raw, (uint8_t)(raw >> 8), 0, 0, 1, 1, 0xab, 0xcd}};
        uint64_t target_dispatch = atomic_load_explicit(
            &session.dispatched_routes, memory_order_acquire) + 1;
        uint64_t before = timing ? now_ns() : 0;
        assert(PGW_CANMemory_inject(&memory, &baseline) == PGW_OK);
        assert(wait_session_dispatches(&session, target_dispatch));
        if (timing) observe(&local, now_ns() - before);
        bool observed = false;
        for (unsigned attempt = 0; attempt < 2000 && !observed; ++attempt) {
            PGW_Status status = state_reader.iface->read(state_reader.state, &state_loan, 8);
            ++polls;
            assert(status == PGW_OK || status == PGW_NO_DATA);
            if (status == PGW_OK) {
                RTI_INT32 count = PGW_SampleSeq_get_length(&state_loan);
                assert(count >= 0);
                peer_states += (uint64_t)count;
                for (RTI_INT32 i = 0; i < count; ++i) {
                    PGW_Signal value;
                    assert(state_reader.representation->access->copy_value(
                        *PGW_SampleSeq_get_reference(&state_loan, i), &value, sizeof(value)) == PGW_OK);
                    if (value.id == 1001 && value.value.kind == PGW_VALUE_DOUBLE &&
                        value.value.data.real == raw * 0.1) observed = true;
                }
                assert(state_reader.iface->return_loan(state_reader.state, &state_loan) == PGW_OK);
            }
            if (!observed) OSAPI_Thread_sleep(1);
        }
        assert(observed);
        bool saturate = (batch + 1) % 8 == 0;
        if (saturate) {
            assert(can_config.transport.iface->send(can_config.transport.state, &baseline) == PGW_OK);
            ++blocked;
        }
        SignalSample command = {{1001, {.kind = PGW_VALUE_DOUBLE, .data.real = (raw + 100) * 0.1}}};
        target_dispatch = atomic_load_explicit(&session.dispatched_routes,
                                                memory_order_acquire) + 1;
        write_one(&command_writer, &command_sequence, &command_ref, &command, &write_results);
        assert(wait_session_dispatches(&session, target_dispatch));
        observed = false;
        for (unsigned attempt = 0; attempt < 2000 && !observed; ++attempt) {
            PGW_CANFrame frame;
            ++polls;
            if (saturate) {
                if (counter(&routes[2], PGW_COUNT_BACKPRESSURE) == blocked) {
                    assert(PGW_CANMemory_take_sent(&memory, &frame) == PGW_OK);
                    assert(!memcmp(frame.data, baseline.data, 8));
                    observed = true;
                }
            } else if (PGW_CANMemory_take_sent(&memory, &frame) == PGW_OK) {
                uint16_t expected = (uint16_t)(raw + 100);
                assert(frame.id == 256 && frame.length == 8 &&
                    frame.data[0] == (uint8_t)expected && frame.data[1] == (uint8_t)(expected >> 8));
                assert(!memcmp(frame.data + 2, baseline.data + 2, 6));
                ++sent_frames;
                observed = true;
            }
            if (!observed) OSAPI_Thread_sleep(1);
        }
        assert(observed);
        if (timing) observe(&roundtrip, now_ns() - before);
        ProbeSample probe = {{1, (int32_t)batch}, {true, true, 123, (uint32_t)batch + 1}};
        write_one(&probe_writer, &probe_sequence, &probe_write_ref, &probe, &write_results);
        observed = false;
        for (unsigned attempt = 0; attempt < 2000 && !observed; ++attempt) {
            PGW_Status status = probe_reader.iface->read(probe_reader.state, &probe_loan, 1);
            ++polls;
            assert(status == PGW_OK || status == PGW_NO_DATA);
            if (status == PGW_OK) {
                PGW_ProbeValue value;
                PGW_Timestamp timestamp;
                assert(probe_reader.representation->access->copy_value(
                    *PGW_SampleSeq_get_reference(&probe_loan, 0), &value, sizeof(value)) == PGW_OK);
                assert(value.id == 1 && value.reading == (int32_t)batch);
                assert(probe_reader.representation->access->source_timestamp(
                    *PGW_SampleSeq_get_reference(&probe_loan, 0), &timestamp) == PGW_OK);
                if (preserve) assert(timestamp.valid && timestamp.seconds == 123 &&
                                     timestamp.nanoseconds == (uint32_t)batch + 1);
                ++peer_probes;
                assert(probe_reader.iface->return_loan(probe_reader.state, &probe_loan) == PGW_OK);
                observed = true;
            }
            if (!observed) OSAPI_Thread_sleep(1);
        }
        assert(observed);
#if PGW_DDS_DIAGNOSTICS
        uint64_t diagnostic_now = now_ns();
        if (diagnostic_now >= next_diagnostic_ns) {
            do {
                next_diagnostic_ns += diagnostic_period_ns;
            } while (next_diagnostic_ns <= diagnostic_now);
            for (size_t i = 0; i < 4; ++i) {
                PGW_CounterSnapshot snapshot;
                uint64_t collected_ns;
                assert(PGW_Runtime_monotonic_time_ns(&collected_ns));
                assert(PGW_Counters_snapshot(&routes[i].counters, routes[i].id, batch + 1,
                                            collected_ns, &snapshot));
                PGW_Status status = PGW_DDS_export_snapshot(&exporter, &snapshot);
                if (status == PGW_OK) ++exports;
                else PGW_Counters_add(&routes[i].counters, PGW_COUNT_EXPORT_ERRORS, 1);
            }
            unsigned seen = 0;
            for (unsigned attempt = 0; attempt < 2000 && seen != 15u; ++attempt) {
                PGW_Status status = diagnostic_reader.iface->read(diagnostic_reader.state,
                                                                 &diagnostic_loan, 4);
                ++polls;
                assert(status == PGW_OK || status == PGW_NO_DATA);
                if (status == PGW_OK) {
                    RTI_INT32 count = PGW_SampleSeq_get_length(&diagnostic_loan);
                    assert(count >= 0);
                    peer_diagnostics += (uint64_t)count;
                    for (RTI_INT32 i = 0; i < count; ++i) {
                        PGW_CounterSnapshot value;
                        assert(diagnostic_reader.representation->access->copy_value(
                            *PGW_SampleSeq_get_reference(&diagnostic_loan, i), &value, sizeof(value)) == PGW_OK);
                        assert(value.entity_id >= 1 && value.entity_id <= 4 &&
                               value.sequence == batch + 1);
                        seen |= 1u << (value.entity_id - 1);
                    }
                    assert(diagnostic_reader.iface->return_loan(diagnostic_reader.state,
                                                               &diagnostic_loan) == PGW_OK);
                }
                if (seen != 15u) OSAPI_Thread_sleep(1);
            }
            assert(seen == 15u);
        }
#endif
    }
    uint64_t elapsed = now_ns() - start;
    PGW_CANStats can_stats;
    PGW_DDSStatistics commands, peer_state_status;
    assert(PGW_CAN_stats(can, &can_stats) == PGW_OK);
    assert(PGW_DDS_statistics(companion, "command_powertrain", &commands) == PGW_OK);
    assert(PGW_DDS_statistics(companion, "state_powertrain", &peer_state_status) == PGW_OK);
    assert(can_stats.received_frames == batches && can_stats.decoded_samples == batches * 4);
    assert(can_stats.accepted_commands == batches - blocked && can_stats.backpressure_commands == blocked);
    assert(commands.accepted == batches && sent_frames == batches - blocked && peer_probes == batches);
    assert(counter(&routes[0], PGW_COUNT_RECEIVED) == batches * 4);
    assert(counter(&routes[0], PGW_COUNT_ACCEPTED) == batches * 4);
    for (size_t i = 0; i < 4; ++i)
        assert(!counter(&routes[i], PGW_COUNT_LOANS) && !counter(&routes[i], PGW_COUNT_FATAL));
    assert(PGW_Service_stop(&service) == PGW_OK);
    uint64_t libc_calls = PGW_allocation_calls(), osapi_calls = PGW_osapi_allocation_calls();
    assert(arena.used == ready_bytes && !atomic_load(&runtime_arena_calls));
    PGW_allocation_monitor(false);
    atomic_store(&frozen, false);
    uint64_t local_states = counter(&routes[0], PGW_COUNT_ACCEPTED);
    uint64_t invalid = counter(&routes[2], PGW_COUNT_INVALID);
    assert(PGW_Service_finalize(&service) == PGW_OK);
    assert(PGW_RouteSeq_unloan(&route_sequence));
    assert(PGW_RouteSeq_finalize(&route_sequence));
    for (RTI_INT32 i = 0; i < 4; ++i) {
        finalize_sequence(&route_sample_storage[i]);
        assert(PGW_WriteResultSeq_unloan(&route_result_storage[i]));
        assert(PGW_WriteResultSeq_finalize(&route_result_storage[i]));
    }
    finalize_sequence(&state_loan); finalize_sequence(&probe_loan);
    finalize_sequence(&command_sequence); finalize_sequence(&probe_sequence);
    assert(PGW_WriteResultSeq_unloan(&write_results));
    assert(PGW_WriteResultSeq_finalize(&write_results));
#if PGW_DDS_DIAGNOSTICS
    finalize_sequence(&diagnostic_loan);
#endif
    assert(PGW_CANAdapter.connection->close(can) == PGW_OK);
    assert(PGW_CANConfig_finalize(&can_config) == PGW_OK);
    assert(PGW_CANCategorySeq_unloan(&category_sequence));
    assert(PGW_CANCategorySeq_finalize(&category_sequence));
    assert(PGW_DDSConnextMicroConnection.close(companion) == PGW_OK);
    assert(PGW_DDSConnextMicroConnection.close(gateway) == PGW_OK);
    assert(PGW_DDSEndpointConfigSeq_unloan(&gateway_config.endpoints));
    assert(PGW_DDSEndpointConfigSeq_finalize(&gateway_config.endpoints));
    free(storage);
    struct rusage usage_stats;
    assert(!getrusage(RUSAGE_SELF, &usage_stats));
    printf("{\"format_version\":1,\"workload\":\"actual-micro-mag-can-dds-roundtrip-v1\","
        "\"batches\":%" PRIu64 ",\"samples\":%" PRIu64 ",\"timing\":%s,"
        "\"elapsed_ns\":%" PRIu64 ",\"initialization_ns\":%" PRIu64 ",\"initial_matching_ns\":%" PRIu64 ","
        "\"samples_per_second\":%.3f,\"sample_rate_basis\":\"four decoded states plus one DDS command offered per notification batch; excludes Probe/management\","
        "\"offered_can_frames\":%" PRIu64 ",\"received_can_frames\":%" PRIu64 ",\"decoded_states\":%" PRIu64 ","
        "\"local_accepted_states\":%" PRIu64 ",\"local_dds_accepted_commands\":%" PRIu64 ","
        "\"local_can_accepted_commands\":%" PRIu64 ",\"peer_observed_state_samples\":%" PRIu64 ","
        "\"peer_observed_can_frames\":%" PRIu64 ",\"peer_observed_probe_samples\":%" PRIu64 ","
        "\"management_exports\":%" PRIu64 ",\"peer_observed_management_samples\":%" PRIu64 ","
        "\"backpressure_commands\":%" PRIu64 ",\"invalid_commands\":%" PRIu64 ","
        "\"rx_dropped_signals\":%" PRIu64 ",\"dds_state_backpressure\":%" PRIu64 ",\"outstanding_loans\":0,"
        "\"peer_read_attempts\":%" PRIu64 ",\"backpressure_period_batches\":8,"
        "\"gateway_runtime_allocations\":null,\"gateway_arena_runtime_requests\":0,"
        "\"observed_libc_runtime_allocations\":%" PRIu64 ",\"osapi_runtime_allocations\":%" PRIu64 ","
        "\"observed_libc_initialization_allocations\":%" PRIu64 ",\"observed_osapi_initialization_allocations\":%" PRIu64 ","
        "\"observed_libc_matching_allocations\":%" PRIu64 ",\"observed_osapi_matching_allocations\":%" PRIu64 ","
        "\"allocation_coverage\":\"all eight libc/OSAPI controls plus arena control verified; phase totals include middleware and are not ownership attribution; no libc-internal/kernel interception; initial matching and first business/Probe/management traffic included; no late undeclared peers\","
        "\"fixture_primary_storage_bytes\":%zu,\"arena_reserved_bytes\":%zu,\"arena_bytes_used\":%zu,"
        "\"arena_backing\":\"single fixed initialization malloc; freed after shutdown; no fallback/growth\","
        "\"arena_initialization_bytes\":%zu,"
        "\"can_arena_bound\":%zu,\"can_arena_used\":%zu,\"core_resource_bytes\":%zu,"
        "\"dds_gateway_arena_bytes\":%zu,\"dds_companion_arena_bytes\":%zu,"
        "\"transport_rx_capacity\":8,\"transport_tx_capacity\":1,\"route_sample_capacity\":8,"
        "\"queue_high_water\":%zu,\"schema_fingerprint\":\"%s\","
        "\"getrusage_maxrss_kib\":%ld,\"cpu_user_us\":%ld,\"cpu_system_us\":%ld,"
        "\"configuration\":{\"domain\":%d,\"transport\":\"UDP loopback only\",\"discovery\":\"DPDE\","
        "\"diagnostics\":%s,\"peer_wait_timeout_iterations\":2000,\"peer_wait_sleep_ms\":1,"
        "\"sample_budget\":%u,\"can_receive_budget\":%u,"
        "\"can_write_capacity\":%u,\"diagnostic_period_ms\":%u},"
        "\"metadata_capture\":{\"DDS_public_SampleInfo\":true,\"CAN_context\":true},"
        "\"metadata_preservation\":{\"probe_source_timestamp\":%s,\"source\":\"explicit synthetic portable DDS timestamp 123s plus ordinal nanoseconds; never the monotonic observation clock\"},"
        "\"dds_effective_resources\":{\"factory_participants\":%d,\"factory_components\":%d,"
        "\"gateway_local_readers\":%d,\"gateway_local_writers\":%d,\"gateway_local_topics\":%d,"
        "\"remote_participants\":%d,\"remote_readers\":%d,\"remote_writers\":%d,"
        "\"instances\":%d,\"samples\":%d,\"per_instance\":%d,\"history_depth\":%d,"
        "\"writer_blocking_seconds\":%d,\"writer_blocking_nanoseconds\":%u,"
        "\"memory\":\"entity limits are actual getters; exact middleware bytes not inferred\"},"
        "\"dispatch_latency_ns\":{\"boundary\":\"POSIX CLOCK_MONOTONIC CAN injection to bounded session dispatch, local DDS acceptance, and loan return; not wire latency\","
        "\"count\":%" PRIu64 ",\"min\":%" PRIu64 ",\"max\":%" PRIu64 ",\"mean\":%.3f,"
        "\"p50_upper\":%" PRIu64 ",\"p95_upper\":%" PRIu64 ",\"p99_upper\":%" PRIu64 ","
        "\"histogram\":\"64 power-of-two buckets; upper-bound percentiles\"},"
        "\"roundtrip_latency_ns\":{\"boundary\":\"same-process POSIX CLOCK_MONOTONIC CAN injection through the asynchronous DDS-to-CAN route; blocked batches end at explicit backpressure, not delivery\","
        "\"count\":%" PRIu64 ",\"min\":%" PRIu64 ",\"max\":%" PRIu64 ",\"mean\":%.3f,"
        "\"p50_upper\":%" PRIu64 ",\"p95_upper\":%" PRIu64 ",\"p99_upper\":%" PRIu64 ","
        "\"histogram\":\"64 power-of-two buckets; upper-bound percentiles\"}",
        batches, batches * 5, timing ? "true" : "false", elapsed, initialization_ns, matching_ns,
        elapsed ? batches * 5.0 * 1e9 / elapsed : 0.0, batches, can_stats.received_frames,
        can_stats.decoded_samples, local_states, commands.accepted, can_stats.accepted_commands,
        peer_states, sent_frames, peer_probes, exports, peer_diagnostics, blocked, invalid,
        can_stats.receive_drops, counter(&routes[0], PGW_COUNT_BACKPRESSURE),
        polls, libc_calls, osapi_calls, init_libc, init_osapi,
        discovery_libc, discovery_osapi,
        storage_capacity + sizeof(routes) + sizeof(route_refs) + sizeof(results) +
            sizeof(rx) + sizeof(tx) + sizeof(memory) + sizeof(local) + sizeof(roundtrip),
        storage_capacity, ready_bytes, storage_capacity,
        can_bound, can_stats.mutable_bytes, core_resources.total_bytes,
        gateway_resources.gateway_storage_bytes, companion_resources.gateway_storage_bytes,
        can_stats.queue_high_water, PGW_codec_schema.fingerprint, usage_stats.ru_maxrss,
        usage_stats.ru_utime.tv_sec * 1000000 + usage_stats.ru_utime.tv_usec,
        usage_stats.ru_stime.tv_sec * 1000000 + usage_stats.ru_stime.tv_usec,
        PGW_BENCHMARK_DDS_DOMAIN, PGW_DDS_DIAGNOSTICS ? "true" : "false",
        pgw_config_sample_budget, pgw_config_can_receive_budget,
        pgw_config_can_write_capacity,
        PGW_DDS_DIAGNOSTICS ? 100u : 0u,
        preserve ? "true" : "false", gateway_resources.factory_participants,
        gateway_resources.factory_components, gateway_resources.local_readers,
        gateway_resources.local_writers, gateway_resources.local_topics,
        gateway_resources.remote_participants, gateway_resources.remote_readers,
        gateway_resources.remote_writers, history.instances, history.samples,
        history.samples_per_instance, history.history_depth, history.blocking_seconds,
        history.blocking_nanoseconds, local.count, local.count ? local.minimum : 0,
        local.maximum, local.count ? (double)local.sum / local.count : 0.0,
        percentile(&local, 50), percentile(&local, 95), percentile(&local, 99),
        roundtrip.count, roundtrip.count ? roundtrip.minimum : 0, roundtrip.maximum,
        roundtrip.count ? (double)roundtrip.sum / roundtrip.count : 0.0,
        percentile(&roundtrip, 50), percentile(&roundtrip, 95), percentile(&roundtrip, 99));
    printf(",\"peer_reader\":{\"samples_lost\":%d,\"samples_rejected\":%d,"
           "\"unobserved_state_samples_at_stop\":%" PRIu64 ","
           "\"instances\":%d,\"samples\":%d,\"per_instance\":%d,\"history_depth\":%d},"
           "\"histogram_counts\":{\"local\":",
           peer_state_status.lost, peer_state_status.rejected, local_states - peer_states,
           reader_history.instances, reader_history.samples,
           reader_history.samples_per_instance, reader_history.history_depth);
    print_buckets(&local);
    printf(",\"roundtrip\":");
    print_buckets(&roundtrip);
    printf("}}\n");
    close(domain_lock);
    return 0;
}

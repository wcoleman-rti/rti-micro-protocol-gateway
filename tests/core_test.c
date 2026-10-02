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
#include "support/fake.h"
#include "support/allocation.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "osapi/osapi_heap.h"

static void routing(void)
{
    PGW_TestReader readers[2] = {
        {.values = {{10, 1}, {20, 2}}, .available = 2},
        {.values = {{7, 3}}, .available = 1}
    };
    PGW_TestWriter writers[2] = {{.outcome = PGW_WRITE_BACKPRESSURE}, {0}};
    PGW_SampleRef refs[2][4];
    PGW_WriteResult results[2][4];
    PGW_Route routes[2];
    for (size_t i = 0; i < 2; ++i)
        PGW_test_route(&routes[i], (uint32_t)i + 1, &readers[i], &writers[i], refs[i], results[i], 4);
    PGW_Event events[2];
    PGW_Diagnostics diagnostics;
    assert(PGW_test_diagnostics_initialize(&diagnostics, events, 2));
    PGW_Service service = {.route_budget = 1,
                            .sample_budget = 2, .diagnostics = &diagnostics};
    assert(PGW_test_service_set_routes(&service, routes, 2) == PGW_OK);
    assert(PGW_Service_initialize(&service) == PGW_OK);
    PGW_allocation_monitor(true);
    for (size_t i = 0; i < 10000; ++i) assert(PGW_Service_step(&service) == PGW_OK);
    assert(readers[0].borrows == 5000 && readers[0].returns == 5000);
    assert(readers[1].borrows == 5000 && readers[1].returns == 5000);
    assert(writers[1].sum == 35000);
    PGW_CounterSnapshot snapshot;
    PGW_Counters_snapshot(&routes[0].counters, 1, 1, 0, &snapshot);
    assert(snapshot.values[PGW_COUNT_BACKPRESSURE] == 10000);
    assert(snapshot.values[PGW_COUNT_LOANS] == 0);
    assert(atomic_load(&diagnostics.overflow) == 4998);
    PGW_Event drained[2];
    PGW_EventSeq drained_seq;
    assert(PGW_EventSeq_initialize(&drained_seq));
    assert(PGW_EventSeq_loan_contiguous(&drained_seq, drained, 0, 2));
    size_t drained_count;
    assert(PGW_Diagnostics_drain(&diagnostics, &drained_seq));
    drained_count = PGW_EventSeq_get_length(&drained_seq);
    assert(drained_count == 2 && drained[0].code == PGW_BACKPRESSURE);
    char output[512];
    size_t bytes;
    assert(PGW_snapshot_json(&snapshot, output, sizeof(output), &bytes));
    assert(bytes == strlen(output) && output[bytes - 1] == '\n');
    char tiny[2];
    assert(!PGW_snapshot_json(&snapshot, tiny, sizeof(tiny), &bytes));
    assert(tiny[1] == 0);
    readers[0].available = 0;
    readers[0].empty_ok = true;
    assert(PGW_Service_step(&service) == PGW_OK);
    assert(readers[0].borrows == 5001 && readers[0].returns == 5001);
    readers[1].available = 0;
    assert(PGW_Service_step(&service) == PGW_OK);
    assert(readers[1].borrows == 5000);
    readers[0].available = 2;
    writers[0].outcome = PGW_WRITE_INVALID;
    assert(PGW_Service_step(&service) == PGW_OK);
    PGW_Counters_snapshot(&routes[0].counters, 1, 2, 0, &snapshot);
    assert(snapshot.values[PGW_COUNT_INVALID] == 2);
    assert(PGW_Diagnostics_drain(&diagnostics, &drained_seq));
    drained_count = PGW_EventSeq_get_length(&drained_seq);
    assert(drained_count == 1 && drained[0].code == PGW_INVALID &&
           drained[0].count == 2 && drained[0].severity == 2);
    assert(PGW_Service_step(&service) == PGW_OK);
    writers[0].partial = true;
    writers[0].outcome = PGW_WRITE_BACKPRESSURE;
    assert(PGW_Service_step(&service) == PGW_OK);
    assert(writers[0].sum == 10);
    assert(PGW_Service_step(&service) == PGW_OK);
    writers[0].status = PGW_FATAL;
    assert(PGW_Service_step(&service) == PGW_FATAL);
    assert(routes[0].lifecycle == PGW_FAULTED);
    PGW_Counters_snapshot(&routes[0].counters, 1, 3, 0, &snapshot);
    assert(snapshot.values[PGW_COUNT_ROUTE_FAULTS] == 1);
    assert(readers[0].borrows == readers[0].returns);
    readers[1].available = 1;
    assert(PGW_Service_step(&service) == PGW_OK);
    assert(writers[1].sum == 35007);
    assert(PGW_allocation_calls() == 0 && PGW_osapi_allocation_calls() == 0);
    PGW_allocation_monitor(false);
    assert(PGW_Service_stop(&service) == PGW_OK);
    assert(PGW_Service_finalize(&service) == PGW_OK);
    assert(PGW_EventSeq_unloan(&drained_seq));
    assert(PGW_EventSeq_finalize(&drained_seq));
    assert(PGW_Diagnostics_finalize(&diagnostics));
}

typedef struct { double temperature; uint32_t sensor; } Temperature;
typedef struct { Temperature value; bool loaned; size_t returns; } TemperatureReader;
typedef struct { double received; } TemperatureWriter;

static PGW_Status temperature_copy(const PGW_Sample *sample, void *out, size_t bytes)
{
    if (!sample || !out || bytes != sizeof(Temperature)) return PGW_INVALID;
    memcpy(out, sample, bytes);
    return PGW_OK;
}

static PGW_Status temperature_read(void *state, PGW_SampleSeq *seq, size_t budget)
{
    TemperatureReader *r = state;
    if (!budget || r->loaned) return PGW_LOAN_ERROR;
    assert(PGW_SampleSeq_set_length(seq, 1));
    *PGW_SampleSeq_get_reference(seq, 0) = (const PGW_Sample *)&r->value;
    r->loaned = true;
    return PGW_OK;
}

static PGW_Status temperature_return(void *state, PGW_SampleSeq *seq)
{
    TemperatureReader *r = state;
    if (!r->loaned) return PGW_LOAN_ERROR;
    ++r->returns;
    r->loaned = false;
    return PGW_SampleSeq_set_length(seq, 0) ? PGW_OK : PGW_LOAN_ERROR;
}

static PGW_Status temperature_bind(void *state, const PGW_Representation *rep)
{
    (void)state;
    return rep->access && rep->access->copy_value == temperature_copy ? PGW_OK : PGW_UNSUPPORTED;
}

static PGW_Status temperature_write(void *state, const PGW_SampleSeq *seq,
                                    PGW_WriteResultSeq *out)
{
    TemperatureWriter *w = state;
    if (PGW_SampleSeq_get_length(seq) != 1 || PGW_WriteResultSeq_get_length(out) != 1)
        return PGW_INVALID;
    Temperature value;
    if (temperature_copy(*PGW_SampleSeq_get_reference(seq, 0), &value, sizeof(value)) != PGW_OK) return PGW_INVALID;
    w->received = value.temperature;
    *PGW_WriteResultSeq_get_reference(out, 0) = PGW_WRITE_ACCEPTED;
    return PGW_OK;
}

static void second_schema_and_loan_errors(void)
{
    const PGW_Schema schema = {"test.temperature", 1, "f64-key-v1"};
    const PGW_SampleAccessI ops = {PGW_ABI_VERSION, sizeof(ops), temperature_copy, NULL};
    const PGW_Representation rep = {&schema, "temperature", sizeof(Temperature), _Alignof(Temperature), &ops};
    const PGW_StreamReaderI reader_ops = {PGW_ABI_VERSION, sizeof(reader_ops), temperature_read, temperature_return};
    const PGW_StreamWriterI writer_ops = {PGW_ABI_VERSION, sizeof(writer_ops), temperature_bind, temperature_write};
    TemperatureReader reader = {.value = {23.75, 42}};
    TemperatureWriter writer = {0};
    PGW_SampleRef refs[1];
    PGW_WriteResult results[1];
    PGW_Route route = {.id = 9, .reader = {&reader, &reader_ops, &rep},
        .writer = {&writer, &writer_ops, &rep}};
    assert(PGW_test_route_initialize_storage(&route, refs, results, 1) == PGW_OK);
    PGW_Service s = {.route_budget = 1, .sample_budget = 1};
    assert(PGW_test_service_set_routes(&s, &route, 1) == PGW_OK);
    assert(PGW_Service_initialize(&s) == PGW_OK);
    assert(PGW_Service_step(&s) == PGW_OK);
    assert(writer.received == 23.75 && reader.returns == 1);
    assert(PGW_Service_stop(&s) == PGW_OK);
    assert(PGW_Service_finalize(&s) == PGW_OK);
    PGW_TestReader bad_reader = {.values = {{1, 1}}, .available = 1, .return_status = PGW_LOAN_ERROR};
    PGW_TestWriter sink = {0};
    PGW_test_route(&route, 1, &bad_reader, &sink, refs, results, 1);
    s.lifecycle = PGW_UNINITIALIZED;
    assert(PGW_test_service_set_routes(&s, &route, 1) == PGW_OK);
    assert(PGW_Service_initialize(&s) == PGW_OK);
    assert(PGW_Service_step(&s) == PGW_LOAN_ERROR);
    assert(bad_reader.returns == 1 && route.lifecycle == PGW_FAULTED);
    assert(!strcmp(route.error.operation, "return_loan"));
    assert(PGW_Service_stop(&s) == PGW_OK);
    assert(PGW_Service_finalize(&s) == PGW_OK);
    bad_reader = (PGW_TestReader){.read_status = PGW_IO_ERROR};
    PGW_test_route(&route, 1, &bad_reader, &sink, refs, results, 1);
    assert(PGW_test_service_set_routes(&s, &route, 1) == PGW_OK);
    assert(PGW_Service_initialize(&s) == PGW_OK);
    assert(PGW_Service_step(&s) == PGW_IO_ERROR);
    assert(bad_reader.returns == 0);
    assert(PGW_Service_stop(&s) == PGW_OK);
    assert(PGW_Service_finalize(&s) == PGW_OK);
}

static bool unavailable_clock(void *context, uint64_t *nanoseconds)
{
    (void)context;
    (void)nanoseconds;
    return false;
}

static void clock_failure_is_not_zero_timestamp(void)
{
    PGW_TestReader input = {.values = {{5, 9}}, .available = 1};
    PGW_TestWriter output = {.outcome = PGW_WRITE_BACKPRESSURE};
    PGW_SampleRef references[1];
    PGW_WriteResult results[1];
    PGW_Route routes[1];
    PGW_test_route(&routes[0], 17, &input, &output, references, results, 1);
    PGW_RouteSeq route_storage;
    assert(PGW_RouteSeq_initialize(&route_storage));
    assert(PGW_RouteSeq_loan_contiguous(&route_storage, routes, 1, 1));
    PGW_Event event_storage[2];
    PGW_Diagnostics diagnostics;
    assert(PGW_test_diagnostics_initialize(&diagnostics, event_storage, 2));
    PGW_Service service = {.route_budget = 1, .sample_budget = 1,
                           .diagnostics = &diagnostics,
                           .clock_ns = unavailable_clock};
    assert(PGW_Service_set_routes(&service, &route_storage) == PGW_OK);
    assert(PGW_Service_initialize(&service) == PGW_OK);
    assert(PGW_Service_step(&service) == PGW_IO_ERROR);
    assert(input.borrows == 1 && input.returns == 1);
    PGW_CounterSnapshot snapshot;
    assert(PGW_Counters_snapshot(&routes[0].counters, 17, 1, 0, &snapshot));
    assert(snapshot.values[PGW_COUNT_BACKPRESSURE] == 1);
    PGW_EventSeq drained;
    PGW_Event output_storage[2];
    assert(PGW_EventSeq_initialize(&drained));
    assert(PGW_EventSeq_loan_contiguous(&drained, output_storage, 0, 2));
    assert(PGW_Diagnostics_drain(&diagnostics, &drained));
    assert(PGW_EventSeq_get_length(&drained) == 0);
    assert(PGW_Service_stop(&service) == PGW_OK);
    assert(PGW_Service_finalize(&service) == PGW_OK);
    assert(PGW_RouteSeq_unloan(&route_storage));
    assert(PGW_RouteSeq_finalize(&route_storage));
    assert(PGW_EventSeq_unloan(&drained));
    assert(PGW_EventSeq_finalize(&drained));
    assert(PGW_Diagnostics_finalize(&diagnostics));
}

static PGW_Status registry_create(const void *config, PGW_Arena *arena, PGW_Connection **out)
{
    (void)config; (void)arena; (void)out;
    return PGW_UNSUPPORTED;
}

static PGW_Status registry_reader(PGW_Connection *connection, const char *name, PGW_StreamReader *out)
{
    (void)connection; (void)name; (void)out;
    return PGW_UNSUPPORTED;
}

static PGW_Status registry_close(PGW_Connection *connection)
{
    (void)connection;
    return PGW_UNSUPPORTED;
}

static void bounds_and_schema(void)
{
    size_t size;
    assert(!PGW_size_add(SIZE_MAX, 1, &size));
    assert(!PGW_size_multiply(SIZE_MAX, 2, &size));
    size_t capacities[2] = {2, 4};
    PGW_SizeSeq capacity_seq;
    assert(PGW_SizeSeq_initialize(&capacity_seq));
    assert(PGW_SizeSeq_loan_contiguous(&capacity_seq, capacities, 2, 2));
    PGW_CoreResourceReport report;
    assert(PGW_core_resource_report(&capacity_seq, true, 8, &report) == PGW_OK);
    assert(report.sample_references_bytes == 6 * sizeof(PGW_SampleRef));
    assert(report.diagnostics_bytes == sizeof(PGW_Diagnostics) + 8 * sizeof(PGW_Event));
    assert(report.total_bytes == report.objects_bytes + report.sample_references_bytes +
                                report.write_results_bytes + report.diagnostics_bytes);
    assert(PGW_core_resource_report(&capacity_seq, true, SIZE_MAX, &report) == PGW_CAPACITY);
    assert(PGW_SizeSeq_unloan(&capacity_seq));
    assert(PGW_SizeSeq_finalize(&capacity_seq));
    unsigned char storage[65];
    PGW_Arena arena = {storage, sizeof(storage), 0};
    void *p;
    assert(PGW_Arena_allocate(&arena, 16, 16, &p) == PGW_OK);
    assert((uintptr_t)p % 16 == 0);
    assert(PGW_Arena_allocate(&arena, 100, 8, &p) == PGW_CAPACITY);
    assert(PGW_Arena_allocate(&arena, 1, 3, &p) == PGW_INVALID);
    const PGW_Representation *slots[2];
    PGW_AdapterRef adapter_slots[1];
    PGW_Registry registry = {0};
    assert(PGW_test_registry_initialize(&registry, adapter_slots, 1, slots, 2) == PGW_OK);
    const PGW_ConnectionI connection = {
        PGW_ABI_VERSION, sizeof(connection), registry_reader, NULL, registry_close
    };
    const PGW_AdapterI adapter = {
        PGW_ABI_VERSION, sizeof(adapter), "fixture", registry_create, &connection
    };
    PGW_AdapterI other_adapter = adapter;
    other_adapter.name = "other";
    assert(PGW_Registry_register_adapter(&registry, &adapter) == PGW_OK);
    assert(PGW_AdapterSeq_get_length(&registry.adapters) == 1);
    assert(PGW_AdapterSeq_get_maximum(&registry.adapters) == 1);
    assert(*PGW_AdapterSeq_get_reference(&registry.adapters, 0) == &adapter);
    assert(PGW_Registry_find_adapter(&registry, "fixture") == &adapter);
    assert(PGW_Registry_register_adapter(&registry, &adapter) == PGW_INVALID);
    assert(PGW_Registry_register_adapter(&registry, &other_adapter) == PGW_CAPACITY);
    assert(PGW_Registry_register_binding(&registry, &PGW_test_representation) == PGW_OK);
    assert(PGW_Registry_register_binding(&registry, &PGW_test_representation) == PGW_INVALID);
    PGW_Schema other = {"test.temperature", 1, "f64-key-v1"};
    PGW_Representation second = PGW_test_representation;
    second.name = "test.temperature.native";
    second.schema = &other;
    assert(PGW_Registry_register_binding(&registry, &second) == PGW_OK);
    assert(!PGW_Registry_find_binding(&registry, "missing"));
    PGW_Schema empty_fingerprint = {"test.counter", 1, ""};
    assert(!PGW_schema_equal(&empty_fingerprint, &empty_fingerprint));
    registry.frozen = true;
    assert(PGW_Registry_register_adapter(&registry, &other_adapter) == PGW_INVALID);
    assert(PGW_Registry_register_binding(&registry, &second) == PGW_INVALID);
    assert(PGW_RepresentationSeq_get_length(&registry.bindings) == 2);
    assert(PGW_RepresentationSeq_get_maximum(&registry.bindings) == 2);
    assert(PGW_Registry_finalize(&registry) == PGW_OK);
    PGW_TestReader r = {0};
    PGW_TestWriter w = {0};
    PGW_Route route;
    PGW_SampleRef refs[4];
    PGW_WriteResult results[4];
    PGW_test_route(&route, 1, &r, &w, refs, results, 4);
    route.writer.representation = &second;
    PGW_Service s = {.route_budget = 1, .sample_budget = 4};
    assert(PGW_test_service_set_routes(&s, &route, 1) == PGW_OK);
    assert(PGW_Service_initialize(&s) == PGW_INVALID);
    assert(s.lifecycle == PGW_FAULTED);
    other = (PGW_Schema){"test.counter", 1, "different-layout-same-name"};
    PGW_test_route(&route, 1, &r, &w, refs, results, 4);
    route.writer.representation = &second;
    s.lifecycle = PGW_UNINITIALIZED;
    assert(PGW_test_service_set_routes(&s, &route, 1) == PGW_OK);
    assert(PGW_Service_initialize(&s) == PGW_INVALID);
    assert(!strcmp(route.error.operation, "validate"));
    PGW_SampleSeq seq;
    assert(PGW_SampleSeq_initialize(&seq));
    assert(PGW_SampleSeq_loan_contiguous(&seq, refs, 0, 4));
    assert(!PGW_SampleSeq_set_length(&seq, 5));
    assert(!PGW_SampleSeq_finalize(&seq));
    assert(PGW_SampleSeq_unloan(&seq));
    assert(!PGW_SampleSeq_unloan(&seq));
    assert(PGW_SampleSeq_finalize(&seq));
}

static PGW_Counters concurrent_counters;
static PGW_Diagnostics concurrent_events;
static atomic_bool producers_done;
static size_t observed_events;
static void *producer(void *unused)
{
    (void)unused;
    for (size_t i = 0; i < 10000; ++i) {
        PGW_Counters_add(&concurrent_counters, PGW_COUNT_RECEIVED, 1);
        PGW_Event e = {.time_ns = i, .time_valid = true,
                       .entity_id = 1, .code = 1, .severity = 2, .count = 1};
        (void)PGW_Diagnostics_emit(&concurrent_events, &e);
    }
    return NULL;
}

static void *observer(void *unused)
{
    (void)unused;
    uint64_t previous = 0;
    PGW_Event events[8];
    PGW_EventSeq output;
    assert(PGW_EventSeq_initialize(&output));
    assert(PGW_EventSeq_loan_contiguous(&output, events, 0, 8));
    do {
        PGW_CounterSnapshot snapshot;
        PGW_Counters_snapshot(&concurrent_counters, 1, 0, 0, &snapshot);
        assert(snapshot.values[PGW_COUNT_RECEIVED] >= previous);
        previous = snapshot.values[PGW_COUNT_RECEIVED];
        if (PGW_Diagnostics_drain(&concurrent_events, &output)) {
            size_t count = PGW_EventSeq_get_length(&output);
            for (size_t i = 0; i < count; ++i)
                assert(events[i].time_valid && events[i].entity_id == 1 && events[i].code == 1 &&
                       events[i].count == 1 && events[i].time_ns < 10000);
            observed_events += count;
        }
    } while (!atomic_load(&producers_done));
    assert(PGW_EventSeq_unloan(&output));
    assert(PGW_EventSeq_finalize(&output));
    return NULL;
}

static void concurrent_capture(void)
{
    PGW_Event storage[8];
    assert(PGW_Counters_initialize(&concurrent_counters));
    assert(!PGW_Counters_add(&concurrent_counters, (PGW_CounterId)-1, 1));
    assert(PGW_test_diagnostics_initialize(&concurrent_events, storage, 8));
    atomic_init(&producers_done, false);
    pthread_t threads[4], observer_thread;
    assert(!pthread_create(&observer_thread, NULL, observer, NULL));
    for (size_t i = 0; i < 4; ++i) assert(!pthread_create(&threads[i], NULL, producer, NULL));
    for (size_t i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
    atomic_store(&producers_done, true);
    assert(!pthread_join(observer_thread, NULL));
    PGW_CounterSnapshot s;
    PGW_Counters_snapshot(&concurrent_counters, 1, 0, 0, &s);
    assert(s.values[PGW_COUNT_RECEIVED] == 40000);
    size_t count;
    PGW_Event out[8];
    PGW_EventSeq output;
    assert(PGW_EventSeq_initialize(&output));
    assert(PGW_EventSeq_loan_contiguous(&output, out, 0, 8));
    assert(PGW_Diagnostics_drain(&concurrent_events, &output));
    count = PGW_EventSeq_get_length(&output);
    assert(count + observed_events + atomic_load(&concurrent_events.overflow) +
           atomic_load(&concurrent_events.contention) == 40000);
    PGW_Counters_add(&concurrent_counters, PGW_COUNT_RECEIVED, UINT64_MAX - 39999);
    PGW_Counters_snapshot(&concurrent_counters, 1, 0, 0, &s);
    assert(s.values[PGW_COUNT_RECEIVED] == 0);
    assert(PGW_Diagnostics_finalize(&concurrent_events));
    assert(PGW_test_diagnostics_initialize(&concurrent_events, storage, 8));
    assert(PGW_Diagnostics_configure_rate(&concurrent_events, 10, 2));
    PGW_Event event = {.entity_id = 1, .code = 1, .severity = 2, .count = 1};
    assert(PGW_Diagnostics_emit(&concurrent_events, &event));
    event.time_ns = 1;
    assert(PGW_Diagnostics_emit(&concurrent_events, &event));
    event.time_ns = 2;
    assert(!PGW_Diagnostics_emit(&concurrent_events, &event));
    event.time_ns = 10;
    assert(PGW_Diagnostics_emit(&concurrent_events, &event));
    assert(atomic_load(&concurrent_events.rate_limited) == 1);
    PGW_DiagnosticSnapshot diagnostic_snapshot;
    assert(PGW_Diagnostics_snapshot(&concurrent_events, 99, &diagnostic_snapshot));
    assert(diagnostic_snapshot.version == 1 && diagnostic_snapshot.collected_ns == 99);
    assert(diagnostic_snapshot.rate_limited == 1 && diagnostic_snapshot.event_capacity == 8);
    assert(PGW_EventSeq_unloan(&output));
    assert(PGW_EventSeq_finalize(&output));
    assert(PGW_Diagnostics_finalize(&concurrent_events));
}

int main(void)
{
    PGW_allocation_monitor(true);
    void *volatile p = malloc(32);
    assert(p);
    free(p);
    p = calloc(2, 16);
    assert(p);
    p = realloc(p, 64);
    assert(p);
    free(p);
    p = aligned_alloc(16, 32);
    assert(p);
    free(p);
    void *aligned;
    assert(!posix_memalign(&aligned, 16, 32));
    free(aligned);
    void *q = OSAPI_Heap_allocate(1, 32);
    assert(q);
    OSAPI_Heap_free(q);
    uint64_t osapi_calls = PGW_osapi_allocation_calls();
    assert(osapi_calls >= 1);
    q = OSAPI_Heap_realloc(NULL, 32);
    assert(q);
    OSAPI_Heap_free(q);
    assert(PGW_osapi_allocation_calls() > osapi_calls);
    osapi_calls = PGW_osapi_allocation_calls();
    char *buffer = NULL;
    OSAPI_Heap_allocate_buffer(&buffer, 32, OSAPI_ALIGNMENT_DEFAULT);
    assert(buffer);
    OSAPI_Heap_free_buffer(buffer);
    assert(PGW_allocation_calls() >= 5);
    assert(PGW_osapi_allocation_calls() > osapi_calls);
    PGW_allocation_monitor(false);
    bounds_and_schema();
    routing();
    second_schema_and_loan_errors();
    clock_failure_is_not_zero_timestamp();
    concurrent_capture();
    return 0;
}

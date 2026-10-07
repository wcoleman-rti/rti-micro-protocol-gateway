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

#include "pgw/atomic.h"
#include "fake.h"
#include "osapi/osapi_thread.h"
#include <string.h>
#include <stdlib.h>

#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
static PGW_ATOMIC(RTI_UINT64) test_clock_value;

static bool test_clock(void *state, uint64_t *nanoseconds)
{
    (void)state;
    if (!nanoseconds) return false;
    *nanoseconds = PGW_ATOMIC_ADD(&test_clock_value, 1000,
                                             OSAPI_ATOMIC_MEMORY_ORDER_RELAXED) + 1000;
    return true;
}
#endif

static PGW_Status copy_value(const PGW_Sample *sample, void *out, size_t bytes)
{
    if (!sample || !out || bytes != sizeof(PGW_TestValue)) return PGW_INVALID;
    memcpy(out, sample, bytes);
    return PGW_OK;
}

static const PGW_TypeInfo schema = {"test.counter", 1, "integer-key-u64-v1"};
static const PGW_SampleAccessI sample_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), copy_value, NULL, NULL
};
const PGW_SampleRepresentation PGW_test_representation = {
    &schema, "test.counter.native", sizeof(PGW_TestValue),
    _Alignof(PGW_TestValue), &sample_access, NULL
};

static PGW_Status read_samples(void *state, PGW_SampleSeq *seq, size_t budget)
{
    PGW_TestReader *r = state;
    PGW_ATOMIC_ADD(&r->read_calls, 1,
                   OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE);
    if (r->loaned) return PGW_LOAN_ERROR;
    if (r->read_status != PGW_OK) return r->read_status;
    if (!r->available && !r->empty_ok) return PGW_NO_DATA;
    size_t count = r->available < budget ? r->available : budget;
    if (count > 8 || !PGW_SampleSeq_set_length(seq, count)) return PGW_CAPACITY;
    for (size_t i = 0; i < count; ++i)
        *PGW_SampleSeq_get_reference(seq, i) = (const PGW_Sample *)&r->values[i];
    r->loaned = true;
    ++r->borrows;
    r->available -= count;
    if (r->available && r->listener) {
        PGW_ATOMIC_ADD(&r->notifications, 1, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
        r->listener->on_data_available(r->listener->context);
    }
    return PGW_OK;
}

static PGW_Status return_samples(void *state, PGW_SampleSeq *seq)
{
    PGW_TestReader *r = state;
    if (!r->loaned) return PGW_LOAN_ERROR;
    r->loaned = false;
    ++r->returns;
    if (!PGW_SampleSeq_set_length(seq, 0)) return PGW_LOAN_ERROR;
    return r->return_status;
}

static PGW_Status register_listener(void *state,
                                    const PGW_ReaderListener *listener)
{
    PGW_TestReader *reader = state;
    if (!reader || !listener || !listener->on_data_available ||
        (reader->listener && reader->listener != listener))
        return PGW_INVALID;
    size_t notifications = reader->available ? reader->available :
        (reader->empty_ok || reader->read_status != PGW_OK ? 1 : 0);
    reader->listener = listener;
    for (size_t i = 0; i < notifications; ++i) {
        PGW_ATOMIC_ADD(&reader->notifications, 1,
                                  OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
        listener->on_data_available(listener->context);
    }
    return PGW_OK;
}

static PGW_Status unregister_listener(void *state,
                                      const PGW_ReaderListener *listener)
{
    PGW_TestReader *reader = state;
    if (!reader || reader->listener != listener) return PGW_INVALID;
    reader->listener = NULL;
    return PGW_OK;
}

static PGW_Status bind(void *state, const PGW_SampleRepresentation *representation)
{
    PGW_TestWriter *w = state;
    if (!representation || !w->target_schema ||
        !PGW_type_info_equal(representation->schema, w->target_schema) ||
        !representation->access || !representation->access->copy_value)
        return PGW_UNSUPPORTED;
    w->source = representation;
    return PGW_OK;
}

static PGW_Status write_samples(void *state, const PGW_SampleSeq *seq,
                                PGW_WriteResultSeq *results)
{
    PGW_TestWriter *w = state;
    size_t count = PGW_SampleSeq_get_length(seq);
    if (count != (size_t)PGW_WriteResultSeq_get_length(results)) return PGW_INVALID;
    if (w->order_clock && w->writes < sizeof(w->order) / sizeof(w->order[0]))
        w->order[w->writes] = PGW_ATOMIC_ADD(
            w->order_clock, 1, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED) + 1;
    for (size_t i = 0; i < count; ++i) {
        PGW_TestValue value;
        PGW_WriteResult *result = PGW_WriteResultSeq_get_reference(results, i);
        if (w->source->access->copy_value(*PGW_SampleSeq_get_reference(seq, (RTI_INT32)i),
                                          &value, sizeof(value)) != PGW_OK)
            *result = PGW_WRITE_INVALID;
        else {
            *result = w->partial && i == 0 ? PGW_WRITE_ACCEPTED : w->outcome;
            if (*result == PGW_WRITE_ACCEPTED) w->sum += value.value;
        }
    }
    ++w->writes;
    return w->status;
}

const PGW_StreamReaderI PGW_test_reader_iface = {
    .version = PGW_ABI_VERSION,
    .size = sizeof(PGW_StreamReaderI),
    .read = read_samples,
    .return_loan = return_samples,
    .register_listener = register_listener,
    .unregister_listener = unregister_listener
};
const PGW_StreamWriterI PGW_test_writer_iface = {
    PGW_ABI_VERSION, sizeof(PGW_StreamWriterI), bind, write_samples
};

void PGW_test_route(PGW_Route *route, uint32_t id, PGW_TestReader *reader,
                    PGW_TestWriter *writer, PGW_SampleRef *refs,
                    PGW_WriteResult *results, size_t capacity)
{
    PGW_ATOMIC_INIT(&reader->read_calls, 0);
    PGW_ATOMIC_INIT(&reader->notifications, 0);
    *route = (PGW_Route){
        .id = id,
        .reader = {reader, &PGW_test_reader_iface, &PGW_test_representation},
        .writer = {writer, &PGW_test_writer_iface, &PGW_test_representation}
    };
    writer->target_schema = PGW_test_representation.schema;
    if (PGW_test_route_initialize_storage(route, refs, results, capacity) != PGW_OK) abort();
}

PGW_Status PGW_test_route_initialize_storage(PGW_Route *route, PGW_SampleRef *refs,
                                             PGW_WriteResult *results, size_t capacity)
{
    PGW_SampleSeq sample_storage;
    PGW_WriteResultSeq result_storage;
    if (!route || !refs || !results || !capacity || capacity > INT32_MAX ||
        !PGW_SampleSeq_initialize(&sample_storage) ||
        !PGW_WriteResultSeq_initialize(&result_storage)) return PGW_INVALID;
    if (!PGW_SampleSeq_loan_contiguous(&sample_storage, refs, 0, (RTI_INT32)capacity) ||
        !PGW_WriteResultSeq_loan_contiguous(&result_storage, results, 0, (RTI_INT32)capacity)) {
        (void)PGW_SampleSeq_unloan(&sample_storage);
        (void)PGW_WriteResultSeq_unloan(&result_storage);
        (void)PGW_SampleSeq_finalize(&sample_storage);
        (void)PGW_WriteResultSeq_finalize(&result_storage);
        return PGW_CAPACITY;
    }
    PGW_Status status = PGW_Route_initialize_storage(route, &sample_storage, &result_storage);
    (void)PGW_SampleSeq_unloan(&sample_storage);
    (void)PGW_WriteResultSeq_unloan(&result_storage);
    (void)PGW_SampleSeq_finalize(&sample_storage);
    (void)PGW_WriteResultSeq_finalize(&result_storage);
    return status;
}

PGW_Status PGW_test_session_set_routes(PGW_Session *session,
                                       PGW_Route *routes, size_t count)
{
    PGW_RouteSeq sequence;
    if (!session || !routes || !count || count > INT32_MAX ||
        !PGW_RouteSeq_initialize(&sequence))
        return PGW_INVALID;
    if (!PGW_RouteSeq_loan_contiguous(&sequence, routes, (RTI_INT32)count,
                                      (RTI_INT32)count)) {
        (void)PGW_RouteSeq_finalize(&sequence);
        return PGW_CAPACITY;
    }
    session->name = session->name ? session->name : "test-session";
    PGW_Status status = PGW_Session_set_routes(session, &sequence);
    if (!PGW_RouteSeq_unloan(&sequence) || !PGW_RouteSeq_finalize(&sequence))
        return PGW_LOAN_ERROR;
    if (status != PGW_OK) return status;
    return PGW_OK;
}

PGW_Status PGW_test_service_set_sessions(PGW_Service *service,
                                         PGW_Session *sessions,
                                         size_t count)
{
    PGW_SessionSeq sequence;
    if (!service || !sessions || !count || count > INT32_MAX ||
        !PGW_SessionSeq_initialize(&sequence))
        return PGW_INVALID;
    if (!PGW_SessionSeq_loan_contiguous(&sequence, sessions,
            (RTI_INT32)count, (RTI_INT32)count)) {
        (void)PGW_SessionSeq_finalize(&sequence);
        return PGW_CAPACITY;
    }
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    if (!service->clock_ns) service->clock_ns = test_clock;
#endif
    PGW_Status status = PGW_Service_set_sessions(service, &sequence);
    if (!PGW_SessionSeq_unloan(&sequence) ||
        !PGW_SessionSeq_finalize(&sequence)) return PGW_LOAN_ERROR;
    return status;
}

PGW_Status PGW_test_service_set_routes(PGW_Service *service,
                                       PGW_Session *session,
                                       PGW_Route *routes, size_t count)
{
    PGW_Status status = PGW_test_session_set_routes(session, routes, count);
    if (status != PGW_OK) return status;
    return PGW_test_service_set_sessions(service, session, 1);
}

void PGW_test_wait_dispatches(PGW_Session *session, uint64_t expected)
{
    for (size_t i = 0; i < 2000; ++i) {
        if (PGW_ATOMIC_LOAD(&session->dispatched_routes,
                                 OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) >= expected) return;
        OSAPI_Thread_sleep(1);
    }
    abort();
}

void PGW_test_wait_wakeups(PGW_Session *session, uint64_t expected)
{
    for (size_t i = 0; i < 2000; ++i) {
        if (PGW_ATOMIC_LOAD(&session->wakeups,
                                 OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) >= expected) return;
        OSAPI_Thread_sleep(1);
    }
    abort();
}

PGW_Status PGW_test_notify_routes(PGW_Service *service)
{
    PGW_Status status = PGW_OK;
    for (RTI_INT32 i = 0; i < PGW_SessionSeq_get_length(&service->sessions); ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        RTI_INT32 count = PGW_RouteSeq_get_length(&session->routes);
        bool was_faulted[(size_t)count];
        size_t active_count = 0;
        for (RTI_INT32 j = 0; j < count; ++j)
        {
            PGW_Route *route =
                PGW_RouteSeq_get_reference(&session->routes, j);
            PGW_EntityState state = (PGW_EntityState)PGW_ATOMIC_LOAD(
                &route->lifecycle, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
            was_faulted[j] = state == PGW_FAULTED;
            if (state != PGW_FAULTED && state != PGW_PAUSED)
                ++active_count;
        }
        PGW_ATOMIC_STORE(&session->error, PGW_OK, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
        uint64_t target = PGW_ATOMIC_LOAD(&session->dispatched_routes,
                                               OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) +
                          active_count;
        for (RTI_INT32 j = 0; j < count; ++j) {
            PGW_Route *route =
                PGW_RouteSeq_get_reference(&session->routes, j);
            if (!route->listener.on_data_available) abort();
            route->listener.on_data_available(route->listener.context);
        }
        if (active_count) PGW_test_wait_dispatches(session, target);
        for (RTI_INT32 j = 0; j < count; ++j) {
            PGW_Route *route =
                PGW_RouteSeq_get_reference(&session->routes, j);
            if (!was_faulted[j] &&
                PGW_ATOMIC_LOAD(&route->lifecycle,
                    OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) == PGW_FAULTED &&
                status == PGW_OK)
                status = route->error.status;
        }
        PGW_Status session_status = PGW_ATOMIC_LOAD(
            &session->error, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
        if (status == PGW_OK && session_status != PGW_OK)
            status = session_status;
    }
    return status;
}

PGW_Status PGW_test_registry_initialize(PGW_Registry *registry,
                                        PGW_AdapterRef *adapters, size_t adapter_capacity,
                                        PGW_SampleRepresentationRef *bindings, size_t binding_capacity)
{
    PGW_AdapterSeq adapter_sequence;
    PGW_SampleRepresentationSeq binding_sequence;
    if (adapter_capacity > INT32_MAX || binding_capacity > INT32_MAX ||
        !PGW_AdapterSeq_initialize(&adapter_sequence) ||
        !PGW_SampleRepresentationSeq_initialize(&binding_sequence)) return PGW_INVALID;
    if (!PGW_AdapterSeq_loan_contiguous(&adapter_sequence, adapters, 0,
                                         (RTI_INT32)adapter_capacity) ||
        !PGW_SampleRepresentationSeq_loan_contiguous(&binding_sequence, bindings, 0,
                                                (RTI_INT32)binding_capacity)) {
        (void)PGW_AdapterSeq_unloan(&adapter_sequence);
        (void)PGW_AdapterSeq_finalize(&adapter_sequence);
        (void)PGW_SampleRepresentationSeq_unloan(&binding_sequence);
        (void)PGW_SampleRepresentationSeq_finalize(&binding_sequence);
        return PGW_CAPACITY;
    }
    PGW_Status status = PGW_Registry_initialize(registry, &adapter_sequence, &binding_sequence);
    (void)PGW_AdapterSeq_unloan(&adapter_sequence);
    (void)PGW_AdapterSeq_finalize(&adapter_sequence);
    (void)PGW_SampleRepresentationSeq_unloan(&binding_sequence);
    (void)PGW_SampleRepresentationSeq_finalize(&binding_sequence);
    return status;
}

bool PGW_test_diagnostics_initialize(PGW_Diagnostics *diagnostics, PGW_Event *storage,
                                     size_t capacity)
{
    PGW_EventSeq sequence;
    if (!storage || !capacity || capacity > INT32_MAX ||
        !PGW_EventSeq_initialize(&sequence)) return false;
    if (!PGW_EventSeq_loan_contiguous(&sequence, storage, 0, (RTI_INT32)capacity)) {
        (void)PGW_EventSeq_finalize(&sequence);
        return false;
    }
    bool result = PGW_Diagnostics_initialize(diagnostics, &sequence);
    if (!PGW_EventSeq_unloan(&sequence) || !PGW_EventSeq_finalize(&sequence)) return false;
    return result;
}

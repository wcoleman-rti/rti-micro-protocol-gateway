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
#include "pgw/core.h"
#include "osapi/osapi_thread.h"
#include <limits.h>
#include <string.h>

static PGW_EntityState route_state(const PGW_Route *route)
{
    return (PGW_EntityState)PGW_ATOMIC_LOAD(
        &route->lifecycle, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

static void route_state_store(PGW_Route *route, PGW_EntityState state)
{
    PGW_ATOMIC_STORE(&route->lifecycle, state, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
}

static PGW_EntityState session_state(const PGW_Session *session)
{
    return (PGW_EntityState)PGW_ATOMIC_LOAD(
        &session->lifecycle, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

static void session_state_store(PGW_Session *session, PGW_EntityState state)
{
    PGW_ATOMIC_STORE(&session->lifecycle, state, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
}

static void session_fault(PGW_Session *session, PGW_Status status)
{
    PGW_ATOMIC_STORE(&session->error, status, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
    session_state_store(session, PGW_FAULTED);
}

static PGW_EntityState service_state(const PGW_Service *service)
{
    return (PGW_EntityState)PGW_ATOMIC_LOAD(
        &service->lifecycle, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
}

static void service_state_store(PGW_Service *service, PGW_EntityState state)
{
    PGW_ATOMIC_STORE(&service->lifecycle, state, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
}

static bool service_state_take(PGW_Service *service)
{
    return service && service->state_mutex &&
           OSAPI_Mutex_take(service->state_mutex);
}

static bool service_state_give(PGW_Service *service)
{
    return service && service->state_mutex &&
           OSAPI_Mutex_give(service->state_mutex);
}

bool PGW_size_add(size_t a, size_t b, size_t *out)
{
    if (!out || a > SIZE_MAX - b) return false;
    *out = a + b;
    return true;
}

bool PGW_size_multiply(size_t a, size_t b, size_t *out)
{
    if (!out || (a && b > SIZE_MAX / a)) return false;
    *out = a * b;
    return true;
}

PGW_Status PGW_core_resource_report(const PGW_SizeSeq *capacities,
                                    size_t sessions, bool diagnostics,
                                    size_t events, PGW_CoreResourceReport *out)
{
    if (!capacities) return PGW_INVALID;
    size_t routes = (size_t)PGW_SizeSeq_get_length(capacities);
    if (!routes || !sessions || !out || (!diagnostics && events))
        return PGW_INVALID;
    PGW_CoreResourceReport report = {0};
    size_t route_bytes, session_bytes, references = 0;
    if (!PGW_size_multiply(routes, sizeof(PGW_Route), &route_bytes) ||
        !PGW_size_multiply(sessions, sizeof(PGW_Session), &session_bytes) ||
        !PGW_size_add(sizeof(PGW_Service), session_bytes,
                      &report.objects_bytes) ||
        !PGW_size_add(report.objects_bytes, route_bytes,
                      &report.objects_bytes))
        return PGW_CAPACITY;
    for (size_t i = 0; i < routes; ++i) {
        size_t capacity = *PGW_SizeSeq_get_reference(capacities, (RTI_INT32)i);
        if (!capacity || capacity > INT32_MAX) return PGW_INVALID;
        if (!PGW_size_add(references, capacity, &references)) return PGW_CAPACITY;
    }
    if (!PGW_size_multiply(references, sizeof(PGW_SampleRef), &report.sample_references_bytes) ||
        !PGW_size_multiply(references, sizeof(PGW_WriteResult), &report.write_results_bytes) ||
        !PGW_size_multiply(events, sizeof(PGW_Event), &report.diagnostics_bytes))
        return PGW_CAPACITY;
    if (diagnostics && !PGW_size_add(report.diagnostics_bytes, sizeof(PGW_Diagnostics),
                                     &report.diagnostics_bytes)) return PGW_CAPACITY;
    if (!PGW_size_add(report.objects_bytes, report.sample_references_bytes, &report.total_bytes) ||
        !PGW_size_add(report.total_bytes, report.write_results_bytes, &report.total_bytes) ||
        !PGW_size_add(report.total_bytes, report.diagnostics_bytes, &report.total_bytes))
        return PGW_CAPACITY;
    *out = report;
    return PGW_OK;
}

PGW_Status PGW_Arena_allocate(PGW_Arena *a, size_t bytes, size_t alignment, void **out)
{
    if (!a || !out || !a->storage || !alignment ||
        (alignment & (alignment - 1)) || a->used > a->capacity) return PGW_INVALID;
    uintptr_t base = (uintptr_t)a->storage;
    if (a->used > UINTPTR_MAX - base) return PGW_CAPACITY;
    uintptr_t current = base + a->used;
    size_t padding = (size_t)((alignment - current % alignment) % alignment);
    size_t offset, end;
    if (!PGW_size_add(a->used, padding, &offset) ||
        !PGW_size_add(offset, bytes, &end) || end > a->capacity ||
        end > UINTPTR_MAX - base) return PGW_CAPACITY;
    *out = (unsigned char *)a->storage + offset;
    a->used = end;
    return PGW_OK;
}

bool PGW_type_info_equal(const PGW_TypeInfo *a, const PGW_TypeInfo *b)
{
    return a && b && a->name && a->name[0] && b->name && b->name[0] &&
           a->fingerprint && a->fingerprint[0] && b->fingerprint && b->fingerprint[0] &&
           a->version == b->version && !strcmp(a->name, b->name) &&
           !strcmp(a->fingerprint, b->fingerprint);
}

static bool representation_valid(const PGW_SampleRepresentation *b)
{
    bool access_valid = !b || !b->access ||
        (b->access->version == PGW_ABI_VERSION &&
         b->access->size == sizeof(PGW_SampleAccessI) &&
         (!b->access->view || b->view_contract));
    bool view_valid = !b || !b->view_contract ||
        (b->access && b->access->view && b->view_contract->type_identity &&
         (b->view_contract->kind == PGW_SAMPLE_VIEW_CANONICAL ||
          b->view_contract->kind == PGW_SAMPLE_VIEW_NATIVE) &&
         (b->view_contract->kind != PGW_SAMPLE_VIEW_CANONICAL ||
          b->view_contract->value_size));
    return b && b->name && b->name[0] && b->sample_size && b->sample_alignment &&
        !(b->sample_alignment & (b->sample_alignment - 1)) &&
        PGW_type_info_equal(b->schema, b->schema) &&
        access_valid && view_valid;
}

PGW_Status PGW_Registry_initialize(PGW_Registry *r, const PGW_AdapterSeq *adapters,
                                  const PGW_SampleRepresentationSeq *representations)
{
    if (!r || !adapters || !representations || r->initialized) return PGW_INVALID;
    RTI_INT32 adapter_capacity = PGW_AdapterSeq_get_maximum(adapters);
    RTI_INT32 representation_capacity =
        PGW_SampleRepresentationSeq_get_maximum(representations);
    RTI_INT32 adapter_length = PGW_AdapterSeq_get_length(adapters);
    RTI_INT32 representation_length =
        PGW_SampleRepresentationSeq_get_length(representations);
    PGW_AdapterRef *adapter_storage = PGW_AdapterSeq_get_contiguous_buffer(adapters);
    PGW_SampleRepresentationRef *representation_storage =
        PGW_SampleRepresentationSeq_get_contiguous_buffer(representations);
    if (adapter_capacity <= 0 || representation_capacity <= 0 ||
        adapter_length < 0 || adapter_length > adapter_capacity ||
        representation_length < 0 || representation_length > representation_capacity ||
        !adapter_storage || !representation_storage) return PGW_INVALID;
    r->frozen = false;
    if (!PGW_AdapterSeq_initialize(&r->adapters)) return PGW_FATAL;
    if (!PGW_SampleRepresentationSeq_initialize(&r->representations)) {
        (void)PGW_AdapterSeq_finalize(&r->adapters);
        return PGW_FATAL;
    }
    r->initialized = true;
    if (!PGW_AdapterSeq_loan_contiguous(&r->adapters, adapter_storage,
                                        adapter_length, adapter_capacity)) {
        (void)PGW_Registry_finalize(r);
        return PGW_INVALID;
    }
    r->adapters_borrowed = true;
    if (!PGW_SampleRepresentationSeq_loan_contiguous(
            &r->representations, representation_storage, representation_length,
            representation_capacity)) {
        (void)PGW_Registry_finalize(r);
        return PGW_INVALID;
    }
    r->representations_borrowed = true;
    return PGW_OK;
}

PGW_Status PGW_Registry_finalize(PGW_Registry *r)
{
    if (!r || !r->initialized) return PGW_INVALID;
    bool adapters = !r->adapters_borrowed || PGW_AdapterSeq_unloan(&r->adapters);
    bool representations = !r->representations_borrowed ||
        PGW_SampleRepresentationSeq_unloan(&r->representations);
    if (adapters) adapters = PGW_AdapterSeq_finalize(&r->adapters);
    if (representations)
        representations = PGW_SampleRepresentationSeq_finalize(&r->representations);
    if (!adapters || !representations) return PGW_LOAN_ERROR;
    r->frozen = false;
    r->initialized = false;
    r->adapters_borrowed = false;
    r->representations_borrowed = false;
    return PGW_OK;
}

PGW_Status PGW_Registry_register_adapter(PGW_Registry *r, const PGW_AdapterI *a)
{
    if (!r || !a || r->frozen || a->version != PGW_ABI_VERSION ||
        a->size != sizeof(*a) || !a->name || !a->name[0] || !a->create || !a->connection ||
        a->connection->version != PGW_ABI_VERSION ||
        a->connection->size != sizeof(PGW_ConnectionI) ||
        (!a->connection->lookup_stream_reader && !a->connection->lookup_stream_writer) ||
        !a->connection->close || !r->initialized) return PGW_INVALID;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (a->control && (a->control->version != PGW_CONTROL_ABI_VERSION ||
        a->control->size != sizeof(PGW_ControlAdapterI) ||
        a->control->manifest_version != 1 || !a->control->apply ||
        (!!a->control->telemetry_metric_mask != !!a->control->read_telemetry)))
        return PGW_INVALID;
#endif
    if (PGW_Registry_find_adapter(r, a->name)) return PGW_INVALID;
    RTI_INT32 count = PGW_AdapterSeq_get_length(&r->adapters);
    if (count == PGW_AdapterSeq_get_maximum(&r->adapters)) return PGW_CAPACITY;
    if (!PGW_AdapterSeq_set_length(&r->adapters, count + 1)) return PGW_CAPACITY;
    *PGW_AdapterSeq_get_reference(&r->adapters, count) = a;
    return PGW_OK;
}

PGW_Status PGW_Registry_register_representation(
    PGW_Registry *r, const PGW_SampleRepresentation *representation)
{
    if (!r || r->frozen || !representation_valid(representation) ||
        !r->initialized) return PGW_INVALID;
    if (PGW_Registry_find_representation(r, representation->name)) return PGW_INVALID;
    RTI_INT32 count = PGW_SampleRepresentationSeq_get_length(&r->representations);
    if (count == PGW_SampleRepresentationSeq_get_maximum(&r->representations)) return PGW_CAPACITY;
    if (!PGW_SampleRepresentationSeq_set_length(&r->representations, count + 1)) return PGW_CAPACITY;
    *PGW_SampleRepresentationSeq_get_reference(&r->representations, count) =
        representation;
    return PGW_OK;
}

const PGW_AdapterI *PGW_Registry_find_adapter(const PGW_Registry *r, const char *name)
{
    if (!r || !r->initialized || !name) return NULL;
    for (RTI_INT32 i = 0; i < PGW_AdapterSeq_get_length(&r->adapters); ++i) {
        PGW_AdapterRef adapter = *PGW_AdapterSeq_get_reference(&r->adapters, i);
        if (!strcmp(adapter->name, name)) return adapter;
    }
    return NULL;
}

const PGW_SampleRepresentation *PGW_Registry_find_representation(const PGW_Registry *r, const char *name)
{
    if (!r || !r->initialized || !name) return NULL;
    for (RTI_INT32 i = 0; i < PGW_SampleRepresentationSeq_get_length(&r->representations); ++i) {
        PGW_SampleRepresentationRef binding = *PGW_SampleRepresentationSeq_get_reference(&r->representations, i);
        if (!strcmp(binding->name, name)) return binding;
    }
    return NULL;
}

PGW_Status PGW_Route_initialize_storage(PGW_Route *r, const PGW_SampleSeq *samples,
                                       const PGW_WriteResultSeq *results)
{
    if (!r || route_state(r) != PGW_UNINITIALIZED || !samples || !results ||
        r->storage_initialized) return PGW_INVALID;
    RTI_INT32 capacity = PGW_SampleSeq_get_maximum(samples);
    RTI_INT32 result_capacity = PGW_WriteResultSeq_get_maximum(results);
    PGW_SampleRef *references = PGW_SampleSeq_get_contiguous_buffer(samples);
    PGW_WriteResult *outcomes = PGW_WriteResultSeq_get_contiguous_buffer(results);
    if (capacity <= 0 || result_capacity < capacity || !references || !outcomes ||
        PGW_SampleSeq_get_length(samples) != 0 ||
        PGW_WriteResultSeq_get_length(results) != 0) return PGW_INVALID;
    if (!PGW_SampleSeq_initialize(&r->samples)) return PGW_FATAL;
    if (!PGW_WriteResultSeq_initialize(&r->results)) {
        (void)PGW_SampleSeq_finalize(&r->samples);
        return PGW_FATAL;
    }
    if (!PGW_SampleSeq_loan_contiguous(&r->samples, references, 0, capacity)) {
        (void)PGW_SampleSeq_finalize(&r->samples);
        (void)PGW_WriteResultSeq_finalize(&r->results);
        return PGW_INVALID;
    }
    r->samples_borrowed = true;
    if (!PGW_WriteResultSeq_loan_contiguous(&r->results, outcomes, 0, capacity)) {
        (void)PGW_SampleSeq_unloan(&r->samples);
        (void)PGW_SampleSeq_finalize(&r->samples);
        (void)PGW_WriteResultSeq_finalize(&r->results);
        r->samples_borrowed = false;
        return PGW_INVALID;
    }
    r->results_borrowed = true;
    r->storage_initialized = true;
    return PGW_OK;
}

static PGW_Status session_signal_wake(PGW_Session *session)
{
    if (!session || !session->async_waitset.wake_condition) return PGW_OK;
    if (DDS_GuardCondition_set_trigger_value(session->async_waitset.wake_condition,
            DDS_BOOLEAN_TRUE) != DDS_RETCODE_OK) {
        PGW_ATOMIC_STORE(&session->error, PGW_IO_ERROR,
                              OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
        return PGW_IO_ERROR;
    }
    return PGW_OK;
}

static PGW_Status route_trigger_ready(PGW_Route *route)
{
    if (!route->ready_condition) return PGW_INVALID;
    if (DDS_GuardCondition_set_trigger_value(route->ready_condition,
            DDS_BOOLEAN_TRUE) != DDS_RETCODE_OK) {
        if (route->session)
            PGW_ATOMIC_STORE(&route->session->error, PGW_IO_ERROR,
                             OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
        return PGW_IO_ERROR;
    }
    return PGW_OK;
}

static PGW_Status route_signal_ready(PGW_Route *route)
{
    if (!route) return PGW_INVALID;
    if (PGW_ATOMIC_LOAD(&route->lifecycle,
            OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) == PGW_FAULTED)
        return PGW_OK;
    PGW_ATOMIC_ADD(&route->ready_generation, 1,
                   OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE);
    if (PGW_ATOMIC_LOAD(&route->lifecycle,
            OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) == PGW_PAUSED)
        return PGW_OK;
    return route_trigger_ready(route);
}

static void route_data_available(void *context)
{
    PGW_Route *route = context;
    if (!route || !route->session) return;
    (void)route_signal_ready(route);
}

#if defined(PGW_ENABLE_REMOTE_CONTROL)
static void control_data_available(void *context)
{
    PGW_Service *service = context;
    if (!service || !service->control_session) return;
    PGW_ATOMIC_ADD(&service->control_generation, 1,
                   OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE);
    (void)session_signal_wake(service->control_session);
}
#endif

static PGW_Status notify_route_state(PGW_Route *route)
{
    PGW_Session *session = route->session;
    if (!session) return PGW_OK;
    PGW_Status status = session_signal_wake(session);
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (session->service && session->service->control_session &&
        session->service->control_session != session) {
        PGW_Status control_status =
            session_signal_wake(session->service->control_session);
        if (status == PGW_OK) status = control_status;
    }
#endif
    return status;
}

PGW_Status PGW_Route_pause(PGW_Route *r)
{
    if (!r || !r->storage_initialized) return PGW_INVALID;
    PGW_EntityState state = route_state(r);
    if (state == PGW_FAULTED) return PGW_FATAL;
    if (state == PGW_PAUSED) return PGW_NO_CHANGE;
    PGW_Service *service = r->session ? r->session->service : NULL;
    if (!service) return PGW_INVALID;
    if (!service_state_take(service)) return PGW_IO_ERROR;
    PGW_EntityState lifecycle = (PGW_EntityState)PGW_ATOMIC_LOAD(
        &r->lifecycle, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
    if (lifecycle == PGW_FAULTED) {
        if (!service_state_give(service)) return PGW_IO_ERROR;
        return PGW_FATAL;
    }
    if (lifecycle == PGW_PAUSED) {
        if (!service_state_give(service)) return PGW_IO_ERROR;
        return PGW_NO_CHANGE;
    }
    if (lifecycle != PGW_ENABLED && lifecycle != PGW_STARTED) {
        if (!service_state_give(service)) return PGW_IO_ERROR;
        return PGW_INVALID;
    }
    PGW_ATOMIC_STORE(&r->lifecycle, PGW_PAUSED,
                     OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
    if (!service_state_give(service)) return PGW_IO_ERROR;
    return notify_route_state(r);
}

PGW_Status PGW_Route_resume(PGW_Route *r)
{
    if (!r || !r->storage_initialized) return PGW_INVALID;
    if (route_state(r) == PGW_FAULTED) return PGW_FATAL;
    PGW_Service *service = r->session ? r->session->service : NULL;
    if (!service) return PGW_INVALID;
    if (!service_state_take(service)) return PGW_IO_ERROR;
    PGW_EntityState lifecycle = (PGW_EntityState)PGW_ATOMIC_LOAD(
        &r->lifecycle, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
    if (lifecycle == PGW_FAULTED) {
        if (!service_state_give(service)) return PGW_IO_ERROR;
        return PGW_FATAL;
    }
    if (lifecycle == PGW_PAUSED) {
        PGW_ATOMIC_STORE(&r->lifecycle, PGW_ENABLED,
                         OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
        if (!service_state_give(service)) return PGW_IO_ERROR;
        uint64_t generation = (uint64_t)PGW_ATOMIC_LOAD(
            &r->ready_generation, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
        if (generation != (uint64_t)PGW_ATOMIC_LOAD(
                &r->dispatched_generation, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE)) {
            PGW_Status status = route_trigger_ready(r);
            if (status != PGW_OK) return status;
        }
        return notify_route_state(r);
    }
    if (lifecycle == PGW_ENABLED || lifecycle == PGW_STARTED) {
        if (!service_state_give(service)) return PGW_IO_ERROR;
        return PGW_NO_CHANGE;
    }
    if (!service_state_give(service)) return PGW_IO_ERROR;
    return PGW_INVALID;
}

PGW_Status PGW_Session_set_routes(PGW_Session *session,
                                  const PGW_RouteSeq *routes)
{
    if (!session || session_state(session) != PGW_UNINITIALIZED || !routes ||
        session->routes_initialized) return PGW_INVALID;
    RTI_INT32 count = PGW_RouteSeq_get_length(routes);
    RTI_INT32 capacity = PGW_RouteSeq_get_maximum(routes);
    PGW_Route *route_storage = PGW_RouteSeq_get_contiguous_buffer(routes);
    if (count <= 0 || capacity < count || !route_storage) return PGW_INVALID;
    if (!PGW_RouteSeq_initialize(&session->routes)) return PGW_FATAL;
    if (!PGW_RouteSeq_loan_contiguous(
            &session->routes, route_storage, count, capacity)) {
        (void)PGW_RouteSeq_finalize(&session->routes);
        return PGW_INVALID;
    }
    session->routes_initialized = true;
    session->routes_borrowed = true;
    return PGW_OK;
}

PGW_Status PGW_Service_set_sessions(PGW_Service *service,
                                   const PGW_SessionSeq *sessions)
{
    if (!service || service_state(service) != PGW_UNINITIALIZED || !sessions ||
        service->sessions_initialized) return PGW_INVALID;
    RTI_INT32 count = PGW_SessionSeq_get_length(sessions);
    RTI_INT32 capacity = PGW_SessionSeq_get_maximum(sessions);
    PGW_Session *storage = PGW_SessionSeq_get_contiguous_buffer(sessions);
    if (count <= 0 || capacity < count || !storage) return PGW_INVALID;
    if (!PGW_SessionSeq_initialize(&service->sessions)) return PGW_FATAL;
    if (!PGW_SessionSeq_loan_contiguous(
            &service->sessions, storage, count, capacity)) {
        (void)PGW_SessionSeq_finalize(&service->sessions);
        return PGW_INVALID;
    }
    service->sessions_initialized = true;
    service->sessions_borrowed = true;
    return PGW_OK;
}

#if defined(PGW_ENABLE_REMOTE_CONTROL)
static PGW_ControlResource *control_find_resource(PGW_Service *, uint32_t);

static bool control_route_is_configured(const PGW_Service *service, const PGW_Route *route)
{
    for (RTI_INT32 i = 0; i < PGW_SessionSeq_get_length(&service->sessions); ++i) {
        const PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        for (RTI_INT32 j = 0; j < PGW_RouteSeq_get_length(&session->routes); ++j)
            if (PGW_RouteSeq_get_reference(&session->routes, j) == route)
                return true;
    }
    return false;
}

PGW_Status PGW_Service_set_control(PGW_Service *service,
                                  PGW_Session *control_session,
                                  PGW_ControlEndpoint endpoint,
                                  PGW_ControlResource *resources, size_t count)
{
    const uint32_t action_bits = (UINT32_C(1) << 8) - 1;
    OSAPI_Mutex_T *control_mutex;
    if (!service || service_state(service) != PGW_UNINITIALIZED ||
        !service->sessions_initialized || !control_session ||
        !resources || !count ||
        service->control_resources) return PGW_INVALID;
    if (!endpoint.iface || endpoint.iface->version != PGW_CONTROL_ABI_VERSION ||
        endpoint.iface->size != sizeof(PGW_ControlEndpointI) ||
        !endpoint.iface->register_listener ||
        !endpoint.iface->unregister_listener ||
        !endpoint.iface->take_command || !endpoint.iface->rearm_commands ||
        !endpoint.iface->write_state ||
        !endpoint.iface->write_result) return PGW_INVALID;
    bool session_found = false;
    for (RTI_INT32 i = 0; i < PGW_SessionSeq_get_length(&service->sessions); ++i)
        if (PGW_SessionSeq_get_reference(&service->sessions, i) == control_session)
            session_found = true;
    if (!session_found) return PGW_INVALID;
    for (size_t i = 0; i < count; ++i) {
        PGW_ControlResource *resource = &resources[i];
        if ((unsigned)resource->kind > PGW_CONTROL_RESOURCE_ROUTE ||
            (unsigned)resource->status > PGW_CONTROL_STATUS_FAULTED ||
            (resource->command_capabilities & ~action_bits)) return PGW_INVALID;
        for (size_t j = 0; j < i; ++j)
            if (resources[j].id == resource->id) return PGW_INVALID;
        if (resource->kind == PGW_CONTROL_RESOURCE_ROUTE) {
            uint32_t route_actions =
                PGW_CONTROL_ACTION_MASK(PGW_CONTROL_ROUTE_PAUSE) |
                PGW_CONTROL_ACTION_MASK(PGW_CONTROL_ROUTE_RESUME);
            if (!resource->route || resource->adapter ||
                !control_route_is_configured(service, resource->route) ||
                (resource->command_capabilities & ~route_actions))
                return PGW_INVALID;
        } else {
            uint32_t resource_bit = UINT32_C(1) << resource->kind;
            if (resource->route || !resource->adapter ||
                resource->adapter->version != PGW_CONTROL_ABI_VERSION ||
                resource->adapter->size != sizeof(PGW_ControlAdapterI) ||
                resource->adapter->manifest_version != 1 ||
                !resource->adapter->apply ||
                !(resource->adapter->resource_kind_mask & resource_bit) ||
                (resource->command_capabilities & ~resource->adapter->action_mask))
                return PGW_INVALID;
        }
    }
    control_mutex = OSAPI_Mutex_new();
    if (!control_mutex) return PGW_FATAL;
    service->control = endpoint;
    service->control_session = control_session;
    service->control_listener = (PGW_ReaderListener){
        control_data_available, service
    };
    service->control_mutex = control_mutex;
    PGW_ATOMIC_INIT(&service->control_generation, 0);
    service->control_listener_registered = false;
    service->control_resources = resources;
    service->control_resource_count = count;
    service->control_state_retry_cursor = 0;
    service->control_counters = (PGW_ControlCounters){0};
    return PGW_OK;
}

PGW_Status PGW_Service_control_counters(PGW_Service *service,
                                       PGW_ControlCounters *out)
{
    if (!service || !out || !service->control_resources) return PGW_INVALID;
    if (!service->control_mutex ||
        !OSAPI_Mutex_take(service->control_mutex)) return PGW_IO_ERROR;
    *out = service->control_counters;
    return OSAPI_Mutex_give(service->control_mutex) ? PGW_OK : PGW_IO_ERROR;
}

PGW_Status PGW_Service_set_telemetry(PGW_Service *service,
                                    PGW_ControlTelemetryMetric *metrics,
                                    size_t count, uint32_t period_ms,
                                    uint32_t minimum_period_ms)
{
    if (!service || service_state(service) != PGW_UNINITIALIZED ||
        !service->control_resources || service->control_telemetry_metrics ||
        !metrics || !count || !minimum_period_ms ||
        !service->control.iface->write_telemetry ||
        (period_ms && period_ms < minimum_period_ms) ||
        (period_ms && !service->clock_ns)) return PGW_INVALID;
    for (size_t i = 0; i < count; ++i) {
        PGW_ControlTelemetryMetric *metric = &metrics[i];
        PGW_ControlResource *resource =
            control_find_resource(service, metric->resource_id);
        if (!resource || !metric->name || !*metric->name ||
            !metric->unit || strlen(metric->unit) > 32 ||
            (unsigned)metric->scalar_type > PGW_CONTROL_SCALAR_DOUBLE ||
            metric->telemetry_kind >= 32 || metric->adapter_metric_bit >= 32 ||
            !metric->adapter || metric->adapter != resource->adapter ||
            !metric->adapter->read_telemetry ||
            !(metric->adapter->telemetry_metric_mask &
              (UINT32_C(1) << metric->adapter_metric_bit)) ||
            !(resource->telemetry_capabilities &
              (UINT32_C(1) << metric->telemetry_kind)))
            return PGW_INVALID;
        for (size_t j = 0; j < i; ++j)
            if (metrics[j].id == metric->id ||
                (metrics[j].resource_id == metric->resource_id &&
                 metrics[j].telemetry_kind == metric->telemetry_kind))
                return PGW_INVALID;
    }
    service->control_telemetry_metrics = metrics;
    service->control_telemetry_metric_count = count;
    service->control_telemetry_period_ns = (uint64_t)period_ms * UINT64_C(1000000);
    service->control_telemetry_last_ns = 0;
    service->control_telemetry_clock_initialized = false;
    return PGW_OK;
}

static PGW_ControlResourceStatus control_route_status(const PGW_Route *route)
{
    PGW_EntityState state = route_state(route);
    if (state == PGW_FAULTED) return PGW_CONTROL_STATUS_FAULTED;
    if (state == PGW_PAUSED) return PGW_CONTROL_STATUS_PAUSED;
    if (state == PGW_ENABLED || state == PGW_STARTED)
        return PGW_CONTROL_STATUS_UP;
    return PGW_CONTROL_STATUS_UNKNOWN;
}

static PGW_ControlState control_state(const PGW_ControlResource *resource)
{
    PGW_ControlResourceStatus status = resource->kind == PGW_CONTROL_RESOURCE_ROUTE ?
        control_route_status(resource->route) : resource->status;
    return (PGW_ControlState){resource->id, status,
        resource->command_capabilities, resource->telemetry_capabilities};
}

static bool control_write_state(PGW_Service *service, PGW_ControlResource *resource,
                                bool retry)
{
    PGW_ControlState state = control_state(resource);
    if (retry) ++service->control_counters.state_retries;
    if (service->control.iface->write_state(service->control.state, &state) != PGW_OK) {
        resource->state_dirty = true;
        ++service->control_counters.state_write_failures;
        return false;
    }
    resource->state_dirty = false;
    return true;
}

static PGW_ControlResource *control_find_resource(PGW_Service *service, uint32_t id)
{
    for (size_t i = 0; i < service->control_resource_count; ++i)
        if (service->control_resources[i].id == id)
            return &service->control_resources[i];
    return NULL;
}

static bool control_expected_status(PGW_ControlResourceKind kind,
                                    PGW_ControlAction action,
                                    PGW_ControlResourceStatus *status)
{
    switch (action) {
        case PGW_CONTROL_CONNECTION_UP:
            if (kind != PGW_CONTROL_RESOURCE_CONNECTION) return false;
            *status = PGW_CONTROL_STATUS_UP;
            return true;
        case PGW_CONTROL_CONNECTION_DOWN:
            if (kind != PGW_CONTROL_RESOURCE_CONNECTION) return false;
            *status = PGW_CONTROL_STATUS_DOWN;
            return true;
        case PGW_CONTROL_INPUT_ENABLE:
            if (kind != PGW_CONTROL_RESOURCE_INPUT) return false;
            *status = PGW_CONTROL_STATUS_UP;
            return true;
        case PGW_CONTROL_INPUT_DISABLE:
            if (kind != PGW_CONTROL_RESOURCE_INPUT) return false;
            *status = PGW_CONTROL_STATUS_DOWN;
            return true;
        case PGW_CONTROL_OUTPUT_ENABLE:
            if (kind != PGW_CONTROL_RESOURCE_OUTPUT) return false;
            *status = PGW_CONTROL_STATUS_UP;
            return true;
        case PGW_CONTROL_OUTPUT_DISABLE:
            if (kind != PGW_CONTROL_RESOURCE_OUTPUT) return false;
            *status = PGW_CONTROL_STATUS_DOWN;
            return true;
        case PGW_CONTROL_ROUTE_PAUSE:
            if (kind != PGW_CONTROL_RESOURCE_ROUTE) return false;
            *status = PGW_CONTROL_STATUS_PAUSED;
            return true;
        case PGW_CONTROL_ROUTE_RESUME:
            if (kind != PGW_CONTROL_RESOURCE_ROUTE) return false;
            *status = PGW_CONTROL_STATUS_UP;
            return true;
    }
    return false;
}

static PGW_ControlOutcome control_apply(PGW_ControlResource *resource,
                                       const PGW_ControlCommand *command)
{
    PGW_ControlResourceStatus desired;
    if ((unsigned)command->action > PGW_CONTROL_ROUTE_RESUME)
        return PGW_CONTROL_OUTCOME_INVALID;
    if (!(resource->command_capabilities &
          PGW_CONTROL_ACTION_MASK(command->action)))
        return PGW_CONTROL_OUTCOME_UNSUPPORTED;
    if (!control_expected_status(resource->kind, command->action, &desired))
        return PGW_CONTROL_OUTCOME_UNSUPPORTED;
    if (resource->kind != PGW_CONTROL_RESOURCE_ROUTE &&
        resource->status != PGW_CONTROL_STATUS_UNKNOWN &&
        resource->status == desired)
        return PGW_CONTROL_OUTCOME_NO_CHANGE;
    PGW_Status status;
    if (resource->kind == PGW_CONTROL_RESOURCE_ROUTE) {
        status = command->action == PGW_CONTROL_ROUTE_PAUSE ?
            PGW_Route_pause(resource->route) : PGW_Route_resume(resource->route);
    } else {
        status = resource->adapter->apply(resource->adapter_state, command->action);
    }
    if (status == PGW_OK) {
        resource->status = desired;
        return PGW_CONTROL_OUTCOME_APPLIED;
    }
    if (status == PGW_NO_CHANGE) {
        if (resource->status == PGW_CONTROL_STATUS_UNKNOWN)
            resource->status = desired;
        return PGW_CONTROL_OUTCOME_NO_CHANGE;
    }
    if (status == PGW_UNSUPPORTED) return PGW_CONTROL_OUTCOME_UNSUPPORTED;
    if (status == PGW_INVALID) return PGW_CONTROL_OUTCOME_INVALID;
    return PGW_CONTROL_OUTCOME_FAILED;
}

static size_t control_process_commands(PGW_Service *service)
{
    size_t processed = 0;
    for (; processed < PGW_CONTROL_MAX_COMMANDS_PER_BATCH; ++processed) {
        PGW_ControlCommand command = {0};
        PGW_ControlCorrelation correlation = {0};
        PGW_Status status = service->control.iface->take_command(
            service->control.state, &command, &correlation);
        if (status == PGW_NO_DATA) break;
        if (status != PGW_OK) {
            ++service->control_counters.command_read_failures;
            break;
        }
        ++service->control_counters.commands_processed;
        PGW_ControlResource *resource = control_find_resource(service, command.resource_id);
        PGW_ControlResourceStatus previous_status = resource ? resource->status :
            PGW_CONTROL_STATUS_UNKNOWN;
        PGW_ControlOutcome outcome = resource ?
            control_apply(resource, &command) : PGW_CONTROL_OUTCOME_INVALID;
        if (outcome == PGW_CONTROL_OUTCOME_INVALID)
            ++service->control_counters.commands_invalid;
        else if (outcome == PGW_CONTROL_OUTCOME_UNSUPPORTED)
            ++service->control_counters.commands_unsupported;
        else if (outcome == PGW_CONTROL_OUTCOME_FAILED)
            ++service->control_counters.commands_failed;
        else if (resource && resource->status != previous_status)
            (void)control_write_state(service, resource, false);
        PGW_ControlResult result = {
            command.resource_id, command.action, outcome, correlation
        };
        if (service->control.iface->write_result(service->control.state, &result) != PGW_OK)
            ++service->control_counters.result_write_failures;
    }
    return processed;
}

static void control_retry_one_state(PGW_Service *service)
{
    if (!service->control_resource_count) return;
    for (size_t i = 0; i < service->control_resource_count; ++i) {
        size_t index = service->control_state_retry_cursor;
        service->control_state_retry_cursor =
            (service->control_state_retry_cursor + 1) % service->control_resource_count;
        PGW_ControlResource *resource = &service->control_resources[index];
        if (!resource->state_dirty) continue;
        (void)control_write_state(service, resource, true);
        return;
    }
}

static void control_sync_route(PGW_Service *service, PGW_Route *route)
{
    PGW_ControlResourceStatus status = control_route_status(route);
    for (size_t i = 0; i < service->control_resource_count; ++i) {
        PGW_ControlResource *resource = &service->control_resources[i];
        if (resource->kind == PGW_CONTROL_RESOURCE_ROUTE &&
            resource->route == route && resource->status != status) {
            resource->status = status;
            (void)control_write_state(service, resource, false);
        }
    }
}

static void control_initialize(PGW_Service *service)
{
    if (!service->control_resources) return;
    for (size_t i = 0; i < service->control_resource_count; ++i) {
        PGW_ControlResource *resource = &service->control_resources[i];
        if (resource->kind == PGW_CONTROL_RESOURCE_ROUTE)
            resource->status = control_route_status(resource->route);
        (void)control_write_state(service, resource, false);
    }
}

static void control_publish_telemetry(PGW_Service *service)
{
    if (!service->control_telemetry_metric_count ||
        !service->control_telemetry_period_ns) return;
    uint64_t now;
    if (!service->clock_ns(service->clock_state, &now)) {
        ++service->control_counters.telemetry_clock_failures;
        return;
    }
    if (!service->control_telemetry_clock_initialized ||
        now < service->control_telemetry_last_ns) {
        service->control_telemetry_last_ns = now;
        service->control_telemetry_clock_initialized = true;
        return;
    }
    if (now - service->control_telemetry_last_ns <
        service->control_telemetry_period_ns) return;
    service->control_telemetry_last_ns = now;
    for (size_t i = 0; i < service->control_telemetry_metric_count; ++i) {
        const PGW_ControlTelemetryMetric *metric =
            &service->control_telemetry_metrics[i];
        PGW_ControlScalar scalar = {.type = metric->scalar_type};
        PGW_Status status = metric->adapter->read_telemetry(
            metric->adapter_state, metric->name, &scalar);
        if (status != PGW_OK || scalar.type != metric->scalar_type) {
            ++service->control_counters.telemetry_read_failures;
            continue;
        }
        PGW_ControlTelemetry sample = {
            .metric_id = metric->id,
            .resource_id = metric->resource_id,
            .telemetry_kind = metric->telemetry_kind,
            .scalar = scalar
        };
        size_t unit_length = strlen(metric->unit);
        memcpy(sample.unit, metric->unit, unit_length + 1);
        if (service->control.iface->write_telemetry(
                service->control.state, &sample) != PGW_OK) {
            ++service->control_counters.telemetry_write_failures;
            continue;
        }
        ++service->control_counters.telemetry_samples;
    }
}
#endif

static bool route_storage_finalize(PGW_Route *r)
{
    if (!r->storage_initialized) return false;
    bool samples = !r->samples_borrowed || PGW_SampleSeq_unloan(&r->samples);
    bool results = !r->results_borrowed || PGW_WriteResultSeq_unloan(&r->results);
    if (samples) samples = PGW_SampleSeq_finalize(&r->samples);
    if (results) results = PGW_WriteResultSeq_finalize(&r->results);
    if (samples && results) {
        r->storage_initialized = false;
        r->samples_borrowed = false;
        r->results_borrowed = false;
        return true;
    }
    return false;
}

static PGW_Status fail(PGW_Route *r, PGW_Status status, const char *operation)
{
    r->error = (PGW_Error){status, r->id, operation};
    PGW_Service *service = r->session ? r->session->service : NULL;
    if (service && service->state_mutex &&
        !OSAPI_Mutex_take(service->state_mutex)) {
        route_state_store(r, PGW_FAULTED);
        return PGW_IO_ERROR;
    }
    PGW_ATOMIC_STORE(&r->lifecycle, PGW_FAULTED,
                     OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
    if (service && service->state_mutex &&
        !OSAPI_Mutex_give(service->state_mutex))
        return PGW_IO_ERROR;
    PGW_Counters_add(&r->counters, PGW_COUNT_ROUTE_FAULTS, 1);
    (void)notify_route_state(r);
    return status;
}

static PGW_Status event(PGW_Service *s, PGW_Route *r, uint32_t code, uint64_t count)
{
    if (!s->diagnostics || !count) return PGW_OK;
    PGW_Event e = {
        .entity_id = r->id,
        .code = code,
        .severity = code == PGW_BACKPRESSURE || code == PGW_INVALID ? 2u : 3u,
        .count = count
    };
    if (s->clock_ns) {
        if (!s->clock_ns(s->clock_state, &e.time_ns)) return PGW_IO_ERROR;
        e.time_valid = true;
    }
    (void)PGW_Diagnostics_emit(s->diagnostics, &e);
    return PGW_OK;
}

#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
static bool route_latency_initialize(PGW_RouteLatencyStats *stats)
{
    PGW_ATOMIC_INIT(&stats->batches, 0);
    PGW_ATOMIC_INIT(&stats->timed_batches, 0);
    PGW_ATOMIC_INIT(&stats->samples, 0);
    PGW_ATOMIC_INIT(&stats->accepted, 0);
    PGW_ATOMIC_INIT(&stats->backpressure, 0);
    PGW_ATOMIC_INIT(&stats->invalid, 0);
    PGW_ATOMIC_INIT(&stats->fatal, 0);
    PGW_ATOMIC_INIT(&stats->clock_failures, 0);
    PGW_ATOMIC_INIT(&stats->total_ns, 0);
    PGW_ATOMIC_INIT(&stats->minimum_ns, UINT64_MAX);
    PGW_ATOMIC_INIT(&stats->maximum_ns, 0);
    for (size_t i = 0; i < PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS; ++i)
        PGW_ATOMIC_INIT(&stats->histogram[i], 0);
    return true;
}

static void route_latency_clock_failure(PGW_Route *route)
{
    PGW_ATOMIC_ADD(&route->latency.clock_failures, 1,
                              OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
}

static PGW_Status route_latency_observe(PGW_Route *route, uint64_t duration_ns)
{
    unsigned bucket = 0;
    uint64_t upper_bound = 1000;
    while (bucket + 1 < PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS &&
           duration_ns > upper_bound) {
        upper_bound <<= 1;
        ++bucket;
    }
    PGW_RouteLatencyStats *stats = &route->latency;
    PGW_ATOMIC_ADD(&stats->timed_batches, 1, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    PGW_ATOMIC_ADD(&stats->total_ns, duration_ns, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    PGW_ATOMIC_ADD(&stats->histogram[bucket], 1, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    PGW_Service *service = route->session ? route->session->service : NULL;
    if (!service || !service->state_mutex ||
        !OSAPI_Mutex_take(service->state_mutex))
        return PGW_IO_ERROR;
    uint64_t minimum = (uint64_t)PGW_ATOMIC_LOAD(
        &stats->minimum_ns, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    uint64_t maximum = (uint64_t)PGW_ATOMIC_LOAD(
        &stats->maximum_ns, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    if (duration_ns < minimum)
        PGW_ATOMIC_STORE(&stats->minimum_ns, duration_ns,
                         OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    if (duration_ns > maximum)
        PGW_ATOMIC_STORE(&stats->maximum_ns, duration_ns,
                         OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    return OSAPI_Mutex_give(service->state_mutex) ? PGW_OK : PGW_IO_ERROR;
}

static void route_latency_batch(PGW_Route *route, size_t samples,
                                uint64_t accepted, uint64_t backpressure,
                                uint64_t invalid, uint64_t fatal)
{
    PGW_RouteLatencyStats *stats = &route->latency;
    PGW_ATOMIC_ADD(&stats->batches, 1, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    PGW_ATOMIC_ADD(&stats->samples, samples, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    PGW_ATOMIC_ADD(&stats->accepted, accepted, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    PGW_ATOMIC_ADD(&stats->backpressure, backpressure, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    PGW_ATOMIC_ADD(&stats->invalid, invalid, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    PGW_ATOMIC_ADD(&stats->fatal, fatal, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
}

PGW_Status PGW_Route_latency_snapshot(const PGW_Route *route,
                                     PGW_RouteLatencySnapshot *out)
{
    if (!route || !out || !route->latency_initialized) return PGW_INVALID;
    const PGW_RouteLatencyStats *stats = &route->latency;
    PGW_Service *service = route->session ? route->session->service : NULL;
    if (!service || !service->state_mutex ||
        !OSAPI_Mutex_take(service->state_mutex))
        return PGW_IO_ERROR;
    PGW_RouteLatencySnapshot snapshot = {
        .batches = PGW_ATOMIC_LOAD(&stats->batches, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .timed_batches = PGW_ATOMIC_LOAD(&stats->timed_batches, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .samples = PGW_ATOMIC_LOAD(&stats->samples, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .accepted = PGW_ATOMIC_LOAD(&stats->accepted, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .backpressure = PGW_ATOMIC_LOAD(&stats->backpressure, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .invalid = PGW_ATOMIC_LOAD(&stats->invalid, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .fatal = PGW_ATOMIC_LOAD(&stats->fatal, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .clock_failures = PGW_ATOMIC_LOAD(&stats->clock_failures, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .total_ns = PGW_ATOMIC_LOAD(&stats->total_ns, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .minimum_ns = PGW_ATOMIC_LOAD(&stats->minimum_ns, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED),
        .maximum_ns = PGW_ATOMIC_LOAD(&stats->maximum_ns, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED)
    };
    for (size_t i = 0; i < PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS; ++i)
        snapshot.histogram[i] = PGW_ATOMIC_LOAD(&stats->histogram[i],
                                                     OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    if (!snapshot.timed_batches) snapshot.minimum_ns = 0;
    if (!OSAPI_Mutex_give(service->state_mutex)) return PGW_IO_ERROR;
    *out = snapshot;
    return PGW_OK;
}
#endif

static PGW_Status route_step(PGW_Service *s, PGW_Route *r)
{
    if (!service_state_take(s)) return PGW_IO_ERROR;
    PGW_EntityState lifecycle = (PGW_EntityState)PGW_ATOMIC_LOAD(
        &r->lifecycle, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
    if (lifecycle == PGW_FAULTED || lifecycle == PGW_PAUSED) {
        (void)service_state_give(s);
        return PGW_NO_CHANGE;
    }
    PGW_ATOMIC_STORE(&r->lifecycle, PGW_STARTED,
                     OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
    if (!service_state_give(s)) return PGW_IO_ERROR;
    size_t maximum = PGW_SampleSeq_get_maximum(&r->samples);
    size_t budget = s->sample_budget < maximum ? s->sample_budget : maximum;
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    uint64_t latency_start = 0;
    bool latency_start_valid = s->clock_ns(s->clock_state, &latency_start);
    if (!latency_start_valid) route_latency_clock_failure(r);
#endif
    PGW_Status status = r->reader.iface->read(r->reader.state, &r->samples, budget);
    size_t count = PGW_SampleSeq_get_length(&r->samples);
    if (status != PGW_OK) {
        if (count) {
            PGW_Status returned = r->reader.iface->return_loan(r->reader.state, &r->samples);
            (void)PGW_SampleSeq_set_length(&r->samples, 0);
            if (returned != PGW_OK) PGW_Counters_add(&r->counters, PGW_COUNT_LOAN_ERRORS, 1);
            return fail(r, PGW_LOAN_ERROR, "read failure with loan");
        }
        if (status == PGW_NO_DATA) return PGW_OK;
        return fail(r, status, "read");
    }
    PGW_Counters_add(&r->counters, PGW_COUNT_LOANS, 1);
    PGW_Counters_add(&r->counters, PGW_COUNT_RECEIVED, count);
    if (count > PGW_ATOMIC_LOAD(&r->counters.values[PGW_COUNT_HIGH_WATER], OSAPI_ATOMIC_MEMORY_ORDER_RELAXED))
        PGW_ATOMIC_STORE(&r->counters.values[PGW_COUNT_HIGH_WATER], count, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    bool outcomes_ready = count <= budget && PGW_WriteResultSeq_set_length(&r->results, count);
    bool fatal = !outcomes_ready;
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    bool writer_called = false;
    bool latency_finish_valid = false;
    uint64_t latency_finish = 0;
    PGW_Status latency_status = PGW_OK;
#endif
    if (outcomes_ready)
        for (size_t i = 0; i < count; ++i) {
            *PGW_WriteResultSeq_get_reference(&r->results, i) = PGW_WRITE_FATAL;
            if (!*PGW_SampleSeq_get_reference(&r->samples, (RTI_INT32)i)) fatal = true;
        }
    if (!fatal && count) {
        status = r->writer.iface->write(r->writer.state, &r->samples, &r->results);
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        writer_called = true;
        latency_finish_valid = s->clock_ns(s->clock_state, &latency_finish);
#endif
        if ((size_t)PGW_WriteResultSeq_get_length(&r->results) != count) fatal = true;
        if (status != PGW_OK && status != PGW_BACKPRESSURE && status != PGW_INVALID)
            fatal = true;
    }
    uint64_t backpressure = 0, invalid = 0, accepted = 0, fatal_outcomes = 0;
    if (outcomes_ready && (size_t)PGW_WriteResultSeq_get_length(&r->results) == count) {
        for (size_t i = 0; i < count; ++i) {
            PGW_CounterId id;
            switch (*PGW_WriteResultSeq_get_reference(&r->results, (RTI_INT32)i)) {
                case PGW_WRITE_ACCEPTED: id = PGW_COUNT_ACCEPTED; ++accepted; break;
                case PGW_WRITE_BACKPRESSURE: id = PGW_COUNT_BACKPRESSURE; ++backpressure; break;
                case PGW_WRITE_INVALID: id = PGW_COUNT_INVALID; ++invalid; break;
                default: id = PGW_COUNT_FATAL; ++fatal_outcomes; fatal = true; break;
            }
            PGW_Counters_add(&r->counters, id, 1);
        }
    }
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    if (writer_called) {
        route_latency_batch(r, count, accepted, backpressure, invalid, fatal_outcomes);
        if (!latency_finish_valid) route_latency_clock_failure(r);
        if (latency_start_valid && latency_finish_valid) {
            if (latency_finish >= latency_start)
                latency_status = route_latency_observe(
                    r, latency_finish - latency_start);
            else
                route_latency_clock_failure(r);
        }
    }
#endif
    PGW_Status returned = r->reader.iface->return_loan(r->reader.state, &r->samples);
    (void)PGW_SampleSeq_set_length(&r->samples, 0);
    PGW_Counters_add(&r->counters, PGW_COUNT_LOANS, UINT64_MAX);
    PGW_Status clock_status = event(s, r, PGW_BACKPRESSURE, backpressure);
    PGW_Status invalid_clock_status = event(s, r, PGW_INVALID, invalid);
    if (clock_status != PGW_OK || invalid_clock_status != PGW_OK) return PGW_IO_ERROR;
    if (returned != PGW_OK) {
        PGW_Counters_add(&r->counters, PGW_COUNT_LOAN_ERRORS, 1);
        (void)event(s, r, PGW_LOAN_ERROR, 1);
        return fail(r, returned, "return_loan");
    }
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    if (latency_status != PGW_OK)
        return fail(r, latency_status, "latency statistics lock");
#endif
    if (fatal) {
        (void)event(s, r, PGW_FATAL, 1);
        return fail(r, PGW_FATAL, "write or batch bounds");
    }
    return PGW_OK;
}

static PGW_Status route_ready_handler(void *context)
{
    PGW_Route *route = context;
    if (!route || !route->session || !route->session->service ||
        !route->ready_condition) return PGW_INVALID;
    PGW_Session *session = route->session;
    if (DDS_GuardCondition_set_trigger_value(route->ready_condition,
            DDS_BOOLEAN_FALSE) != DDS_RETCODE_OK)
        return PGW_IO_ERROR;
    PGW_EntityState lifecycle = (PGW_EntityState)PGW_ATOMIC_LOAD(
        &route->lifecycle, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
    if (lifecycle == PGW_FAULTED || lifecycle == PGW_PAUSED)
        return PGW_OK;
    uint64_t generation = (uint64_t)PGW_ATOMIC_LOAD(
        &route->ready_generation, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
    if (generation == (uint64_t)PGW_ATOMIC_LOAD(&route->dispatched_generation,
            OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE))
        return PGW_OK;

    PGW_Status status = route_step(session->service, route);
    if (status == PGW_NO_CHANGE) return PGW_OK;
    PGW_ATOMIC_STORE(&route->dispatched_generation, generation,
                     OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
    if (status != PGW_OK)
        PGW_ATOMIC_STORE(&session->error, status, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
    PGW_ATOMIC_ADD(&session->dispatched_routes, 1,
                   OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE);
    return status;
}

static PGW_Status session_attach(PGW_Session *session, DDS_Condition *condition)
{
    if (!condition ||
        DDS_WaitSet_attach_condition(session->async_waitset.waitset, condition) != DDS_RETCODE_OK)
        return PGW_IO_ERROR;
    ++session->async_waitset.attached_conditions;
    return PGW_OK;
}

static PGW_Status session_unsubscribe(PGW_Session *session)
{
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (session->service && session->service->control_listener_registered &&
        session->service->control_session == session) {
        PGW_Status status = session->service->control.iface->unregister_listener(
            session->service->control.state,
            &session->service->control_listener);
        if (status != PGW_OK) return status;
        session->service->control_listener_registered = false;
        session->service->control_listener = (PGW_ReaderListener){0};
    }
#endif
    for (RTI_INT32 i = 0; session->routes_initialized &&
         i < PGW_RouteSeq_get_length(&session->routes); ++i) {
        PGW_Route *route = PGW_RouteSeq_get_reference(&session->routes, i);
        if (!route->listener_registered) continue;
        PGW_Status status = route->reader.iface->unregister_listener(
            route->reader.state, &route->listener);
        if (status != PGW_OK) return status;
        route->listener_registered = false;
        route->listener = (PGW_ReaderListener){0};
    }
    return PGW_OK;
}

static PGW_Status session_initialize(PGW_Service *service, PGW_Session *session)
{
    size_t route_count = (size_t)PGW_RouteSeq_get_length(&session->routes);
    if (route_count >= INT32_MAX) return PGW_CAPACITY;
    session->service = service;
    session->cursor = 0;
    session->async_waitset.attached_conditions = 0;
    PGW_ATOMIC_INIT(&session->wakeups, 0);
    PGW_ATOMIC_INIT(&session->dispatched_routes, 0);
    PGW_ATOMIC_INIT(&session->stopping, false);
    PGW_ATOMIC_INIT(&session->error, PGW_OK);
    session->async_waitset.waitset = DDS_WaitSet_new();
    if (!session->async_waitset.waitset) return PGW_FATAL;
    session->async_waitset.wake_condition = DDS_GuardCondition_new();
    if (!session->async_waitset.wake_condition) return PGW_FATAL;
    if (!DDS_ConditionSeq_initialize(&session->async_waitset.active_conditions)) return PGW_FATAL;
    session->async_waitset.conditions_initialized = true;
    if (!DDS_ConditionSeq_set_maximum(
            &session->async_waitset.active_conditions,
            (RTI_INT32)(route_count + 1)))
        return PGW_CAPACITY;
    PGW_Status status = session_attach(session,
        DDS_GuardCondition_as_condition(session->async_waitset.wake_condition));
    if (status != PGW_OK) return status;
    session->async_waitset.wake_attached = true;
    session->async_waitset.initialized = true;
    if (DDS_GuardCondition_set_trigger_value(session->async_waitset.wake_condition,
            DDS_BOOLEAN_TRUE) != DDS_RETCODE_OK ||
        DDS_GuardCondition_set_trigger_value(session->async_waitset.wake_condition,
            DDS_BOOLEAN_FALSE) != DDS_RETCODE_OK)
        return PGW_IO_ERROR;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (service->control_resources && service->control_session == session) {
        status = service->control.iface->register_listener(
            service->control.state, &service->control_listener);
        if (status != PGW_OK) return status;
        service->control_listener_registered = true;
        if (PGW_ATOMIC_LOAD(&service->control_generation,
                            OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) != 0)
            session_signal_wake(session);
    }
#endif

    for (size_t i = 0; i < route_count; ++i) {
        PGW_Route *route =
            PGW_RouteSeq_get_reference(&session->routes, (RTI_INT32)i);
        PGW_ATOMIC_INIT(&route->ready_generation, 0);
        PGW_ATOMIC_INIT(&route->dispatched_generation, 0);
        if (!PGW_Counters_initialize(&route->counters)) {
            route->error = (PGW_Error){PGW_UNSUPPORTED, route->id,
                                       "counter backend"};
            route_state_store(route, PGW_FAULTED);
            return PGW_UNSUPPORTED;
        }
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        if (!route_latency_initialize(&route->latency)) {
            route->error = (PGW_Error){PGW_UNSUPPORTED, route->id,
                                       "latency counter backend"};
            route_state_store(route, PGW_FAULTED);
            return PGW_UNSUPPORTED;
        }
        route->latency_initialized = true;
#endif
        if (!route->storage_initialized || !route->samples_borrowed ||
            !route->results_borrowed ||
            !PGW_SampleSeq_get_maximum(&route->samples) ||
            PGW_SampleSeq_get_length(&route->samples) != 0 ||
            PGW_WriteResultSeq_get_maximum(&route->results) <
                PGW_SampleSeq_get_maximum(&route->samples) ||
            route_state(route) != PGW_UNINITIALIZED ||
            !route->reader.iface || !route->writer.iface ||
            route->reader.iface->version != PGW_ABI_VERSION ||
            route->writer.iface->version != PGW_ABI_VERSION ||
            route->reader.iface->size != sizeof(PGW_StreamReaderI) ||
            route->writer.iface->size != sizeof(PGW_StreamWriterI) ||
            !route->reader.iface->read || !route->reader.iface->return_loan ||
            !route->reader.iface->register_listener ||
            !route->reader.iface->unregister_listener ||
            !route->writer.iface->bind || !route->writer.iface->write ||
            !representation_valid(route->reader.representation) ||
            !representation_valid(route->writer.representation))
            return fail(route, PGW_INVALID, "validate");
        for (RTI_INT32 si = 0; si <=
             (RTI_INT32)(session - (PGW_Session *)PGW_SessionSeq_get_contiguous_buffer(
                 &service->sessions)); ++si) {
            PGW_Session *prior_session =
                PGW_SessionSeq_get_reference(&service->sessions, si);
            RTI_INT32 prior_count = PGW_RouteSeq_get_length(&prior_session->routes);
            for (RTI_INT32 ri = 0; ri < prior_count; ++ri) {
                PGW_Route *prior = PGW_RouteSeq_get_reference(
                    &prior_session->routes, ri);
                if (prior == route) break;
                if (prior->id == route->id) return fail(route, PGW_INVALID,
                                                         "duplicate route");
                if (prior->reader.state == route->reader.state &&
                    prior->reader.iface == route->reader.iface)
                    return fail(route, PGW_INVALID, "shared consuming reader");
                if (prior->writer.state == route->writer.state &&
                    prior->writer.iface == route->writer.iface &&
                    prior->reader.representation !=
                        route->reader.representation)
                    return fail(route, PGW_UNSUPPORTED,
                                "shared writer type support");
            }
            if (prior_session == session) break;
        }
        route->session = session;
        route->listener = (PGW_ReaderListener){route_data_available, route};
        status = route->writer.iface->bind(route->writer.state,
                                           route->reader.representation);
        if (status != PGW_OK) return fail(route, status, "bind");
        route->ready_condition = DDS_GuardCondition_new();
        if (!route->ready_condition) return fail(route, PGW_FATAL,
                                                  "ready condition");
        route->ready_handler = route_ready_handler;
        route->ready_context = route;
        status = session_attach(session,
            DDS_GuardCondition_as_condition(route->ready_condition));
        if (status != PGW_OK) return fail(route, status, "attach ready condition");
        route->ready_attached = true;
        status = route->reader.iface->register_listener(
            route->reader.state, &route->listener);
        if (status != PGW_OK) return fail(route, status, "register listener");
        route->listener_registered = true;
        route_state_store(route, PGW_ENABLED);
    }
    session_state_store(session, PGW_ENABLED);
    return PGW_OK;
}

static PGW_Status session_release(PGW_Session *session, bool release_routes)
{
    if (session->async_waitset.worker) return PGW_INVALID;
    PGW_Status status = session_unsubscribe(session);
    if (status != PGW_OK) return status;
    for (RTI_INT32 i = 0; session->routes_initialized &&
         i < PGW_RouteSeq_get_length(&session->routes); ++i) {
        PGW_Route *route = PGW_RouteSeq_get_reference(&session->routes, i);
        if (route->ready_attached && session->async_waitset.waitset &&
            DDS_WaitSet_detach_condition(session->async_waitset.waitset,
                DDS_GuardCondition_as_condition(route->ready_condition)) !=
                    DDS_RETCODE_OK)
            return PGW_IO_ERROR;
        route->ready_attached = false;
        route->ready_handler = NULL;
        route->ready_context = NULL;
        if (route->ready_condition) {
            if (DDS_GuardCondition_delete(route->ready_condition) != DDS_RETCODE_OK)
                return PGW_IO_ERROR;
            route->ready_condition = NULL;
        }
    }
    if (session->async_waitset.wake_attached && session->async_waitset.waitset &&
        DDS_WaitSet_detach_condition(session->async_waitset.waitset,
            DDS_GuardCondition_as_condition(session->async_waitset.wake_condition)) !=
                DDS_RETCODE_OK)
        return PGW_IO_ERROR;
    session->async_waitset.wake_attached = false;
    if (session->async_waitset.wake_condition) {
        if (DDS_GuardCondition_delete(session->async_waitset.wake_condition) != DDS_RETCODE_OK)
            return PGW_IO_ERROR;
        session->async_waitset.wake_condition = NULL;
    }
    if (session->async_waitset.conditions_initialized) {
        if (!DDS_ConditionSeq_finalize(&session->async_waitset.active_conditions))
            return PGW_LOAN_ERROR;
        session->async_waitset.conditions_initialized = false;
    }
    if (session->async_waitset.waitset) {
        if (DDS_WaitSet_delete(session->async_waitset.waitset) != DDS_RETCODE_OK)
            return PGW_IO_ERROR;
        session->async_waitset.waitset = NULL;
    }
    session->async_waitset.attached_conditions = 0;
    session->async_waitset.initialized = false;
    session->service = NULL;
    for (RTI_INT32 i = 0; session->routes_initialized &&
         i < PGW_RouteSeq_get_length(&session->routes); ++i) {
        PGW_Route *route = PGW_RouteSeq_get_reference(&session->routes, i);
        route->session = NULL;
        if (!release_routes) continue;
        if (route->storage_initialized && !route_storage_finalize(route))
            return PGW_LOAN_ERROR;
        if (route_state(route) != PGW_FAULTED)
            route_state_store(route, PGW_UNINITIALIZED);
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        route->latency_initialized = false;
#endif
    }
    if (release_routes && session->routes_initialized) {
        if (session->routes_borrowed) {
            if (!PGW_RouteSeq_unloan(&session->routes))
                return PGW_LOAN_ERROR;
            session->routes_borrowed = false;
        }
        if (!PGW_RouteSeq_finalize(&session->routes))
            return PGW_LOAN_ERROR;
        session->routes_initialized = false;
    }
    return PGW_OK;
}

PGW_Status PGW_Service_initialize(PGW_Service *service)
{
    if (!service || service_state(service) != PGW_UNINITIALIZED ||
        !service->sessions_initialized ||
        !PGW_SessionSeq_get_length(&service->sessions) ||
        !service->sample_budget
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        || !service->clock_ns
#endif
        ) return PGW_INVALID;
    if (!DDS_DomainParticipantFactory_get_instance()) return PGW_FATAL;
    if (service->state_mutex) return PGW_INVALID;
    service->state_mutex = OSAPI_Mutex_new();
    if (!service->state_mutex) return PGW_FATAL;
    service_state_store(service, PGW_INITIALIZING);
    PGW_Status status = PGW_OK;
    for (RTI_INT32 i = 0; i < PGW_SessionSeq_get_length(&service->sessions); ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        if (!session->name || !session->name[0] || !session->routes_initialized ||
            !PGW_RouteSeq_get_length(&session->routes) || session->async_waitset.initialized ||
            session_state(session) != PGW_UNINITIALIZED) {
            status = PGW_INVALID;
            break;
        }
        for (RTI_INT32 j = 0; j < i; ++j) {
            PGW_Session *prior =
                PGW_SessionSeq_get_reference(&service->sessions, j);
            if (!strcmp(session->name, prior->name)) {
                status = PGW_INVALID;
                break;
            }
        }
        if (status != PGW_OK) break;
        status = session_initialize(service, session);
        if (status != PGW_OK) {
            session_state_store(session, PGW_FAULTED);
            break;
        }
    }
    if (status != PGW_OK) {
        bool cleanup_complete = true;
        for (RTI_INT32 i = 0;
             i < PGW_SessionSeq_get_length(&service->sessions); ++i) {
            PGW_Status unsubscribed = session_unsubscribe(
                PGW_SessionSeq_get_reference(&service->sessions, i));
            if (unsubscribed != PGW_OK) {
                status = unsubscribed;
                cleanup_complete = false;
            }
        }
        for (RTI_INT32 i = 0; i < PGW_SessionSeq_get_length(&service->sessions); ++i) {
            PGW_Session *session =
                PGW_SessionSeq_get_reference(&service->sessions, i);
            if (!cleanup_complete) break;
            PGW_Status released = session_release(session, true);
            if (released != PGW_OK) {
                status = released;
                cleanup_complete = false;
                session_state_store(session, PGW_FAULTED);
            } else {
                session_state_store(session, PGW_UNINITIALIZED);
            }
        }
        if (cleanup_complete && service->state_mutex) {
            if (!OSAPI_Mutex_delete(service->state_mutex)) {
                status = PGW_FATAL;
                cleanup_complete = false;
            } else service->state_mutex = NULL;
        }
#if defined(PGW_ENABLE_REMOTE_CONTROL)
        if (cleanup_complete && service->control_mutex) {
            if (!OSAPI_Mutex_delete(service->control_mutex)) {
                status = PGW_FATAL;
                cleanup_complete = false;
            } else service->control_mutex = NULL;
        }
#endif
        if (cleanup_complete && service->sessions_borrowed) {
            if (!PGW_SessionSeq_unloan(&service->sessions)) {
                status = PGW_LOAN_ERROR;
                cleanup_complete = false;
            } else service->sessions_borrowed = false;
        }
        if (cleanup_complete &&
            !PGW_SessionSeq_finalize(&service->sessions)) {
            status = PGW_LOAN_ERROR;
            cleanup_complete = false;
        }
        if (cleanup_complete) service->sessions_initialized = false;
        service_state_store(service,
            cleanup_complete ? PGW_FAULTED : PGW_STOPPED);
        return status;
    }
    service_state_store(service, PGW_ENABLED);
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (service->control_resources) {
        control_initialize(service);
    }
#endif
    return PGW_OK;
}

#if defined(PGW_ENABLE_REMOTE_CONTROL)
static bool control_lock(PGW_Service *service)
{
    return service->control_mutex &&
           OSAPI_Mutex_take(service->control_mutex);
}

static bool control_unlock(PGW_Service *service)
{
    return service->control_mutex &&
           OSAPI_Mutex_give(service->control_mutex);
}

static const struct DDS_Duration_t *control_timeout(PGW_Service *service,
                                                   struct DDS_Duration_t *timeout)
{
    const uint64_t retry_period = UINT64_C(1000000000);
    uint64_t period = service->control_telemetry_period_ns ?
        service->control_telemetry_period_ns : retry_period;
    uint64_t remaining = period;
    if (service->control_telemetry_period_ns && service->clock_ns) {
        uint64_t now;
        if (service->clock_ns(service->clock_state, &now)) {
            if (!service->control_telemetry_clock_initialized ||
                now < service->control_telemetry_last_ns) {
                service->control_telemetry_last_ns = now;
                service->control_telemetry_clock_initialized = true;
            } else {
                uint64_t elapsed = now - service->control_telemetry_last_ns;
                remaining = elapsed >= period ? 0 : period - elapsed;
            }
        } else {
            ++service->control_counters.telemetry_clock_failures;
        }
    }
    timeout->sec = (DDS_Long)(remaining / UINT64_C(1000000000));
    timeout->nanosec = (DDS_UnsignedLong)(remaining % UINT64_C(1000000000));
    return timeout;
}

static void control_sync_all_routes(PGW_Service *service)
{
    for (RTI_INT32 i = 0; i < PGW_SessionSeq_get_length(&service->sessions); ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        for (RTI_INT32 j = 0; j < PGW_RouteSeq_get_length(&session->routes); ++j)
            control_sync_route(service,
                PGW_RouteSeq_get_reference(&session->routes, j));
    }
}
#endif

static RTI_BOOL session_thread_wakeup(struct OSAPI_ThreadInfo *info)
{
    PGW_Session *session = info->user_data;
    return session->async_waitset.wake_condition &&
        DDS_GuardCondition_set_trigger_value(session->async_waitset.wake_condition,
            DDS_BOOLEAN_TRUE) == DDS_RETCODE_OK ? RTI_TRUE : RTI_FALSE;
}

static bool session_condition_active(const PGW_Session *session,
                                     DDS_Condition *condition)
{
    RTI_INT32 length = DDS_ConditionSeq_get_length(
        &session->async_waitset.active_conditions);
    for (RTI_INT32 i = 0; i < length; ++i) {
        DDS_Condition **active = DDS_ConditionSeq_get_reference(
            &session->async_waitset.active_conditions, i);
        if (active && *active == condition) return true;
    }
    return false;
}

static RTI_BOOL session_thread_entry(struct OSAPI_ThreadInfo *info)
{
    PGW_Session *session = info->user_data;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    PGW_Service *service = session->service;
    uint64_t seen_control_generation = 0;
#endif
    while (!PGW_ATOMIC_LOAD(&session->stopping, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE)) {
#if defined(PGW_ENABLE_REMOTE_CONTROL)
        struct DDS_Duration_t timeout_storage;
        const struct DDS_Duration_t *timeout = &DDS_DURATION_INFINITE;
        bool control_session = service->control_resources &&
                              service->control_session == session;
        if (control_session) {
            if (!control_lock(service)) {
                session_fault(session, PGW_IO_ERROR);
                return RTI_FALSE;
            }
            timeout = control_timeout(service, &timeout_storage);
            if (!control_unlock(service)) {
                session_fault(session, PGW_IO_ERROR);
                return RTI_FALSE;
            }
        }
#else
        const struct DDS_Duration_t *timeout = &DDS_DURATION_INFINITE;
#endif
        if (!DDS_ConditionSeq_set_length(&session->async_waitset.active_conditions, 0)) {
            session_fault(session, PGW_FATAL);
            return RTI_FALSE;
        }
        DDS_ReturnCode_t waited = DDS_WaitSet_wait(
            session->async_waitset.waitset, &session->async_waitset.active_conditions, timeout);
        if (PGW_ATOMIC_LOAD(&session->stopping, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE))
            break;
        if (waited == DDS_RETCODE_TIMEOUT) {
#if defined(PGW_ENABLE_REMOTE_CONTROL)
            if (control_session) {
                if (!control_lock(service)) {
                    session_fault(session, PGW_IO_ERROR);
                    return RTI_FALSE;
                }
                control_retry_one_state(service);
                control_publish_telemetry(service);
                if (!control_unlock(service)) {
                    session_fault(session, PGW_IO_ERROR);
                    return RTI_FALSE;
                }
            }
#endif
            continue;
        }
        if (waited != DDS_RETCODE_OK) {
            session_fault(session, PGW_IO_ERROR);
            return RTI_FALSE;
        }
        PGW_ATOMIC_ADD(&session->wakeups, 1,
                       OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE);
        if (DDS_GuardCondition_set_trigger_value(session->async_waitset.wake_condition,
                DDS_BOOLEAN_FALSE) != DDS_RETCODE_OK) {
            session_fault(session, PGW_IO_ERROR);
            return RTI_FALSE;
        }
#if defined(PGW_ENABLE_REMOTE_CONTROL)
        if (control_session) {
            size_t processed = 0;
            bool rearm_failed = false;
            if (!control_lock(service)) {
                session_fault(session, PGW_IO_ERROR);
                return RTI_FALSE;
            }
            uint64_t control_generation = (uint64_t)PGW_ATOMIC_LOAD(
                &service->control_generation,
                OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
            if (control_generation != seen_control_generation) {
                seen_control_generation = control_generation;
                control_retry_one_state(service);
                processed = control_process_commands(service);
                if (processed == PGW_CONTROL_MAX_COMMANDS_PER_BATCH &&
                    service->control.iface->rearm_commands(
                        service->control.state) != PGW_OK) {
                    PGW_ATOMIC_STORE(&session->error, PGW_IO_ERROR,
                                     OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
                    rearm_failed = true;
                }
            }
            control_sync_all_routes(service);
            if (!control_unlock(service)) {
                session_fault(session, PGW_IO_ERROR);
                return RTI_FALSE;
            }
            if (rearm_failed) {
                session_state_store(session, PGW_FAULTED);
                return RTI_FALSE;
            }
        }
#endif
        size_t count = (size_t)PGW_RouteSeq_get_length(&session->routes);
        size_t start = session->cursor;
        for (size_t offset = 0; offset < count; ++offset) {
            size_t index = (start + offset) % count;
            PGW_Route *route =
                PGW_RouteSeq_get_reference(&session->routes, (RTI_INT32)index);
            if (!session_condition_active(session,
                    DDS_GuardCondition_as_condition(route->ready_condition)))
                continue;
            if (!route->ready_handler) {
                session_fault(session, PGW_INVALID);
                return RTI_FALSE;
            }
            PGW_Status handler_status =
                route->ready_handler(route->ready_context);
            if (handler_status != PGW_OK)
                PGW_ATOMIC_STORE(&session->error, handler_status,
                                      OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
        }
        session->cursor = (start + 1) % count;
    }
    return RTI_TRUE;
}

PGW_Status PGW_Service_start(PGW_Service *service)
{
    if (!service || service_state(service) != PGW_ENABLED ||
        !service->sessions_initialized) return PGW_INVALID;
    struct OSAPI_ThreadProperty property = OSAPI_ThreadProperty_INITIALIZER;
    RTI_INT32 count = PGW_SessionSeq_get_length(&service->sessions);
    for (RTI_INT32 i = 0; i < count; ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        PGW_ATOMIC_STORE(&session->stopping, false, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
        session_state_store(session, PGW_STARTED);
        session->async_waitset.worker = OSAPI_Thread_create("pgw-session", &property,
            session_thread_entry, session, session_thread_wakeup);
        if (!session->async_waitset.worker || !OSAPI_Thread_start(session->async_waitset.worker)) {
            if (session->async_waitset.worker) {
                if (OSAPI_Thread_destroy(session->async_waitset.worker))
                    session->async_waitset.worker = NULL;
            }
            PGW_Status stopped = PGW_Service_stop(service);
            return stopped == PGW_OK ? PGW_FATAL : stopped;
        }
        session->async_waitset.started = true;
    }
    service_state_store(service, PGW_STARTED);
    return PGW_OK;
}

PGW_Status PGW_Service_stop(PGW_Service *service)
{
    if (!service || (service_state(service) != PGW_ENABLED &&
                     service_state(service) != PGW_STARTED))
        return PGW_INVALID;
    PGW_Status result = PGW_OK;
    RTI_INT32 count = PGW_SessionSeq_get_length(&service->sessions);
    for (RTI_INT32 i = 0; i < count; ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        PGW_ATOMIC_STORE(&session->stopping, true, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
        if (session->async_waitset.started &&
            DDS_GuardCondition_set_trigger_value(session->async_waitset.wake_condition,
                DDS_BOOLEAN_TRUE) != DDS_RETCODE_OK)
            result = PGW_IO_ERROR;
    }
    for (RTI_INT32 i = 0; i < count; ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        if (session->async_waitset.worker) {
            if (!OSAPI_Thread_destroy(session->async_waitset.worker)) {
                result = PGW_IO_ERROR;
                continue;
            }
            session->async_waitset.worker = NULL;
            session->async_waitset.started = false;
        }
    }
    for (RTI_INT32 i = 0; i < count; ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        if (session->async_waitset.worker) return PGW_IO_ERROR;
    }
    for (RTI_INT32 i = 0; i < count; ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        if (session_state(session) != PGW_FAULTED)
            session_state_store(session, PGW_STOPPED);
        for (RTI_INT32 j = 0; j < PGW_RouteSeq_get_length(&session->routes); ++j) {
            PGW_Route *route =
                PGW_RouteSeq_get_reference(&session->routes, j);
            if (route_state(route) != PGW_FAULTED)
                route_state_store(route, PGW_STOPPED);
        }
    }
    service_state_store(service, PGW_STOPPED);
    return result;
}

PGW_Status PGW_Service_finalize(PGW_Service *service)
{
    if (!service || !service->sessions_initialized ||
        (service_state(service) != PGW_STOPPED &&
         service_state(service) != PGW_UNINITIALIZED)) return PGW_INVALID;
    bool configured_only = service_state(service) == PGW_UNINITIALIZED;
    for (RTI_INT32 i = 0; i < PGW_SessionSeq_get_length(&service->sessions); ++i) {
        PGW_Status unsubscribed = session_unsubscribe(
            PGW_SessionSeq_get_reference(&service->sessions, i));
        if (unsubscribed != PGW_OK) return unsubscribed;
    }
    if (service->state_mutex) {
        if (!OSAPI_Mutex_delete(service->state_mutex)) return PGW_FATAL;
        service->state_mutex = NULL;
    }
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (service->control_mutex) {
        if (!OSAPI_Mutex_delete(service->control_mutex)) return PGW_FATAL;
        service->control_mutex = NULL;
    }
#endif
    for (RTI_INT32 i = 0; i < PGW_SessionSeq_get_length(&service->sessions); ++i) {
        PGW_Session *session =
            PGW_SessionSeq_get_reference(&service->sessions, i);
        if (configured_only && session->async_waitset.initialized) return PGW_INVALID;
        PGW_Status released = session_release(session, true);
        if (released != PGW_OK) return released;
        session_state_store(session, PGW_UNINITIALIZED);
    }
    if (service->sessions_borrowed) {
        if (!PGW_SessionSeq_unloan(&service->sessions)) return PGW_LOAN_ERROR;
        service->sessions_borrowed = false;
    }
    if (!PGW_SessionSeq_finalize(&service->sessions)) return PGW_LOAN_ERROR;
    service->sessions_initialized = false;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    service->control = (PGW_ControlEndpoint){0};
    service->control_session = NULL;
    service->control_resources = NULL;
    service->control_resource_count = 0;
    service->control_state_retry_cursor = 0;
    service->control_telemetry_metrics = NULL;
    service->control_telemetry_metric_count = 0;
    service->control_telemetry_period_ns = 0;
    service->control_telemetry_last_ns = 0;
    service->control_telemetry_clock_initialized = false;
#endif
    service_state_store(service, PGW_UNINITIALIZED);
    return PGW_OK;
}

const char *PGW_status_name(PGW_Status status)
{
    static const char *const names[] = {"OK", "NO_DATA", "BACKPRESSURE", "INVALID",
        "UNSUPPORTED", "CAPACITY", "IO_ERROR", "LOAN_ERROR", "FATAL", "NO_CHANGE"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}

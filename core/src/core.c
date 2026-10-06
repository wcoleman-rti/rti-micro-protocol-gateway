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

#include "pgw/core.h"
#include <limits.h>
#include <string.h>

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
                                    bool diagnostics, size_t events, PGW_CoreResourceReport *out)
{
    if (!capacities) return PGW_INVALID;
    size_t routes = (size_t)PGW_SizeSeq_get_length(capacities);
    if (!routes || !out || (!diagnostics && events)) return PGW_INVALID;
    PGW_CoreResourceReport report = {0};
    size_t route_bytes, references = 0;
    if (!PGW_size_multiply(routes, sizeof(PGW_Route), &route_bytes) ||
        !PGW_size_add(sizeof(PGW_Service), route_bytes, &report.objects_bytes))
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

bool PGW_schema_equal(const PGW_Schema *a, const PGW_Schema *b)
{
    return a && b && a->name && a->name[0] && b->name && b->name[0] &&
           a->fingerprint && a->fingerprint[0] && b->fingerprint && b->fingerprint[0] &&
           a->version == b->version && !strcmp(a->name, b->name) &&
           !strcmp(a->fingerprint, b->fingerprint);
}

static bool representation_valid(const PGW_Representation *b)
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
        PGW_schema_equal(b->schema, b->schema) &&
        access_valid && view_valid;
}

PGW_Status PGW_Registry_initialize(PGW_Registry *r, const PGW_AdapterSeq *adapters,
                                  const PGW_RepresentationSeq *bindings)
{
    if (!r || !adapters || !bindings || r->initialized) return PGW_INVALID;
    RTI_INT32 adapter_capacity = PGW_AdapterSeq_get_maximum(adapters);
    RTI_INT32 binding_capacity = PGW_RepresentationSeq_get_maximum(bindings);
    RTI_INT32 adapter_length = PGW_AdapterSeq_get_length(adapters);
    RTI_INT32 binding_length = PGW_RepresentationSeq_get_length(bindings);
    PGW_AdapterRef *adapter_storage = PGW_AdapterSeq_get_contiguous_buffer(adapters);
    PGW_RepresentationRef *binding_storage = PGW_RepresentationSeq_get_contiguous_buffer(bindings);
    if (adapter_capacity <= 0 || binding_capacity <= 0 ||
        adapter_length < 0 || adapter_length > adapter_capacity ||
        binding_length < 0 || binding_length > binding_capacity ||
        !adapter_storage || !binding_storage) return PGW_INVALID;
    r->frozen = false;
    if (!PGW_AdapterSeq_initialize(&r->adapters)) return PGW_FATAL;
    if (!PGW_RepresentationSeq_initialize(&r->bindings)) {
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
    if (!PGW_RepresentationSeq_loan_contiguous(&r->bindings, binding_storage,
                                               binding_length, binding_capacity)) {
        (void)PGW_Registry_finalize(r);
        return PGW_INVALID;
    }
    r->bindings_borrowed = true;
    return PGW_OK;
}

PGW_Status PGW_Registry_finalize(PGW_Registry *r)
{
    if (!r || !r->initialized) return PGW_INVALID;
    bool adapters = !r->adapters_borrowed || PGW_AdapterSeq_unloan(&r->adapters);
    bool bindings = !r->bindings_borrowed || PGW_RepresentationSeq_unloan(&r->bindings);
    if (adapters) adapters = PGW_AdapterSeq_finalize(&r->adapters);
    if (bindings) bindings = PGW_RepresentationSeq_finalize(&r->bindings);
    if (!adapters || !bindings) return PGW_LOAN_ERROR;
    r->frozen = false;
    r->initialized = false;
    r->adapters_borrowed = false;
    r->bindings_borrowed = false;
    return PGW_OK;
}

PGW_Status PGW_Registry_register_adapter(PGW_Registry *r, const PGW_AdapterI *a)
{
    if (!r || !a || r->frozen || a->version != PGW_ABI_VERSION ||
        a->size != sizeof(*a) || !a->name || !a->name[0] || !a->create || !a->connection ||
        a->connection->version != PGW_ABI_VERSION ||
        a->connection->size != sizeof(PGW_ConnectionI) ||
        (!a->connection->reader && !a->connection->writer) ||
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

PGW_Status PGW_Registry_register_binding(PGW_Registry *r, const PGW_Representation *b)
{
    if (!r || r->frozen || !representation_valid(b) ||
        !r->initialized) return PGW_INVALID;
    if (PGW_Registry_find_binding(r, b->name)) return PGW_INVALID;
    RTI_INT32 count = PGW_RepresentationSeq_get_length(&r->bindings);
    if (count == PGW_RepresentationSeq_get_maximum(&r->bindings)) return PGW_CAPACITY;
    if (!PGW_RepresentationSeq_set_length(&r->bindings, count + 1)) return PGW_CAPACITY;
    *PGW_RepresentationSeq_get_reference(&r->bindings, count) = b;
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

const PGW_Representation *PGW_Registry_find_binding(const PGW_Registry *r, const char *name)
{
    if (!r || !r->initialized || !name) return NULL;
    for (RTI_INT32 i = 0; i < PGW_RepresentationSeq_get_length(&r->bindings); ++i) {
        PGW_RepresentationRef binding = *PGW_RepresentationSeq_get_reference(&r->bindings, i);
        if (!strcmp(binding->name, name)) return binding;
    }
    return NULL;
}

PGW_Status PGW_Route_initialize_storage(PGW_Route *r, const PGW_SampleSeq *samples,
                                       const PGW_WriteResultSeq *results)
{
    if (!r || r->lifecycle != PGW_UNINITIALIZED || !samples || !results ||
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

PGW_Status PGW_Route_pause(PGW_Route *r)
{
    if (!r || !r->storage_initialized) return PGW_INVALID;
    if (r->lifecycle == PGW_FAULTED) return PGW_FATAL;
    if (r->lifecycle == PGW_PAUSED) return PGW_NO_CHANGE;
    if (r->lifecycle != PGW_READY && r->lifecycle != PGW_RUNNING)
        return PGW_INVALID;
    r->lifecycle = PGW_PAUSED;
    return PGW_OK;
}

PGW_Status PGW_Route_resume(PGW_Route *r)
{
    if (!r || !r->storage_initialized) return PGW_INVALID;
    if (r->lifecycle == PGW_FAULTED) return PGW_FATAL;
    if (r->lifecycle == PGW_PAUSED) {
        r->lifecycle = PGW_READY;
        return PGW_OK;
    }
    if (r->lifecycle == PGW_READY || r->lifecycle == PGW_RUNNING)
        return PGW_NO_CHANGE;
    return PGW_INVALID;
}

PGW_Status PGW_Service_set_routes(PGW_Service *s, const PGW_RouteSeq *routes)
{
    if (!s || s->lifecycle != PGW_UNINITIALIZED || !routes ||
        s->routes_initialized) return PGW_INVALID;
    RTI_INT32 count = PGW_RouteSeq_get_length(routes);
    RTI_INT32 capacity = PGW_RouteSeq_get_maximum(routes);
    PGW_Route *route_storage = PGW_RouteSeq_get_contiguous_buffer(routes);
    if (count <= 0 || capacity < count || !route_storage) return PGW_INVALID;
    if (!PGW_RouteSeq_initialize(&s->routes)) return PGW_FATAL;
    if (!PGW_RouteSeq_loan_contiguous(&s->routes, route_storage, count, capacity)) {
        (void)PGW_RouteSeq_finalize(&s->routes);
        return PGW_INVALID;
    }
    s->routes_initialized = true;
    return PGW_OK;
}

#if defined(PGW_ENABLE_REMOTE_CONTROL)
static PGW_ControlResource *control_find_resource(PGW_Service *, uint32_t);

static bool control_route_is_configured(const PGW_Service *service, const PGW_Route *route)
{
    for (RTI_INT32 i = 0; i < PGW_RouteSeq_get_length(&service->routes); ++i)
        if (PGW_RouteSeq_get_reference(&service->routes, i) == route) return true;
    return false;
}

PGW_Status PGW_Service_set_control(PGW_Service *service, PGW_ControlEndpoint endpoint,
                                  PGW_ControlResource *resources, size_t count)
{
    const uint32_t action_bits = (UINT32_C(1) << 8) - 1;
    if (!service || service->lifecycle != PGW_UNINITIALIZED ||
        !service->routes_initialized || !resources || !count ||
        service->control_resources) return PGW_INVALID;
    if (!endpoint.iface || endpoint.iface->version != PGW_CONTROL_ABI_VERSION ||
        endpoint.iface->size != sizeof(PGW_ControlEndpointI) ||
        !endpoint.iface->take_command || !endpoint.iface->write_state ||
        !endpoint.iface->write_result) return PGW_INVALID;
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
    service->control = endpoint;
    service->control_resources = resources;
    service->control_resource_count = count;
    service->control_state_retry_cursor = 0;
    service->control_counters = (PGW_ControlCounters){0};
    return PGW_OK;
}

PGW_Status PGW_Service_control_counters(const PGW_Service *service,
                                       PGW_ControlCounters *out)
{
    if (!service || !out || !service->control_resources) return PGW_INVALID;
    *out = service->control_counters;
    return PGW_OK;
}

PGW_Status PGW_Service_set_telemetry(PGW_Service *service,
                                    PGW_ControlTelemetryMetric *metrics,
                                    size_t count, uint32_t period_ms,
                                    uint32_t minimum_period_ms)
{
    if (!service || service->lifecycle != PGW_UNINITIALIZED ||
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
    if (route->lifecycle == PGW_FAULTED) return PGW_CONTROL_STATUS_FAULTED;
    if (route->lifecycle == PGW_PAUSED) return PGW_CONTROL_STATUS_PAUSED;
    if (route->lifecycle == PGW_READY || route->lifecycle == PGW_RUNNING)
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

static void control_process_commands(PGW_Service *service)
{
    for (size_t i = 0; i < PGW_CONTROL_MAX_COMMANDS_PER_STEP; ++i) {
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
    r->lifecycle = PGW_FAULTED;
    PGW_Counters_add(&r->counters, PGW_COUNT_ROUTE_FAULTS, 1);
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
    atomic_init(&stats->batches, 0);
    atomic_init(&stats->timed_batches, 0);
    atomic_init(&stats->samples, 0);
    atomic_init(&stats->accepted, 0);
    atomic_init(&stats->backpressure, 0);
    atomic_init(&stats->invalid, 0);
    atomic_init(&stats->fatal, 0);
    atomic_init(&stats->clock_failures, 0);
    atomic_init(&stats->total_ns, 0);
    atomic_init(&stats->minimum_ns, UINT64_MAX);
    atomic_init(&stats->maximum_ns, 0);
    for (size_t i = 0; i < PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS; ++i)
        atomic_init(&stats->histogram[i], 0);
    if (!atomic_is_lock_free(&stats->batches) ||
        !atomic_is_lock_free(&stats->total_ns) ||
        !atomic_is_lock_free(&stats->histogram[0]))
        return false;
    return true;
}

static void route_latency_clock_failure(PGW_Route *route)
{
    atomic_fetch_add_explicit(&route->latency.clock_failures, 1,
                              memory_order_relaxed);
}

static void route_latency_minimum(PGW_RouteLatencyStats *stats, uint64_t value)
{
    uint_fast64_t current = atomic_load_explicit(&stats->minimum_ns,
                                                memory_order_relaxed);
    while (value < current &&
           !atomic_compare_exchange_weak_explicit(&stats->minimum_ns, &current,
               value, memory_order_relaxed, memory_order_relaxed)) {}
}

static void route_latency_maximum(PGW_RouteLatencyStats *stats, uint64_t value)
{
    uint_fast64_t current = atomic_load_explicit(&stats->maximum_ns,
                                                memory_order_relaxed);
    while (value > current &&
           !atomic_compare_exchange_weak_explicit(&stats->maximum_ns, &current,
               value, memory_order_relaxed, memory_order_relaxed)) {}
}

static void route_latency_observe(PGW_Route *route, uint64_t duration_ns)
{
    unsigned bucket = 0;
    uint64_t upper_bound = 1000;
    while (bucket + 1 < PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS &&
           duration_ns > upper_bound) {
        upper_bound <<= 1;
        ++bucket;
    }
    PGW_RouteLatencyStats *stats = &route->latency;
    atomic_fetch_add_explicit(&stats->timed_batches, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->total_ns, duration_ns, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->histogram[bucket], 1, memory_order_relaxed);
    route_latency_minimum(stats, duration_ns);
    route_latency_maximum(stats, duration_ns);
}

static void route_latency_batch(PGW_Route *route, size_t samples,
                                uint64_t accepted, uint64_t backpressure,
                                uint64_t invalid, uint64_t fatal)
{
    PGW_RouteLatencyStats *stats = &route->latency;
    atomic_fetch_add_explicit(&stats->batches, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->samples, samples, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->accepted, accepted, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->backpressure, backpressure, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->invalid, invalid, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->fatal, fatal, memory_order_relaxed);
}

PGW_Status PGW_Route_latency_snapshot(const PGW_Route *route,
                                     PGW_RouteLatencySnapshot *out)
{
    if (!route || !out || !route->latency_initialized) return PGW_INVALID;
    const PGW_RouteLatencyStats *stats = &route->latency;
    PGW_RouteLatencySnapshot snapshot = {
        .batches = atomic_load_explicit(&stats->batches, memory_order_relaxed),
        .timed_batches = atomic_load_explicit(&stats->timed_batches, memory_order_relaxed),
        .samples = atomic_load_explicit(&stats->samples, memory_order_relaxed),
        .accepted = atomic_load_explicit(&stats->accepted, memory_order_relaxed),
        .backpressure = atomic_load_explicit(&stats->backpressure, memory_order_relaxed),
        .invalid = atomic_load_explicit(&stats->invalid, memory_order_relaxed),
        .fatal = atomic_load_explicit(&stats->fatal, memory_order_relaxed),
        .clock_failures = atomic_load_explicit(&stats->clock_failures, memory_order_relaxed),
        .total_ns = atomic_load_explicit(&stats->total_ns, memory_order_relaxed),
        .minimum_ns = atomic_load_explicit(&stats->minimum_ns, memory_order_relaxed),
        .maximum_ns = atomic_load_explicit(&stats->maximum_ns, memory_order_relaxed)
    };
    for (size_t i = 0; i < PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS; ++i)
        snapshot.histogram[i] = atomic_load_explicit(&stats->histogram[i],
                                                     memory_order_relaxed);
    if (!snapshot.timed_batches) snapshot.minimum_ns = 0;
    *out = snapshot;
    return PGW_OK;
}
#endif

PGW_Status PGW_Service_initialize(PGW_Service *s)
{
    if (!s || s->lifecycle != PGW_UNINITIALIZED || !s->routes_initialized ||
        !PGW_RouteSeq_get_length(&s->routes) || !s->route_budget || !s->sample_budget
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        || !s->clock_ns
#endif
        ) return PGW_INVALID;
    s->lifecycle = PGW_INITIALIZING;
    s->cursor = 0;
    size_t count = PGW_RouteSeq_get_length(&s->routes);
    PGW_Status status = PGW_OK;
    for (size_t i = 0; i < count; ++i) {
        PGW_Route *r = PGW_RouteSeq_get_reference(&s->routes, i);
        if (!PGW_Counters_initialize(&r->counters)) {
            status = PGW_UNSUPPORTED;
            r->error = (PGW_Error){status, r->id, "counter backend"};
            r->lifecycle = PGW_FAULTED;
            break;
        }
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        if (!route_latency_initialize(&r->latency)) {
            status = PGW_UNSUPPORTED;
            r->error = (PGW_Error){status, r->id, "latency counter backend"};
            r->lifecycle = PGW_FAULTED;
            break;
        }
        r->latency_initialized = true;
#endif
        if (!r->storage_initialized || !r->samples_borrowed || !r->results_borrowed ||
            !PGW_SampleSeq_get_maximum(&r->samples) ||
            PGW_SampleSeq_get_length(&r->samples) != 0 ||
            PGW_WriteResultSeq_get_maximum(&r->results) < PGW_SampleSeq_get_maximum(&r->samples) ||
            r->lifecycle != PGW_UNINITIALIZED || !r->reader.iface || !r->writer.iface ||
            r->reader.iface->version != PGW_ABI_VERSION ||
            r->writer.iface->version != PGW_ABI_VERSION ||
            r->reader.iface->size != sizeof(PGW_StreamReaderI) ||
            r->writer.iface->size != sizeof(PGW_StreamWriterI) ||
            !r->reader.iface->read || !r->reader.iface->return_loan ||
            !r->writer.iface->bind || !r->writer.iface->write ||
            !representation_valid(r->reader.representation) ||
            !representation_valid(r->writer.representation)) {
            status = fail(r, PGW_INVALID, "validate"); break;
        }
        for (size_t j = 0; j < i; ++j)
            if (PGW_RouteSeq_get_reference(&s->routes, (RTI_INT32)j)->id == r->id) status = PGW_INVALID;
        if (status != PGW_OK) { fail(r, status, "duplicate route"); break; }
        status = r->writer.iface->bind(r->writer.state, r->reader.representation);
        if (status != PGW_OK) { fail(r, status, "bind"); break; }
        r->lifecycle = PGW_READY;
    }
    if (status != PGW_OK) {
        for (size_t i = 0; i < count; ++i) {
            PGW_Route *r = PGW_RouteSeq_get_reference(&s->routes, i);
            if (!route_storage_finalize(r)) status = PGW_LOAN_ERROR;
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
            r->latency_initialized = false;
#endif
            if (r->lifecycle != PGW_FAULTED) r->lifecycle = PGW_UNINITIALIZED;
        }
        if (!PGW_RouteSeq_unloan(&s->routes) || !PGW_RouteSeq_finalize(&s->routes))
            status = PGW_LOAN_ERROR;
        else s->routes_initialized = false;
        s->lifecycle = PGW_FAULTED;
        return status;
    }
    s->lifecycle = PGW_READY;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    control_initialize(s);
#endif
    return PGW_OK;
}

static PGW_Status route_step(PGW_Service *s, PGW_Route *r)
{
    r->lifecycle = PGW_RUNNING;
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
    if (count > atomic_load_explicit(&r->counters.values[PGW_COUNT_HIGH_WATER], memory_order_relaxed))
        atomic_store_explicit(&r->counters.values[PGW_COUNT_HIGH_WATER], count, memory_order_relaxed);
    bool outcomes_ready = count <= budget && PGW_WriteResultSeq_set_length(&r->results, count);
    bool fatal = !outcomes_ready;
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    bool writer_called = false;
    bool latency_finish_valid = false;
    uint64_t latency_finish = 0;
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
                route_latency_observe(r, latency_finish - latency_start);
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
    if (fatal) {
        (void)event(s, r, PGW_FATAL, 1);
        return fail(r, PGW_FATAL, "write or batch bounds");
    }
    return PGW_OK;
}

PGW_Status PGW_Service_step(PGW_Service *s)
{
    if (!s || (s->lifecycle != PGW_READY && s->lifecycle != PGW_RUNNING))
        return PGW_INVALID;
    s->lifecycle = PGW_RUNNING;
    PGW_Status result = PGW_OK;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (s->control_resources) {
        control_retry_one_state(s);
        control_process_commands(s);
        control_publish_telemetry(s);
    }
#endif
    size_t count = PGW_RouteSeq_get_length(&s->routes);
    size_t work = s->route_budget < count ? s->route_budget : count;
    for (size_t i = 0; i < work; ++i) {
        PGW_Route *r = PGW_RouteSeq_get_reference(&s->routes, s->cursor);
        s->cursor = (s->cursor + 1) % count;
        if (r->lifecycle == PGW_FAULTED || r->lifecycle == PGW_PAUSED) continue;
        PGW_Status status = route_step(s, r);
#if defined(PGW_ENABLE_REMOTE_CONTROL)
        if (s->control_resources) control_sync_route(s, r);
#endif
        if (status != PGW_OK) result = status;
    }
    return result;
}

PGW_Status PGW_Service_stop(PGW_Service *s)
{
    if (!s || (s->lifecycle != PGW_READY && s->lifecycle != PGW_RUNNING))
        return PGW_INVALID;
    s->lifecycle = PGW_STOPPED;
    for (RTI_INT32 i = 0; i < PGW_RouteSeq_get_length(&s->routes); ++i) {
        PGW_Route *r = PGW_RouteSeq_get_reference(&s->routes, i);
        if (r->lifecycle != PGW_FAULTED) r->lifecycle = PGW_STOPPED;
    }
    return PGW_OK;
}

PGW_Status PGW_Service_finalize(PGW_Service *s)
{
    if (!s || s->lifecycle != PGW_STOPPED) return PGW_INVALID;
    PGW_Status status = PGW_OK;
    for (RTI_INT32 i = 0; i < PGW_RouteSeq_get_length(&s->routes); ++i) {
        PGW_Route *r = PGW_RouteSeq_get_reference(&s->routes, i);
        if (!route_storage_finalize(r))
            status = fail(r, PGW_LOAN_ERROR, "sequence finalize");
        else r->lifecycle = PGW_UNINITIALIZED;
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        r->latency_initialized = false;
#endif
    }
    if (!PGW_RouteSeq_unloan(&s->routes) || !PGW_RouteSeq_finalize(&s->routes))
        status = PGW_LOAN_ERROR;
    else s->routes_initialized = false;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    s->control = (PGW_ControlEndpoint){0};
    s->control_resources = NULL;
    s->control_resource_count = 0;
    s->control_state_retry_cursor = 0;
    s->control_telemetry_metrics = NULL;
    s->control_telemetry_metric_count = 0;
    s->control_telemetry_period_ns = 0;
    s->control_telemetry_last_ns = 0;
    s->control_telemetry_clock_initialized = false;
#endif
    s->lifecycle = status == PGW_OK ? PGW_UNINITIALIZED : PGW_FAULTED;
    return status;
}

const char *PGW_status_name(PGW_Status status)
{
    static const char *const names[] = {"OK", "NO_DATA", "BACKPRESSURE", "INVALID",
        "UNSUPPORTED", "CAPACITY", "IO_ERROR", "LOAN_ERROR", "FATAL", "NO_CHANGE"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}

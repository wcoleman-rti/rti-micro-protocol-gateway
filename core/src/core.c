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
    return b && b->name && b->name[0] && b->sample_size && b->sample_alignment &&
        !(b->sample_alignment & (b->sample_alignment - 1)) &&
        PGW_schema_equal(b->schema, b->schema) &&
        (!b->access || (b->access->version == PGW_ABI_VERSION &&
                       b->access->size == sizeof(PGW_SampleAccessI)));
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

PGW_Status PGW_Service_initialize(PGW_Service *s)
{
    if (!s || s->lifecycle != PGW_UNINITIALIZED || !s->routes_initialized ||
        !PGW_RouteSeq_get_length(&s->routes) || !s->route_budget || !s->sample_budget) return PGW_INVALID;
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
            !representation_valid(r->writer.representation) ||
            !PGW_schema_equal(r->reader.representation->schema,
                              r->writer.representation->schema)) {
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
            if (r->lifecycle != PGW_FAULTED) r->lifecycle = PGW_UNINITIALIZED;
        }
        if (!PGW_RouteSeq_unloan(&s->routes) || !PGW_RouteSeq_finalize(&s->routes))
            status = PGW_LOAN_ERROR;
        else s->routes_initialized = false;
        s->lifecycle = PGW_FAULTED;
        return status;
    }
    s->lifecycle = PGW_READY;
    return PGW_OK;
}

static PGW_Status route_step(PGW_Service *s, PGW_Route *r)
{
    r->lifecycle = PGW_RUNNING;
    size_t maximum = PGW_SampleSeq_get_maximum(&r->samples);
    size_t budget = s->sample_budget < maximum ? s->sample_budget : maximum;
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
    if (outcomes_ready)
        for (size_t i = 0; i < count; ++i) {
            *PGW_WriteResultSeq_get_reference(&r->results, i) = PGW_WRITE_FATAL;
            if (!*PGW_SampleSeq_get_reference(&r->samples, (RTI_INT32)i)) fatal = true;
        }
    if (!fatal && count) {
        status = r->writer.iface->write(r->writer.state, &r->samples, &r->results);
        if ((size_t)PGW_WriteResultSeq_get_length(&r->results) != count) fatal = true;
        if (status != PGW_OK && status != PGW_BACKPRESSURE && status != PGW_INVALID)
            fatal = true;
    }
    uint64_t backpressure = 0, invalid = 0;
    if (outcomes_ready && (size_t)PGW_WriteResultSeq_get_length(&r->results) == count) {
        for (size_t i = 0; i < count; ++i) {
            PGW_CounterId id;
            switch (*PGW_WriteResultSeq_get_reference(&r->results, (RTI_INT32)i)) {
                case PGW_WRITE_ACCEPTED: id = PGW_COUNT_ACCEPTED; break;
                case PGW_WRITE_BACKPRESSURE: id = PGW_COUNT_BACKPRESSURE; ++backpressure; break;
                case PGW_WRITE_INVALID: id = PGW_COUNT_INVALID; ++invalid; break;
                default: id = PGW_COUNT_FATAL; fatal = true; break;
            }
            PGW_Counters_add(&r->counters, id, 1);
        }
    }
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
    size_t count = PGW_RouteSeq_get_length(&s->routes);
    size_t work = s->route_budget < count ? s->route_budget : count;
    for (size_t i = 0; i < work; ++i) {
        PGW_Route *r = PGW_RouteSeq_get_reference(&s->routes, s->cursor);
        s->cursor = (s->cursor + 1) % count;
        if (r->lifecycle == PGW_FAULTED) continue;
        PGW_Status status = route_step(s, r);
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
    }
    if (!PGW_RouteSeq_unloan(&s->routes) || !PGW_RouteSeq_finalize(&s->routes))
        status = PGW_LOAN_ERROR;
    else s->routes_initialized = false;
    s->lifecycle = status == PGW_OK ? PGW_UNINITIALIZED : PGW_FAULTED;
    return status;
}

const char *PGW_status_name(PGW_Status status)
{
    static const char *const names[] = {"OK", "NO_DATA", "BACKPRESSURE", "INVALID",
        "UNSUPPORTED", "CAPACITY", "IO_ERROR", "LOAN_ERROR", "FATAL"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "UNKNOWN";
}

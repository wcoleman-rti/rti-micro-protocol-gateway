#include "fake.h"
#include <string.h>
#include <stdlib.h>

static PGW_Status copy_value(const PGW_Sample *sample, void *out, size_t bytes)
{
    if (!sample || !out || bytes != sizeof(PGW_TestValue)) return PGW_INVALID;
    memcpy(out, sample, bytes);
    return PGW_OK;
}

static const PGW_Schema schema = {"test.counter", 1, "integer-key-u64-v1"};
static const PGW_SampleAccessI sample_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), copy_value, NULL
};
const PGW_Representation PGW_test_representation = {
    &schema, "test.counter.native", sizeof(PGW_TestValue), _Alignof(PGW_TestValue), &sample_access
};

static PGW_Status read_samples(void *state, PGW_SampleSeq *seq, size_t budget)
{
    PGW_TestReader *r = state;
    if (r->loaned) return PGW_LOAN_ERROR;
    if (r->read_status != PGW_OK) return r->read_status;
    if (!r->available && !r->empty_ok) return PGW_NO_DATA;
    size_t count = r->available < budget ? r->available : budget;
    if (count > 4 || !PGW_SampleSeq_set_length(seq, count)) return PGW_CAPACITY;
    for (size_t i = 0; i < count; ++i)
        *PGW_SampleSeq_get_reference(seq, i) = (const PGW_Sample *)&r->values[i];
    r->loaned = true;
    ++r->borrows;
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

static PGW_Status bind(void *state, const PGW_Representation *representation)
{
    PGW_TestWriter *w = state;
    if (!representation->access || !representation->access->copy_value) return PGW_UNSUPPORTED;
    w->source = representation;
    return PGW_OK;
}

static PGW_Status write_samples(void *state, const PGW_SampleSeq *seq,
                                PGW_WriteResultSeq *results)
{
    PGW_TestWriter *w = state;
    size_t count = PGW_SampleSeq_get_length(seq);
    if (count != (size_t)PGW_WriteResultSeq_get_length(results)) return PGW_INVALID;
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
    PGW_ABI_VERSION, sizeof(PGW_StreamReaderI), read_samples, return_samples
};
const PGW_StreamWriterI PGW_test_writer_iface = {
    PGW_ABI_VERSION, sizeof(PGW_StreamWriterI), bind, write_samples
};

void PGW_test_route(PGW_Route *route, uint32_t id, PGW_TestReader *reader,
                    PGW_TestWriter *writer, PGW_SampleRef *refs,
                    PGW_WriteResult *results, size_t capacity)
{
    *route = (PGW_Route){
        .id = id,
        .reader = {reader, &PGW_test_reader_iface, &PGW_test_representation},
        .writer = {writer, &PGW_test_writer_iface, &PGW_test_representation}
    };
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

PGW_Status PGW_test_service_set_routes(PGW_Service *service, PGW_Route *routes,
                                       size_t count)
{
    PGW_RouteSeq sequence;
    if (!routes || !count || count > INT32_MAX || !PGW_RouteSeq_initialize(&sequence))
        return PGW_INVALID;
    if (!PGW_RouteSeq_loan_contiguous(&sequence, routes, (RTI_INT32)count,
                                      (RTI_INT32)count)) {
        (void)PGW_RouteSeq_finalize(&sequence);
        return PGW_CAPACITY;
    }
    PGW_Status status = PGW_Service_set_routes(service, &sequence);
    if (!PGW_RouteSeq_unloan(&sequence) || !PGW_RouteSeq_finalize(&sequence))
        return PGW_LOAN_ERROR;
    return status;
}

PGW_Status PGW_test_registry_initialize(PGW_Registry *registry,
                                        PGW_AdapterRef *adapters, size_t adapter_capacity,
                                        PGW_RepresentationRef *bindings, size_t binding_capacity)
{
    PGW_AdapterSeq adapter_sequence;
    PGW_RepresentationSeq binding_sequence;
    if (adapter_capacity > INT32_MAX || binding_capacity > INT32_MAX ||
        !PGW_AdapterSeq_initialize(&adapter_sequence) ||
        !PGW_RepresentationSeq_initialize(&binding_sequence)) return PGW_INVALID;
    if (!PGW_AdapterSeq_loan_contiguous(&adapter_sequence, adapters, 0,
                                         (RTI_INT32)adapter_capacity) ||
        !PGW_RepresentationSeq_loan_contiguous(&binding_sequence, bindings, 0,
                                                (RTI_INT32)binding_capacity)) {
        (void)PGW_AdapterSeq_unloan(&adapter_sequence);
        (void)PGW_AdapterSeq_finalize(&adapter_sequence);
        (void)PGW_RepresentationSeq_unloan(&binding_sequence);
        (void)PGW_RepresentationSeq_finalize(&binding_sequence);
        return PGW_CAPACITY;
    }
    PGW_Status status = PGW_Registry_initialize(registry, &adapter_sequence, &binding_sequence);
    (void)PGW_AdapterSeq_unloan(&adapter_sequence);
    (void)PGW_AdapterSeq_finalize(&adapter_sequence);
    (void)PGW_RepresentationSeq_unloan(&binding_sequence);
    (void)PGW_RepresentationSeq_finalize(&binding_sequence);
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

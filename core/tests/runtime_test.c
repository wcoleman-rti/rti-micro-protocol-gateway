#include "pgw/runtime.h"
#include "osapi/osapi_system.h"
#include "osapi/osapi_thread.h"
#include <stdio.h>
#ifdef PGW_TEST_ALLOCATION_PROBE
#include "allocation.h"
#endif

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "check failed: %s (%d)\n", #test, __LINE__); return 1; \
} } while (0)

#ifdef PGW_HAS_RUNNER
static PGW_Status read_empty(void *context, PGW_SampleSeq *samples, size_t budget)
{
    atomic_uint *calls = context;
    (void)samples;
    (void)budget;
    atomic_fetch_add_explicit(calls, 1, memory_order_relaxed);
    return PGW_NO_DATA;
}

static PGW_Status return_empty(void *context, PGW_SampleSeq *samples)
{
    (void)context;
    (void)samples;
    return PGW_OK;
}

static PGW_Status bind_empty(void *context, const PGW_Representation *representation)
{
    (void)context;
    (void)representation;
    return PGW_OK;
}

static PGW_Status write_empty(void *context, const PGW_SampleSeq *samples,
    PGW_WriteResultSeq *results)
{
    (void)context;
    if (samples == NULL || results == NULL) {
        return PGW_INVALID;
    }
    RTI_INT32 length = PGW_SampleSeq_get_length(samples);
    if (length < 0 || PGW_WriteResultSeq_get_length(results) != length) {
        return PGW_INVALID;
    }
    for (RTI_INT32 i = 0; i < length; ++i) {
        PGW_WriteResult *result = PGW_WriteResultSeq_get_reference(results, i);
        if (result == NULL) {
            return PGW_INVALID;
        }
        *result = PGW_WRITE_ACCEPTED;
    }
    return PGW_OK;
}
#endif

int main(void)
{
    uint64_t before = 0, after = 0;
    CHECK(PGW_Runtime_initialize());
    CHECK(!PGW_Runtime_monotonic_time_ns(NULL));
    CHECK(PGW_Runtime_monotonic_time_ns(&before));
    for (unsigned i = 0; i < 100 && after <= before; ++i) {
        OSAPI_Thread_sleep(10);
        CHECK(PGW_Runtime_monotonic_time_ns(&after));
    }
    CHECK(after > before);
#ifdef PGW_HAS_RUNNER
    PGW_Runner runner = {0};
    atomic_uint calls;
    atomic_init(&calls, 0);
    const PGW_Schema schema = {"runner-test", 1, "runner-test-v1"};
    const PGW_Representation representation = {
        &schema, "runner-test", sizeof(int), _Alignof(int), NULL};
    const PGW_StreamReaderI reader = {
        PGW_ABI_VERSION, sizeof(PGW_StreamReaderI), read_empty, return_empty};
    const PGW_StreamWriterI writer = {
        PGW_ABI_VERSION, sizeof(PGW_StreamWriterI), bind_empty, write_empty};
    PGW_SampleRef references[1];
    PGW_WriteResult results[1];
    PGW_SampleSeq sample_storage;
    PGW_WriteResultSeq result_storage;
    PGW_RouteSeq route_storage;
    PGW_Route route = {
        .id = 1, .reader = {&calls, &reader, &representation},
        .writer = {NULL, &writer, &representation}};
    PGW_Service service = {
        .route_budget = 1, .sample_budget = 1,
        .clock_ns = PGW_Runtime_monotonic_clock};
    CHECK(PGW_SampleSeq_initialize(&sample_storage));
    CHECK(PGW_SampleSeq_loan_contiguous(&sample_storage, references, 0, 1));
    CHECK(PGW_WriteResultSeq_initialize(&result_storage));
    CHECK(PGW_WriteResultSeq_loan_contiguous(&result_storage, results, 0, 1));
    CHECK(PGW_Route_initialize_storage(&route, &sample_storage, &result_storage) == PGW_OK);
    CHECK(PGW_RouteSeq_initialize(&route_storage));
    CHECK(PGW_RouteSeq_loan_contiguous(&route_storage, &route, 1, 1));
    CHECK(PGW_Service_set_routes(&service, &route_storage) == PGW_OK);
    CHECK(!PGW_Runner_initialize(&runner, &service, 0, 0));
#ifdef PGW_TEST_ALLOCATION_PROBE
    PGW_allocation_monitor(true);
#endif
    CHECK(PGW_Runner_initialize(&runner, &service, 1, 0));
#ifdef PGW_TEST_ALLOCATION_PROBE
    uint64_t initialization_allocations = PGW_osapi_allocation_calls();
    PGW_allocation_monitor(false);
    CHECK(initialization_allocations > 0);
#endif
    CHECK(service.lifecycle == PGW_READY);
    CHECK(runner.thread != NULL);
    OSAPI_Thread_sleep(3);
    CHECK(atomic_load_explicit(&calls, memory_order_relaxed) == 0);
#ifdef PGW_TEST_ALLOCATION_PROBE
    PGW_allocation_monitor(true);
#endif
    CHECK(PGW_Runner_start(&runner));
    CHECK(!PGW_Runner_start(&runner));
    for (unsigned i = 0; i < 100 &&
         atomic_load_explicit(&calls, memory_order_relaxed) < 2; ++i) {
        OSAPI_Thread_sleep(1);
    }
    CHECK(atomic_load_explicit(&calls, memory_order_relaxed) >= 2);
    CHECK(PGW_Runner_stop(&runner));
    CHECK(service.lifecycle == PGW_STOPPED);
    CHECK(atomic_load_explicit(&runner.last_step_status, memory_order_acquire) == PGW_OK);
    unsigned stopped = atomic_load_explicit(&calls, memory_order_relaxed);
    OSAPI_Thread_sleep(3);
    CHECK(atomic_load_explicit(&calls, memory_order_relaxed) == stopped);
    CHECK(!PGW_Runner_start(&runner));
#ifdef PGW_TEST_ALLOCATION_PROBE
    uint64_t runtime_allocations = PGW_allocation_calls();
    uint64_t runtime_osapi_allocations = PGW_osapi_allocation_calls();
    PGW_allocation_monitor(false);
    CHECK(runtime_allocations == 0);
    CHECK(runtime_osapi_allocations == 0);
#endif
    CHECK(PGW_Runner_finalize(&runner));
    CHECK(PGW_Service_finalize(&service) == PGW_OK);
    CHECK(PGW_RouteSeq_unloan(&route_storage));
    CHECK(PGW_RouteSeq_finalize(&route_storage));
    CHECK(PGW_SampleSeq_unloan(&sample_storage));
    CHECK(PGW_SampleSeq_finalize(&sample_storage));
    CHECK(PGW_WriteResultSeq_unloan(&result_storage));
    CHECK(PGW_WriteResultSeq_finalize(&result_storage));
    CHECK(PGW_RouteSeq_get_maximum(&service.routes) == 0);
    CHECK(PGW_SampleSeq_get_maximum(&route.samples) == 0);
    CHECK(PGW_WriteResultSeq_get_maximum(&route.results) == 0);
    CHECK(PGW_SampleSeq_initialize(&sample_storage));
    CHECK(PGW_SampleSeq_loan_contiguous(&sample_storage, references, 0, 1));
    CHECK(PGW_WriteResultSeq_initialize(&result_storage));
    CHECK(PGW_WriteResultSeq_loan_contiguous(&result_storage, results, 0, 1));
    CHECK(PGW_Route_initialize_storage(&route, &sample_storage, &result_storage) == PGW_OK);
    CHECK(PGW_RouteSeq_initialize(&route_storage));
    CHECK(PGW_RouteSeq_loan_contiguous(&route_storage, &route, 1, 1));
    CHECK(PGW_Service_set_routes(&service, &route_storage) == PGW_OK);
    CHECK(PGW_Runner_initialize(&runner, &service, 1, 0));
    CHECK(PGW_Runner_finalize(&runner));
    CHECK(PGW_Service_finalize(&service) == PGW_OK);
    CHECK(PGW_RouteSeq_unloan(&route_storage));
    CHECK(PGW_RouteSeq_finalize(&route_storage));
    CHECK(PGW_SampleSeq_unloan(&sample_storage));
    CHECK(PGW_SampleSeq_finalize(&sample_storage));
    CHECK(PGW_WriteResultSeq_unloan(&result_storage));
    CHECK(PGW_WriteResultSeq_finalize(&result_storage));
#endif
    CHECK(OSAPI_System_finalize());
    return 0;
}

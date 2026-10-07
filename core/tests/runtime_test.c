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
#include "pgw/runtime.h"
#include "osapi/osapi_mutex.h"
#include "osapi/osapi_system.h"
#include "osapi/osapi_thread.h"
#include <stdio.h>
#ifdef PGW_TEST_ALLOCATION_PROBE
#include "allocation.h"
#endif

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "check failed: %s (%d)\n", #test, __LINE__); return 1; \
} } while (0)

typedef struct {
    OSAPI_Mutex_T *mutex;
    const PGW_ReaderListener *listener;
    PGW_ATOMIC(RTI_UINT32) read_calls;
    PGW_ATOMIC(RTI_UINT32) notify_stopping;
} ReaderState;

static PGW_Status read_empty(void *opaque, PGW_SampleSeq *samples, size_t budget)
{
    ReaderState *reader = opaque;
    (void)samples;
    (void)budget;
    PGW_ATOMIC_ADD(&reader->read_calls, 1, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED);
    return PGW_NO_DATA;
}

static PGW_Status return_empty(void *opaque, PGW_SampleSeq *samples)
{
    (void)opaque;
    (void)samples;
    return PGW_OK;
}

static PGW_Status register_listener(void *opaque,
                                    const PGW_ReaderListener *listener)
{
    ReaderState *reader = opaque;
    if (!listener || !listener->on_data_available ||
        !OSAPI_Mutex_take(reader->mutex)) return PGW_INVALID;
    if (reader->listener) {
        return OSAPI_Mutex_give(reader->mutex) ? PGW_INVALID : PGW_IO_ERROR;
    }
    reader->listener = listener;
    return OSAPI_Mutex_give(reader->mutex) ? PGW_OK : PGW_IO_ERROR;
}

static PGW_Status unregister_listener(void *opaque,
                                      const PGW_ReaderListener *listener)
{
    ReaderState *reader = opaque;
    if (!listener || !OSAPI_Mutex_take(reader->mutex)) return PGW_INVALID;
    if (reader->listener != listener) {
        return OSAPI_Mutex_give(reader->mutex) ? PGW_INVALID : PGW_IO_ERROR;
    }
    reader->listener = NULL;
    return OSAPI_Mutex_give(reader->mutex) ? PGW_OK : PGW_IO_ERROR;
}

static PGW_Status bind_empty(void *context,
                             const PGW_SampleRepresentation *representation)
{
    (void)context;
    (void)representation;
    return PGW_OK;
}

static PGW_Status write_empty(void *context, const PGW_SampleSeq *samples,
                              PGW_WriteResultSeq *results)
{
    (void)context;
    RTI_INT32 length = PGW_SampleSeq_get_length(samples);
    if (length < 0 || PGW_WriteResultSeq_get_length(results) != length)
        return PGW_INVALID;
    for (RTI_INT32 i = 0; i < length; ++i)
        *PGW_WriteResultSeq_get_reference(results, i) = PGW_WRITE_ACCEPTED;
    return PGW_OK;
}

static RTI_BOOL notify_until_stopped(struct OSAPI_ThreadInfo *info)
{
    ReaderState *reader = info->user_data;
    while (!info->stop_thread &&
           !PGW_ATOMIC_LOAD(&reader->notify_stopping,
                                 OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE)) {
        if (!OSAPI_Mutex_take(reader->mutex)) return RTI_FALSE;
        const PGW_ReaderListener *listener = reader->listener;
        if (listener) listener->on_data_available(listener->context);
        if (!OSAPI_Mutex_give(reader->mutex)) return RTI_FALSE;
        OSAPI_Thread_sleep(1);
    }
    return RTI_TRUE;
}

static RTI_BOOL notify_wakeup(struct OSAPI_ThreadInfo *info)
{
    ReaderState *reader = info->user_data;
    PGW_ATOMIC_STORE(&reader->notify_stopping, true,
                          OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
    return RTI_TRUE;
}

int main(void)
{
    uint64_t before = 0, after = 0;
    CHECK(PGW_Runtime_initialize());
    CHECK(DDS_DomainParticipantFactory_get_instance() != NULL);
    CHECK(!PGW_Runtime_monotonic_time_ns(NULL));
    CHECK(PGW_Runtime_monotonic_time_ns(&before));
    for (unsigned i = 0; i < 100 && after <= before; ++i) {
        OSAPI_Thread_sleep(10);
        CHECK(PGW_Runtime_monotonic_time_ns(&after));
    }
    CHECK(after > before);

    ReaderState reader = {0};
    reader.mutex = OSAPI_Mutex_new();
    CHECK(reader.mutex != NULL);
    PGW_ATOMIC_INIT(&reader.read_calls, 0);
    PGW_ATOMIC_INIT(&reader.notify_stopping, false);
    const PGW_TypeInfo schema = {"runtime-test", 1, "runtime-test-v1"};
    const PGW_SampleRepresentation representation = {
        &schema, "runtime-test", sizeof(int), _Alignof(int), NULL, NULL
    };
    const PGW_StreamReaderI reader_ops = {
        .version = PGW_ABI_VERSION,
        .size = sizeof(PGW_StreamReaderI),
        .read = read_empty,
        .return_loan = return_empty,
        .register_listener = register_listener,
        .unregister_listener = unregister_listener
    };
    const PGW_StreamWriterI writer_ops = {
        .version = PGW_ABI_VERSION,
        .size = sizeof(PGW_StreamWriterI),
        .bind = bind_empty,
        .write = write_empty
    };
    PGW_SampleRef references[1];
    PGW_WriteResult results[1];
    PGW_SampleSeq sample_storage;
    PGW_WriteResultSeq result_storage;
    PGW_Route route = {
        .id = 1,
        .reader = {&reader, &reader_ops, &representation},
        .writer = {NULL, &writer_ops, &representation}
    };
    PGW_Session session = {.name = "runtime"};
    PGW_Service service = {
        .sample_budget = 1,
        .clock_ns = PGW_Runtime_monotonic_clock
    };
    CHECK(PGW_SampleSeq_initialize(&sample_storage));
    CHECK(PGW_SampleSeq_loan_contiguous(&sample_storage, references, 0, 1));
    CHECK(PGW_WriteResultSeq_initialize(&result_storage));
    CHECK(PGW_WriteResultSeq_loan_contiguous(&result_storage, results, 0, 1));
    CHECK(PGW_Route_initialize_storage(&route, &sample_storage,
                                       &result_storage) == PGW_OK);

    PGW_RouteSeq route_storage;
    CHECK(PGW_RouteSeq_initialize(&route_storage));
    CHECK(PGW_RouteSeq_loan_contiguous(&route_storage, &route, 1, 1));
    CHECK(PGW_Session_set_routes(&session, &route_storage) == PGW_OK);
    CHECK(PGW_RouteSeq_unloan(&route_storage));
    CHECK(PGW_RouteSeq_finalize(&route_storage));
    PGW_SessionSeq session_storage;
    CHECK(PGW_SessionSeq_initialize(&session_storage));
    CHECK(PGW_SessionSeq_loan_contiguous(&session_storage, &session, 1, 1));
    CHECK(PGW_Service_set_sessions(&service, &session_storage) == PGW_OK);
    CHECK(PGW_SessionSeq_unloan(&session_storage));
    CHECK(PGW_SessionSeq_finalize(&session_storage));
    CHECK(PGW_Service_initialize(&service) == PGW_OK);
    CHECK(PGW_Service_start(&service) == PGW_OK);
    OSAPI_Thread_sleep(10);
    CHECK(PGW_ATOMIC_LOAD(&reader.read_calls, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED) == 0);

    struct OSAPI_ThreadProperty property = OSAPI_ThreadProperty_INITIALIZER;
    struct OSAPI_Thread *notifier = OSAPI_Thread_create(
        "pgw-runtime-test-notifier", &property, notify_until_stopped, &reader,
        notify_wakeup);
    CHECK(notifier != NULL);
    CHECK(OSAPI_Thread_start(notifier));
#ifdef PGW_TEST_ALLOCATION_PROBE
    PGW_allocation_monitor(true);
#endif
    for (unsigned i = 0; i < 100 &&
         PGW_ATOMIC_LOAD(&reader.read_calls, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED) < 1; ++i)
        OSAPI_Thread_sleep(1);
    CHECK(PGW_ATOMIC_LOAD(&reader.read_calls, OSAPI_ATOMIC_MEMORY_ORDER_RELAXED) > 0);
    CHECK(PGW_Service_stop(&service) == PGW_OK);
    CHECK(PGW_ATOMIC_LOAD(&service.lifecycle,
                          OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) == PGW_STOPPED);
    PGW_ATOMIC_STORE(&reader.notify_stopping, true, OSAPI_ATOMIC_MEMORY_ORDER_RELEASE);
    CHECK(OSAPI_Thread_destroy(notifier));
    CHECK(PGW_Service_finalize(&service) == PGW_OK);
#ifdef PGW_TEST_ALLOCATION_PROBE
    uint64_t runtime_allocations = PGW_allocation_calls();
    uint64_t runtime_osapi_allocations = PGW_osapi_allocation_calls();
    PGW_allocation_monitor(false);
    CHECK(runtime_allocations == 0);
    CHECK(runtime_osapi_allocations == 0);
#endif
    CHECK(OSAPI_Mutex_take(reader.mutex));
    CHECK(reader.listener == NULL);
    CHECK(OSAPI_Mutex_give(reader.mutex));
    CHECK(PGW_SampleSeq_unloan(&sample_storage));
    CHECK(PGW_SampleSeq_finalize(&sample_storage));
    CHECK(PGW_WriteResultSeq_unloan(&result_storage));
    CHECK(PGW_WriteResultSeq_finalize(&result_storage));
    CHECK(OSAPI_Mutex_delete(reader.mutex));
    CHECK(OSAPI_System_finalize());
    return 0;
}

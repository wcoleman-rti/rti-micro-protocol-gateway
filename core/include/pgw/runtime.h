#ifndef PGW_RUNTIME_H
#define PGW_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Initialize Micro's process-global OSAPI before using the clock or runner.
 * Finalization is the application's responsibility after all Micro users stop. */
bool PGW_Runtime_initialize(void);
bool PGW_Runtime_monotonic_time_ns(uint64_t *nanoseconds);
bool PGW_Runtime_monotonic_clock(void *context, uint64_t *nanoseconds);

#ifdef PGW_HAS_RUNNER
#include <stdatomic.h>
#include "pgw/core.h"
struct OSAPI_Thread;

typedef struct PGW_Runner {
    struct OSAPI_Thread *thread;
    void *wake;
    void *ready;
    PGW_Service *service;
    uint32_t period_ms;
    atomic_bool stopping;
    atomic_bool running;
    atomic_int last_step_status;
    bool initialized;
    bool started;
} PGW_Runner;

/* Use a zero-initialized object. Initialization provisions and starts a native
 * thread, waits at most one second for its startup handshake, then initializes
 * the configured UNINITIALIZED service to READY. The thread is blocked until
 * start; start never creates a thread or allocates.
 * Lifecycle calls are caller-serialized and must not run from the callback.
 * stop joins the thread then stops the service; service finalization remains
 * caller-owned. The service is exclusively owned by this
 * runner until stop. Restart requires service and runner reinitialization. */
bool PGW_Runner_initialize(PGW_Runner *runner,
    PGW_Service *service, uint32_t period_ms, uint32_t stack_size);
bool PGW_Runner_start(PGW_Runner *runner);
bool PGW_Runner_stop(PGW_Runner *runner);
bool PGW_Runner_finalize(PGW_Runner *runner);
#endif
#endif

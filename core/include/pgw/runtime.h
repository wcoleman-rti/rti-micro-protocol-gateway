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

#ifndef PGW_RUNTIME_H
#define PGW_RUNTIME_H

/** @addtogroup pgw_core_api
 * @{
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Initialize Micro's process-global OSAPI runtime.
 * Call before using the monotonic clock or runner. Finalization of OSAPI is
 * the application's responsibility after all Micro users have stopped.
 * @return True when OSAPI initialization succeeds; false otherwise.
 */
bool PGW_Runtime_initialize(void);
/** @brief Read the OSAPI monotonic clock in nanoseconds.
 * @param nanoseconds Receives the clock value on success.
 * @return True on success; false for a null output pointer or clock failure.
 */
bool PGW_Runtime_monotonic_time_ns(uint64_t *nanoseconds);
/** @brief Clock callback adapter for PGW_Service.
 * @param context Unused; may be null.
 * @param nanoseconds Receives monotonic time in nanoseconds.
 * @return Same result as PGW_Runtime_monotonic_time_ns().
 */
bool PGW_Runtime_monotonic_clock(void *context, uint64_t *nanoseconds);

#ifdef PGW_HAS_RUNNER
#include <stdatomic.h>
#include "pgw/core.h"
struct OSAPI_Thread;

/** @brief Periodic native-thread runner for a gateway service.
 *
 * Initialize a zero-initialized object. Initialization provisions and starts
 * the native thread, waits up to one second for its startup handshake, then
 * initializes the configured UNINITIALIZED service to READY. The thread waits
 * until start is called; start does not create a thread or allocate memory.
 *
 * Lifecycle calls must be serialized by the caller and must not be made from
 * the service callback/thread. While active, the runner exclusively owns use
 * of the service. Stop joins the thread and stops the service, but does not
 * finalize service storage. Restart requires reinitializing both service and
 * runner objects.
 */
typedef struct PGW_Runner {
    struct OSAPI_Thread *thread;  /**< Internal native thread handle. */
    void *wake;                   /**< Internal wake semaphore. */
    void *ready;                  /**< Internal startup handshake semaphore. */
    PGW_Service *service;         /**< Borrowed service controlled by this runner. */
    uint32_t period_ms;           /**< Period between service steps while running. */
    atomic_bool stopping;         /**< Internal stop request. */
    atomic_bool running;          /**< Internal run state. */
    atomic_int last_step_status;  /**< Most recently returned service-step status. */
    bool initialized;              /**< Internal initialization state. */
    bool started;                  /**< Whether start succeeded for this lifetime. */
} PGW_Runner;

/** @brief Create the runner thread and initialize its service.
 * @param runner Zero-initialized runner storage.
 * @param service Configured service in PGW_UNINITIALIZED state; borrowed until
 *        stop/finalize has completed.
 * @param period_ms Nonzero step period, no greater than INT32_MAX.
 * @param stack_size Native thread stack size passed to OSAPI.
 * @return True on success. False indicates invalid state/period, native thread
 *         or semaphore setup failure, startup timeout, or service initialization
 *         failure. Initialization failures clean up runner-owned resources when
 *         possible.
 */
bool PGW_Runner_initialize(PGW_Runner *runner,
    PGW_Service *service, uint32_t period_ms, uint32_t stack_size);
/** @brief Begin periodic service stepping on the runner thread.
 * Does not allocate or create a thread. May be called only once after successful
 * initialization and before stopping.
 * @return True if the service begins running; false for invalid lifecycle state
 *         or wakeup failure.
 */
bool PGW_Runner_start(PGW_Runner *runner);
/** @brief Stop the runner thread and stop its service.
 * Joins the thread before returning. It does not finalize the service's
 * caller-owned storage.
 * @return True on success; false for invalid state, join failure, or service
 *         stop failure.
 */
bool PGW_Runner_stop(PGW_Runner *runner);
/** @brief Stop if necessary and release runner-owned thread/semaphore resources.
 * The service remains caller-owned and must be finalized separately.
 * @return True on success; false if stopping or resource release fails.
 */
bool PGW_Runner_finalize(PGW_Runner *runner);
#endif
/** @} */
#endif

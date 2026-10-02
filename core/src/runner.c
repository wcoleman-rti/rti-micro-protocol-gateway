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

#include "pgw/runtime.h"
#include "osapi/osapi_thread.h"
#include "osapi/osapi_semaphore.h"

static RTI_BOOL runner_wakeup(struct OSAPI_ThreadInfo *info)
{
    PGW_Runner *runner = info->user_data;
    return OSAPI_Semaphore_give(runner->wake);
}

static RTI_BOOL runner_entry(struct OSAPI_ThreadInfo *info)
{
    PGW_Runner *runner = info->user_data;
    RTI_INT32 reason = 0;
    if (!OSAPI_Semaphore_give(runner->ready)) {
        return RTI_FALSE;
    }
    while (!atomic_load_explicit(&runner->stopping, memory_order_acquire)) {
        RTI_INT32 wait_ms = atomic_load_explicit(&runner->running, memory_order_acquire)
            ? (RTI_INT32)runner->period_ms : OSAPI_SEMAPHORE_TIMEOUT_INFINITE;
        RTI_BOOL awakened = OSAPI_Semaphore_take(runner->wake, wait_ms, &reason);
        if (!awakened && reason != OSAPI_SEMAPHORE_RESULT_TIMEOUT) {
            atomic_store_explicit(&runner->running, false, memory_order_release);
            atomic_store_explicit(&runner->stopping, true, memory_order_release);
            return RTI_FALSE;
        }
        if (atomic_load_explicit(&runner->running, memory_order_acquire) &&
            !atomic_load_explicit(&runner->stopping, memory_order_acquire)) {
            PGW_Status status = PGW_Service_step(runner->service);
            atomic_store_explicit(&runner->last_step_status, status, memory_order_release);
        }
    }
    return RTI_TRUE;
}

bool PGW_Runner_initialize(PGW_Runner *runner,
    PGW_Service *service, uint32_t period_ms, uint32_t stack_size)
{
    struct OSAPI_ThreadProperty property = OSAPI_ThreadProperty_INITIALIZER;
    if (runner == NULL || runner->initialized || service == NULL ||
        service->lifecycle != PGW_UNINITIALIZED ||
        period_ms == 0 || period_ms > INT32_MAX) {
        return false;
    }
    runner->service = service;
    runner->period_ms = period_ms;
    runner->started = false;
    atomic_init(&runner->stopping, false);
    atomic_init(&runner->running, false);
    atomic_init(&runner->last_step_status, PGW_OK);
    runner->wake = OSAPI_Semaphore_new();
    if (runner->wake == NULL) {
        return false;
    }
    runner->ready = OSAPI_Semaphore_new();
    if (runner->ready == NULL) {
        RTI_BOOL deleted = OSAPI_Semaphore_delete(runner->wake);
        (void)deleted;
        runner->wake = NULL;
        return false;
    }
    property.stack_size = stack_size;
    runner->thread = OSAPI_Thread_create("pgw-runner", &property,
        runner_entry, runner, runner_wakeup);
    if (runner->thread == NULL) {
        RTI_BOOL deleted = OSAPI_Semaphore_delete(runner->wake);
        RTI_BOOL ready_deleted = OSAPI_Semaphore_delete(runner->ready);
        (void)deleted;
        (void)ready_deleted;
        runner->wake = NULL;
        runner->ready = NULL;
        return false;
    }
    runner->initialized = true;
    if (!OSAPI_Thread_start(runner->thread)) {
        runner->service = NULL;
        (void)PGW_Runner_finalize(runner);
        return false;
    }
    RTI_INT32 reason = 0;
    if (!OSAPI_Semaphore_take(runner->ready, 1000, &reason) ||
        PGW_Service_initialize(service) != PGW_OK) {
        (void)PGW_Runner_finalize(runner);
        return false;
    }
    return true;
}

bool PGW_Runner_start(PGW_Runner *runner)
{
    if (runner == NULL || !runner->initialized || runner->started ||
        runner->thread == NULL ||
        atomic_load_explicit(&runner->stopping, memory_order_acquire) ||
        runner->service->lifecycle != PGW_READY) {
        return false;
    }
    atomic_store_explicit(&runner->running, true, memory_order_release);
    if (!OSAPI_Semaphore_give(runner->wake)) {
        atomic_store_explicit(&runner->running, false, memory_order_release);
        return false;
    }
    runner->started = true;
    return true;
}

bool PGW_Runner_stop(PGW_Runner *runner)
{
    if (runner == NULL || !runner->initialized) {
        return false;
    }
    atomic_store_explicit(&runner->stopping, true, memory_order_release);
    atomic_store_explicit(&runner->running, false, memory_order_release);
    if (runner->thread != NULL) {
        RTI_BOOL awakened = OSAPI_Semaphore_give(runner->wake);
        (void)awakened;
        if (!OSAPI_Thread_destroy(runner->thread)) {
            return false;
        }
        runner->thread = NULL;
    }
    if (runner->service != NULL &&
        (runner->service->lifecycle == PGW_READY ||
         runner->service->lifecycle == PGW_RUNNING)) {
        return PGW_Service_stop(runner->service) == PGW_OK;
    }
    return true;
}

bool PGW_Runner_finalize(PGW_Runner *runner)
{
    if (runner == NULL || !runner->initialized ||
        !PGW_Runner_stop(runner)) {
        return false;
    }
    if (runner->wake != NULL && !OSAPI_Semaphore_delete(runner->wake)) {
        return false;
    }
    runner->wake = NULL;
    if (runner->ready != NULL && !OSAPI_Semaphore_delete(runner->ready)) {
        return false;
    }
    runner->ready = NULL;
    runner->initialized = false;
    runner->started = false;
    runner->service = NULL;
    return true;
}

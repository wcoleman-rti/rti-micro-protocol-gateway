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

#ifndef PGW_ATOMIC_H
#define PGW_ATOMIC_H

/** @file
 * @brief OSAPI-backed atomic primitives used by public PGW structures.
 *
 * Use these operations to access fields declared with @c PGW_ATOMIC rather
 * than compiler-specific atomic intrinsics.
 */

#include <stdbool.h>
#include "rti_me_c.h"
#include "osapi/osapi_atomic.h"

#define PGW_ATOMIC(type__) RTI_ATOMIC(type__)
#define PGW_ATOMIC_INIT(value__, initial__) \
    OSAPI_Atomic_initialize((value__), (initial__))
#define PGW_ATOMIC_LOAD(value__, order__) \
    OSAPI_Atomic_load((value__), (order__))
#define PGW_ATOMIC_STORE(value__, initial__, order__) \
    OSAPI_Atomic_store((value__), (initial__), (order__))
#define PGW_ATOMIC_ADD(value__, amount__, order__) \
    OSAPI_Atomic_add((value__), (amount__), (order__))
#define PGW_ATOMIC_SUB(value__, amount__, order__) \
    OSAPI_Atomic_sub((value__), (amount__), (order__))
#define PGW_ATOMIC_LOAD_SEQ(value__) \
    OSAPI_Atomic_load((value__), OSAPI_ATOMIC_MEMORY_ORDER_SEQ_CONSISTENT)
#define PGW_ATOMIC_STORE_SEQ(value__, initial__) \
    OSAPI_Atomic_store((value__), (initial__), \
                       OSAPI_ATOMIC_MEMORY_ORDER_SEQ_CONSISTENT)
#define PGW_ATOMIC_ADD_SEQ(value__, amount__) \
    OSAPI_Atomic_add((value__), (amount__), \
                     OSAPI_ATOMIC_MEMORY_ORDER_SEQ_CONSISTENT)

typedef struct {
    RTI_ATOMIC(RTI_UINT32) state;
} PGW_AtomicLock;

static inline void PGW_AtomicLock_initialize(PGW_AtomicLock *lock)
{
    OSAPI_Atomic_initialize(&lock->state, 0);
}

static inline bool PGW_AtomicLock_try_take(PGW_AtomicLock *lock)
{
    if (OSAPI_Atomic_add(&lock->state, 1,
            OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE) == 0)
        return true;
    /* Failed contenders undo their increment without requiring compare-exchange. */
    OSAPI_Atomic_sub(&lock->state, 1,
                     OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE);
    return false;
}

static inline void PGW_AtomicLock_take(PGW_AtomicLock *lock)
{
    while (!PGW_AtomicLock_try_take(lock)) {}
}

static inline void PGW_AtomicLock_give(PGW_AtomicLock *lock)
{
    OSAPI_Atomic_sub(&lock->state, 1,
                     OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE);
}

#endif

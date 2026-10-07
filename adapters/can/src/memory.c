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
#include "pgw/can_memory.h"
#include <string.h>
#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"

static bool memory_lock(PGW_CANMemory *memory)
{
    return memory && memory->mutex && OSAPI_Mutex_take(memory->mutex);
}

static bool memory_unlock(PGW_CANMemory *memory)
{
    return memory && memory->mutex && OSAPI_Mutex_give(memory->mutex);
}

#define REDA_SEQUENCE_USER_API
#define T PGW_CANFrame
#define TSeq PGW_CANFrameSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

PGW_Status PGW_CANMemory_initialize(PGW_CANMemory *m, PGW_CANFrame *rx,
                                  size_t nr, PGW_CANFrame *tx, size_t nt)
{
    if (!m || !rx || !tx || !nr || !nt || nr > (size_t)INT32_MAX ||
        nt > (size_t)INT32_MAX ||
        nr > SIZE_MAX / sizeof(*rx) || nt > SIZE_MAX / sizeof(*tx))
        return PGW_INVALID;
    memset(m, 0, sizeof(*m));
    m->mutex = OSAPI_Mutex_new();
    if (!m->mutex) return PGW_FATAL;
    PGW_ATOMIC_INIT(&m->receive_failure, PGW_OK);
    PGW_ATOMIC_INIT(&m->send_failure, PGW_OK);
    if (!PGW_CANFrameSeq_initialize(&m->rx)) {
        if (!OSAPI_Mutex_delete(m->mutex)) return PGW_FATAL;
        m->mutex = NULL;
        return PGW_FATAL;
    }
    m->rx_initialized = true;
    if (!PGW_CANFrameSeq_loan_contiguous(&m->rx, rx, (RTI_INT32)nr, (RTI_INT32)nr) ||
        !PGW_CANFrameSeq_set_length(&m->rx, (RTI_INT32)nr)) {
        (void)PGW_CANFrameSeq_unloan(&m->rx);
        (void)PGW_CANFrameSeq_finalize(&m->rx);
        m->rx_initialized = false;
        if (!OSAPI_Mutex_delete(m->mutex)) return PGW_FATAL;
        m->mutex = NULL;
        return PGW_CAPACITY;
    }
    m->rx_borrowed = true;
    if (!PGW_CANFrameSeq_initialize(&m->tx)) {
        (void)PGW_CANFrameSeq_unloan(&m->rx);
        (void)PGW_CANFrameSeq_finalize(&m->rx);
        m->rx_initialized = m->rx_borrowed = false;
        if (!OSAPI_Mutex_delete(m->mutex)) return PGW_FATAL;
        m->mutex = NULL;
        return PGW_FATAL;
    }
    m->tx_initialized = true;
    if (!PGW_CANFrameSeq_loan_contiguous(&m->tx, tx, (RTI_INT32)nt, (RTI_INT32)nt) ||
        !PGW_CANFrameSeq_set_length(&m->tx, (RTI_INT32)nt)) {
        (void)PGW_CANFrameSeq_unloan(&m->tx);
        (void)PGW_CANFrameSeq_finalize(&m->tx);
        (void)PGW_CANFrameSeq_unloan(&m->rx);
        (void)PGW_CANFrameSeq_finalize(&m->rx);
        m->tx_initialized = m->rx_initialized = false;
        m->rx_borrowed = false;
        if (!OSAPI_Mutex_delete(m->mutex)) return PGW_FATAL;
        m->mutex = NULL;
        return PGW_CAPACITY;
    }
    m->tx_borrowed = true;
    return PGW_OK;
}

PGW_Status PGW_CANMemory_finalize(PGW_CANMemory *m)
{
    bool ok = true;
    if (!m) return PGW_INVALID;
    if (m->closed && !m->mutex) return PGW_OK;
    if (!m->mutex) return PGW_INVALID;
    if (!memory_lock(m)) return PGW_IO_ERROR;
    if (m->rx_initialized) {
        if (m->rx_borrowed && !PGW_CANFrameSeq_unloan(&m->rx)) ok = false;
        if (!PGW_CANFrameSeq_finalize(&m->rx)) ok = false;
    }
    if (m->tx_initialized) {
        if (m->tx_borrowed && !PGW_CANFrameSeq_unloan(&m->tx)) ok = false;
        if (!PGW_CANFrameSeq_finalize(&m->tx)) ok = false;
    }
    if (!ok) {
        return memory_unlock(m) ? PGW_LOAN_ERROR : PGW_IO_ERROR;
    }
    m->rx_initialized = m->rx_borrowed = false;
    m->tx_initialized = m->tx_borrowed = false;
    m->rx_head = m->rx_count = m->tx_head = m->tx_count = 0;
    m->closed = true;
    if (!memory_unlock(m)) return PGW_IO_ERROR;
    if (!OSAPI_Mutex_delete(m->mutex)) return PGW_FATAL;
    m->mutex = NULL;
    return PGW_OK;
}

PGW_Status PGW_CANMemory_inject(PGW_CANMemory *m, const PGW_CANFrame *f)
{
    if (!m || !f || !m->mutex) return PGW_INVALID;
    if (!memory_lock(m)) return PGW_IO_ERROR;
    if (m->closed || !m->rx_borrowed) {
        if (!memory_unlock(m)) return PGW_IO_ERROR;
        return PGW_INVALID;
    }
    size_t capacity = (size_t)PGW_CANFrameSeq_get_maximum(&m->rx);
    if (m->rx_count == capacity) {
        ++m->rx_overflow;
        return memory_unlock(m) ? PGW_BACKPRESSURE : PGW_IO_ERROR;
    }
    *PGW_CANFrameSeq_get_reference(&m->rx,
        (RTI_INT32)((m->rx_head + m->rx_count) % capacity)) = *f;
    ++m->rx_count;
    return memory_unlock(m) ? PGW_OK : PGW_IO_ERROR;
}

static PGW_Status receive(void *state, PGW_CANFrame *f)
{
    PGW_CANMemory *m = state;
    if (!m || !f || !m->mutex) return PGW_INVALID;
    if (!memory_lock(m)) return PGW_IO_ERROR;
    if (m->closed || !m->rx_borrowed) {
        if (!memory_unlock(m)) return PGW_IO_ERROR;
        return PGW_IO_ERROR;
    }
    PGW_Status receive_failure = PGW_ATOMIC_LOAD(
        &m->receive_failure, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
    if (receive_failure != PGW_OK) {
        return memory_unlock(m) ? receive_failure : PGW_IO_ERROR;
    }
    if (!m->rx_count) {
        return memory_unlock(m) ? PGW_NO_DATA : PGW_IO_ERROR;
    }
    size_t capacity = (size_t)PGW_CANFrameSeq_get_maximum(&m->rx);
    *f = *PGW_CANFrameSeq_get_reference(&m->rx, (RTI_INT32)m->rx_head);
    m->rx_head = (m->rx_head + 1) % capacity;
    --m->rx_count;
    return memory_unlock(m) ? PGW_OK : PGW_IO_ERROR;
}

static PGW_Status send(void *state, const PGW_CANFrame *f)
{
    PGW_CANMemory *m = state;
    if (!m || !m->mutex || !PGW_CANFrame_valid(f) ||
        (f->flags & (PGW_CAN_FLAG_RTR | PGW_CAN_FLAG_ERROR | PGW_CAN_FLAG_ECHO)))
        return PGW_INVALID;
    if (!memory_lock(m)) return PGW_IO_ERROR;
    if (m->closed || !m->tx_borrowed) {
        if (!memory_unlock(m)) return PGW_IO_ERROR;
        return PGW_IO_ERROR;
    }
    PGW_Status send_failure = PGW_ATOMIC_LOAD(
        &m->send_failure, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE);
    if (send_failure != PGW_OK) {
        return memory_unlock(m) ? send_failure : PGW_IO_ERROR;
    }
    size_t capacity = (size_t)PGW_CANFrameSeq_get_maximum(&m->tx);
    if (m->tx_count == capacity) {
        ++m->tx_backpressure;
        return memory_unlock(m) ? PGW_BACKPRESSURE : PGW_IO_ERROR;
    }
    *PGW_CANFrameSeq_get_reference(&m->tx,
        (RTI_INT32)((m->tx_head + m->tx_count) % capacity)) = *f;
    ++m->tx_count;
    return memory_unlock(m) ? PGW_OK : PGW_IO_ERROR;
}

PGW_Status PGW_CANMemory_take_sent(PGW_CANMemory *m, PGW_CANFrame *f)
{
    if (!m || !f || !m->mutex) return PGW_INVALID;
    if (!memory_lock(m)) return PGW_IO_ERROR;
    if (!m->tx_count || !m->tx_borrowed) {
        return memory_unlock(m) ? PGW_NO_DATA : PGW_IO_ERROR;
    }
    size_t capacity = (size_t)PGW_CANFrameSeq_get_maximum(&m->tx);
    *f = *PGW_CANFrameSeq_get_reference(&m->tx, (RTI_INT32)m->tx_head);
    m->tx_head = (m->tx_head + 1) % capacity;
    --m->tx_count;
    return memory_unlock(m) ? PGW_OK : PGW_IO_ERROR;
}

static PGW_Status close_transport(void *state)
{
    PGW_CANMemory *m = state;
    return PGW_CANMemory_finalize(m);
}

PGW_CANTransport PGW_CANMemory_transport(PGW_CANMemory *m)
{
    static const PGW_CANTransportI iface = {receive, send, close_transport};
    PGW_CANTransport result = {m, &iface};
    return result;
}

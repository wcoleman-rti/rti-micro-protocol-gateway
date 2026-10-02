#include "pgw/can_memory.h"
#include <string.h>
#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"

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
    if (!PGW_CANFrameSeq_initialize(&m->rx)) return PGW_FATAL;
    m->rx_initialized = true;
    if (!PGW_CANFrameSeq_loan_contiguous(&m->rx, rx, (RTI_INT32)nr, (RTI_INT32)nr) ||
        !PGW_CANFrameSeq_set_length(&m->rx, (RTI_INT32)nr)) {
        (void)PGW_CANFrameSeq_unloan(&m->rx);
        (void)PGW_CANFrameSeq_finalize(&m->rx);
        m->rx_initialized = false;
        return PGW_CAPACITY;
    }
    m->rx_borrowed = true;
    if (!PGW_CANFrameSeq_initialize(&m->tx)) {
        (void)PGW_CANFrameSeq_unloan(&m->rx);
        (void)PGW_CANFrameSeq_finalize(&m->rx);
        m->rx_initialized = m->rx_borrowed = false;
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
        return PGW_CAPACITY;
    }
    m->tx_borrowed = true;
    return PGW_OK;
}

PGW_Status PGW_CANMemory_finalize(PGW_CANMemory *m)
{
    bool ok = true;
    if (!m) return PGW_INVALID;
    if (m->rx_initialized) {
        if (m->rx_borrowed && !PGW_CANFrameSeq_unloan(&m->rx)) ok = false;
        if (!PGW_CANFrameSeq_finalize(&m->rx)) ok = false;
    }
    if (m->tx_initialized) {
        if (m->tx_borrowed && !PGW_CANFrameSeq_unloan(&m->tx)) ok = false;
        if (!PGW_CANFrameSeq_finalize(&m->tx)) ok = false;
    }
    if (!ok) return PGW_LOAN_ERROR;
    m->rx_initialized = m->rx_borrowed = false;
    m->tx_initialized = m->tx_borrowed = false;
    m->rx_head = m->rx_count = m->tx_head = m->tx_count = 0;
    m->closed = true;
    return PGW_OK;
}

PGW_Status PGW_CANMemory_inject(PGW_CANMemory *m, const PGW_CANFrame *f)
{
    if (!m || !f || m->closed || !m->rx_borrowed) return PGW_INVALID;
    size_t capacity = (size_t)PGW_CANFrameSeq_get_maximum(&m->rx);
    if (m->rx_count == capacity) {
        ++m->rx_overflow;
        return PGW_BACKPRESSURE;
    }
    *PGW_CANFrameSeq_get_reference(&m->rx,
        (RTI_INT32)((m->rx_head + m->rx_count) % capacity)) = *f;
    ++m->rx_count;
    return PGW_OK;
}

static PGW_Status receive(void *state, PGW_CANFrame *f)
{
    PGW_CANMemory *m = state;
    if (m->closed || !m->rx_borrowed) return PGW_IO_ERROR;
    if (m->receive_failure != PGW_OK) return m->receive_failure;
    if (!m->rx_count) return PGW_NO_DATA;
    size_t capacity = (size_t)PGW_CANFrameSeq_get_maximum(&m->rx);
    *f = *PGW_CANFrameSeq_get_reference(&m->rx, (RTI_INT32)m->rx_head);
    m->rx_head = (m->rx_head + 1) % capacity;
    --m->rx_count;
    return PGW_OK;
}

static PGW_Status send(void *state, const PGW_CANFrame *f)
{
    PGW_CANMemory *m = state;
    if (!PGW_CANFrame_valid(f) ||
        (f->flags & (PGW_CAN_FLAG_RTR | PGW_CAN_FLAG_ERROR | PGW_CAN_FLAG_ECHO)))
        return PGW_INVALID;
    if (m->closed || !m->tx_borrowed) return PGW_IO_ERROR;
    if (m->send_failure != PGW_OK) return m->send_failure;
    size_t capacity = (size_t)PGW_CANFrameSeq_get_maximum(&m->tx);
    if (m->tx_count == capacity) {
        ++m->tx_backpressure;
        return PGW_BACKPRESSURE;
    }
    *PGW_CANFrameSeq_get_reference(&m->tx,
        (RTI_INT32)((m->tx_head + m->tx_count) % capacity)) = *f;
    ++m->tx_count;
    return PGW_OK;
}

PGW_Status PGW_CANMemory_take_sent(PGW_CANMemory *m, PGW_CANFrame *f)
{
    if (!m || !f) return PGW_INVALID;
    if (!m->tx_count || !m->tx_borrowed) return PGW_NO_DATA;
    size_t capacity = (size_t)PGW_CANFrameSeq_get_maximum(&m->tx);
    *f = *PGW_CANFrameSeq_get_reference(&m->tx, (RTI_INT32)m->tx_head);
    m->tx_head = (m->tx_head + 1) % capacity;
    --m->tx_count;
    return PGW_OK;
}

static PGW_Status close_transport(void *state)
{
    PGW_CANMemory *m = state;
    m->closed = true;
    return PGW_CANMemory_finalize(m);
}

PGW_CANTransport PGW_CANMemory_transport(PGW_CANMemory *m)
{
    static const PGW_CANTransportI iface = {receive, send, close_transport};
    PGW_CANTransport result = {m, &iface};
    return result;
}

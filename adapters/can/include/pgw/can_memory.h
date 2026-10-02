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

#ifndef PGW_CAN_MEMORY_H
#define PGW_CAN_MEMORY_H
#include "pgw/can.h"

/** @brief In-memory CAN transport with caller-backed receive/transmit rings.
 *
 * This transport is cooperative and single-owner: injection, gateway calls,
 * taking sent frames, and finalization must be serialized. Ring buffers are
 * borrowed, not allocated or freed here. Transport close finalizes both rings.
 */
typedef struct {
    PGW_CANFrameSeq rx;                     /**< Receive ring over caller storage. */
    PGW_CANFrameSeq tx;                     /**< Transmit ring over caller storage. */
    size_t rx_head, rx_count, tx_head, tx_count; /**< Internal ring indices/counts. */
    uint64_t rx_overflow, tx_backpressure;    /**< RX-overflow and TX-backpressure totals. */
    PGW_Status receive_failure, send_failure; /**< Optional injected transport failure statuses. */
    bool rx_initialized, rx_borrowed;         /**< Internal RX sequence state. */
    bool tx_initialized, tx_borrowed;         /**< Internal TX sequence state. */
    bool closed;                              /**< True after finalize/transport close. */
} PGW_CANMemory;

/** @brief Initialize receive and transmit rings over caller-owned arrays.
 * Both capacities must be nonzero and no greater than INT32_MAX. The arrays
 * must remain writable and alive until finalization. Their contents are not
 * initialized beyond setting up sequence lengths.
 * @return PGW_OK, PGW_INVALID for invalid buffers/capacities, PGW_CAPACITY if
 *         sequence loans cannot be established, or PGW_FATAL on setup failure.
 */
PGW_Status PGW_CANMemory_initialize(PGW_CANMemory *, PGW_CANFrame *, size_t,
                                   PGW_CANFrame *, size_t);
/** @brief Return array loans and finalize both rings.
 * Marks the transport closed; reinitialize explicitly before reuse.
 * @return PGW_OK on success, PGW_INVALID for null input, or PGW_LOAN_ERROR if
 *         an unloan/finalization operation fails.
 */
PGW_Status PGW_CANMemory_finalize(PGW_CANMemory *);
/** @brief Copy a frame into the receive ring for a later gateway poll.
 * @return PGW_OK when queued, PGW_BACKPRESSURE when full, or PGW_INVALID for
 *         null/closed/uninitialized input. Frame validity is checked by the
 *         gateway when it is consumed.
 */
PGW_Status PGW_CANMemory_inject(PGW_CANMemory *, const PGW_CANFrame *);
/** @brief Remove the oldest transmitted frame from the transmit ring.
 * @return PGW_OK when a frame is copied to @p frame, PGW_NO_DATA when empty,
 *         or PGW_INVALID for null arguments.
 */
PGW_Status PGW_CANMemory_take_sent(PGW_CANMemory *, PGW_CANFrame *);
/** @brief Create a transport handle referring to @p memory.
 * The state pointer is borrowed; keep @p memory alive until its close callback
 * has completed.
 */
PGW_CANTransport PGW_CANMemory_transport(PGW_CANMemory *);
#endif

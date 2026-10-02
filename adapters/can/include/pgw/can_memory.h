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

/* Cooperative, single-owner rings; injection and gateway calls are serialized. */
typedef struct {
    PGW_CANFrameSeq rx;
    PGW_CANFrameSeq tx;
    size_t rx_head, rx_count, tx_head, tx_count;
    uint64_t rx_overflow, tx_backpressure;
    PGW_Status receive_failure, send_failure;
    bool rx_initialized, rx_borrowed;
    bool tx_initialized, tx_borrowed;
    bool closed;
} PGW_CANMemory;

PGW_Status PGW_CANMemory_initialize(PGW_CANMemory *, PGW_CANFrame *, size_t,
                                   PGW_CANFrame *, size_t);
PGW_Status PGW_CANMemory_finalize(PGW_CANMemory *);
PGW_Status PGW_CANMemory_inject(PGW_CANMemory *, const PGW_CANFrame *);
PGW_Status PGW_CANMemory_take_sent(PGW_CANMemory *, PGW_CANFrame *);
PGW_CANTransport PGW_CANMemory_transport(PGW_CANMemory *);
#endif

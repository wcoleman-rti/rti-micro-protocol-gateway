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

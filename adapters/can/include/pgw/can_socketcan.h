#ifndef PGW_CAN_SOCKETCAN_H
#define PGW_CAN_SOCKETCAN_H
#include "pgw/can.h"

typedef struct {
    uint32_t id;
    uint32_t mask;
} PGW_CANSocketFilter;
typedef const PGW_CANSocketFilter PGW_CANSocketFilterDefinition;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANSocketFilterDefinition
#define TSeq PGW_CANSocketFilterSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANSocketFilterSeq PGW_CANSocketFilterSeq;

typedef struct {
    const char *interface_name;
    bool enable_fd;
    bool receive_own_messages;
    uint32_t error_mask;
    PGW_CANSocketFilterSeq filters;
    bool filters_initialized;
    bool filters_borrowed;
} PGW_CANSocketConfig;

typedef struct {
    int fd;
    int last_errno;
    uint32_t interface_index;
    uint32_t kernel_rx_overflow;
    uint64_t receive_errors;
    uint64_t send_errors;
    bool enable_fd;
} PGW_CANSocket;

PGW_Status PGW_CANSocket_open(PGW_CANSocket *, const PGW_CANSocketConfig *);
PGW_Status PGW_CANSocketConfig_finalize(PGW_CANSocketConfig *);
PGW_CANTransport PGW_CANSocket_transport(PGW_CANSocket *);
#endif

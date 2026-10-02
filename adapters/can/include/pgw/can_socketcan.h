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

#ifndef PGW_CAN_SOCKETCAN_H
#define PGW_CAN_SOCKETCAN_H
#include "pgw/can.h"

/** @brief Linux SocketCAN raw-socket filter identifier and mask.
 * The values use SocketCAN's native can_id/can_mask bit representation.
 */
typedef struct {
    uint32_t id;    /**< Filter identifier value. */
    uint32_t mask;  /**< Bits compared against the received identifier. */
} PGW_CANSocketFilter;
typedef const PGW_CANSocketFilter PGW_CANSocketFilterDefinition;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANSocketFilterDefinition
#define TSeq PGW_CANSocketFilterSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANSocketFilterSeq PGW_CANSocketFilterSeq;

/** @brief Options for opening a Linux raw SocketCAN transport.
 * The interface name and filter sequence are borrowed during open. The filter
 * sequence must be initialized, and may contain at most 64 entries. An empty
 * filter list retains the kernel default of receiving all frames.
 */
typedef struct {
    const char *interface_name;          /**< Network interface name. */
    bool enable_fd;                      /**< Enable CAN FD frame support. */
    bool receive_own_messages;           /**< Ask the kernel to receive own sends. */
    uint32_t error_mask;                 /**< SocketCAN error-frame subscription mask. */
    PGW_CANSocketFilterSeq filters;      /**< Initialized sequence of raw filters. */
    bool filters_initialized;             /**< Internal sequence state. */
    bool filters_borrowed;                /**< Whether the sequence loans its input. */
} PGW_CANSocketConfig;

/** @brief State for one opened nonblocking SocketCAN raw socket.
 * The OS socket is owned by this object and closed through the transport close
 * callback. Do not copy an open object or access it concurrently with transport
 * operations.
 */
typedef struct {
    int fd;                       /**< Owned file descriptor; -1 when closed. */
    int last_errno;               /**< Last recorded system error (errno value). */
    uint32_t interface_index;     /**< Resolved Linux interface index. */
    uint32_t kernel_rx_overflow;  /**< Latest kernel receive-queue overflow count. */
    uint64_t receive_errors;      /**< Receive/close I/O errors. */
    uint64_t send_errors;         /**< Send I/O errors. */
    bool enable_fd;               /**< Whether CAN FD was enabled on this socket. */
} PGW_CANSocket;

/** @brief Open and bind a nonblocking Linux SocketCAN raw socket.
 * The socket argument receives owned state and must not represent an open
 * socket. The config argument supplies valid options and an initialized filter
 * sequence.
 * @return PGW_OK on success, PGW_INVALID for invalid arguments/filter count,
 *         or PGW_IO_ERROR for interface, socket, option, or bind failures.
 *         On I/O failure @c last_errno records the most recent errno where
 *         available; a partially opened descriptor is closed.
 */
PGW_Status PGW_CANSocket_open(PGW_CANSocket *, const PGW_CANSocketConfig *);
/** @brief Return filter-sequence loans and finalize a socket config.
 * Does not close an already opened socket.
 * @return PGW_OK on success, PGW_INVALID for null/uninitialized input, or
 *         PGW_LOAN_ERROR if a sequence loan/finalization fails.
 */
PGW_Status PGW_CANSocketConfig_finalize(PGW_CANSocketConfig *);
/** @brief Create a CAN transport handle for this socket.
 * The socket object is borrowed and must outlive the transport connection.
 */
PGW_CANTransport PGW_CANSocket_transport(PGW_CANSocket *);
#endif

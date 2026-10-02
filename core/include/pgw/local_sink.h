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

#ifndef PGW_LOCAL_SINK_H
#define PGW_LOCAL_SINK_H
#include "pgw/core.h"

/** @brief Nonblocking JSON-lines counter snapshot sink.
 *
 * The descriptor and serialization buffer are borrowed. Initialization marks
 * the descriptor nonblocking but does not close it; the caller retains
 * responsibility for descriptor lifetime. Calls that share one sink must be
 * serialized because the buffer is reused. Regular-file writes may block
 * despite nonblocking descriptor flags, so invoke from outside latency-sensitive
 * routing threads.
 */
typedef struct {
    int descriptor;       /**< Borrowed descriptor configured nonblocking. */
    char *buffer;         /**< Borrowed scratch buffer for JSON output. */
    size_t capacity;      /**< Scratch buffer capacity in bytes. */
    PGW_Counters counters; /**< Counters for export outcomes. */
} PGW_LocalSink;

/** @brief Initialize a sink and set its descriptor to nonblocking mode.
 * The sink argument points to storage; descriptor is an open writable
 * descriptor that remains caller-owned; buffer is writable scratch storage
 * borrowed for the sink lifetime; capacity is its nonzero size.
 * @return PGW_OK, PGW_INVALID for invalid input, PGW_UNSUPPORTED if counter
 *         atomics are unsupported, or PGW_IO_ERROR if descriptor flags cannot
 *         be read/updated. The descriptor is not closed on failure.
 */
PGW_Status PGW_LocalSink_initialize(PGW_LocalSink *, int, char *, size_t);
/** @brief Serialize and write a version-1 snapshot as one JSON line.
 * Calls on a sink must be serialized. The snapshot and sink buffer are not
 * retained after return.
 * @return PGW_OK on a complete write; PGW_INVALID for an unsupported snapshot
 *         version or invalid input, PGW_CAPACITY when JSON does not fit,
 *         PGW_BACKPRESSURE for a retryable nonblocking write, or PGW_IO_ERROR
 *         for a failed/short write.
 */
PGW_Status PGW_LocalSink_snapshot(PGW_LocalSink *, const PGW_CounterSnapshot *);
#endif

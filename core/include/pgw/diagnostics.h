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

#ifndef PGW_DIAGNOSTICS_H
#define PGW_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "pgw/sequence.h"

/** @brief Counter slots maintained by the core and adapters.
 * Values are relaxed atomic totals and may wrap on overflow.
 */
typedef enum {
    PGW_COUNT_RECEIVED,       /**< Input samples/frames observed. */
    PGW_COUNT_ACCEPTED,       /**< Samples or exports accepted. */
    PGW_COUNT_BACKPRESSURE,   /**< Operations not accepted due to capacity. */
    PGW_COUNT_INVALID,        /**< Invalid samples, frames, or requests. */
    PGW_COUNT_FATAL,          /**< Fatal processing failures. */
    PGW_COUNT_LOANS,          /**< Currently outstanding sequence loans. */
    PGW_COUNT_LOAN_ERRORS,    /**< Loan return failures. */
    PGW_COUNT_FRAMES,         /**< Frames processed. */
    PGW_COUNT_OVERFLOW,       /**< Queue or ring overflows. */
    PGW_COUNT_EXPORT_ERRORS,  /**< Snapshot/export failures. */
    PGW_COUNT_HIGH_WATER,     /**< High-water batch/queue measure. */
    PGW_COUNT_ROUTE_FAULTS,   /**< Routes that entered a faulted state. */
    PGW_COUNT_TOTAL           /**< Number of counter slots; not a counter ID. */
} PGW_CounterId;

/** @brief Lock-free atomic counter collection.
 * Initialization fails if 64-bit counter operations are not lock-free on the
 * target. After initialization, increments and snapshots may be concurrent.
 */
typedef struct {
    atomic_uint_fast64_t values[PGW_COUNT_TOTAL];
} PGW_Counters;

/** @brief Versioned copy of all counter values at one collection point.
 * @c sequence is supplied by the caller; timestamps are nanoseconds in the
 * caller-selected clock domain.
 */
typedef struct {
    uint32_t version;           /**< Snapshot format version (currently 1). */
    uint32_t entity_id;         /**< Caller-defined entity identifier. */
    uint64_t sequence;          /**< Caller-defined sequence number. */
    uint64_t collected_ns;      /**< Caller-supplied collection time. */
    uint64_t values[PGW_COUNT_TOTAL]; /**< Counter values indexed by PGW_CounterId. */
} PGW_CounterSnapshot;

/** @brief Diagnostic event stored in the bounded diagnostic ring.
 * Severity values are caller-defined ordered levels; events below the
 * diagnostic object's minimum severity are treated as filtered successfully.
 */
typedef struct {
    uint64_t time_ns;       /**< Event time in the selected clock domain. */
    bool time_valid;        /**< Whether @c time_ns is valid. */
    uint32_t entity_id;     /**< Identifier of the event source. */
    uint32_t code;          /**< Application- or adapter-defined event code. */
    uint32_t severity;      /**< Ordered severity level. */
    uint64_t count;         /**< Number of occurrences represented. */
} PGW_Event;

#define T PGW_Event
#define TSeq PGW_EventSeq
#define REDA_SEQUENCE_API REDA_SEQUENCE_API_UNTYPED
#define TSeq_initialize
#define TSeq_finalize
#define TSeq_get_length
#define TSeq_get_maximum
#define TSeq_set_length
#define TSeq_get_reference
#define TSeq_loan_contiguous
#define TSeq_unloan
#define TSeq_get_contiguous_buffer
#define TSeq_has_ownership
#include "reda/reda_sequence_decl.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate
/** @brief Sequence of diagnostic events used for ring storage and draining. */
typedef struct PGW_EventSeq PGW_EventSeq;

/** @brief Bounded, thread-safe diagnostic event ring.
 *
 * The sequence storage is borrowed from the empty source sequence provided to
 * initialize. Emitters serialize on an internal atomic flag; contention,
 * rate-limiting, and overflow are counted rather than blocking.
 */
typedef struct {
    PGW_EventSeq events;                /**< Ring sequence over borrowed storage. */
    size_t head;                        /**< Internal oldest-event index. */
    size_t count;                       /**< Internal queued-event count. */
    atomic_flag lock;                   /**< Internal nonblocking ring lock. */
    atomic_uint_fast64_t overflow;      /**< Events rejected because ring was full. */
    atomic_uint_fast64_t contention;    /**< Emit attempts rejected on lock contention. */
    atomic_uint_fast64_t drain_contention; /**< Drain/finalize lock-contention count. */
    atomic_uint_fast64_t rate_limited;  /**< Events rejected by the rate limit. */
    uint32_t minimum_severity;          /**< Events below this threshold are filtered. */
    uint64_t rate_interval_ns;          /**< Configured rate-limit interval. */
    size_t rate_max_events;             /**< Maximum accepted events per interval. */
    uint64_t rate_window_ns;            /**< Internal start time of current window. */
    size_t rate_window_events;          /**< Internal count in current window. */
    bool initialized;                   /**< True after successful initialization. */
    bool events_borrowed;               /**< Whether @c events loans caller storage. */
} PGW_Diagnostics;

/** @brief Snapshot of diagnostic ring capacity and loss counters. */
typedef struct {
    uint32_t version;             /**< Snapshot format version (currently 1). */
    uint64_t collected_ns;        /**< Caller-supplied collection time. */
    size_t event_capacity;        /**< Event slots reserved by the ring. */
    uint64_t overflow;            /**< Events lost to a full ring. */
    uint64_t contention;          /**< Emit lock contention. */
    uint64_t drain_contention;    /**< Drain/finalize lock contention. */
    uint64_t rate_limited;        /**< Events rejected by rate limiting. */
} PGW_DiagnosticSnapshot;

/** @brief Initialize counters to zero.
 * @c counters points to storage to initialize.
 * @return True if the pointer is valid and required atomic operations are
 *         lock-free; false otherwise.
 */
bool PGW_Counters_initialize(PGW_Counters *);
/** @brief Atomically add to one counter.
 * @c counters is an initialized collection; @c id is a counter slot less than
 * PGW_COUNT_TOTAL; @c amount is the amount to add (zero is permitted).
 * @return True when the update was applied, false for an invalid pointer/ID.
 */
bool PGW_Counters_add(PGW_Counters *, PGW_CounterId, uint64_t);
/** @brief Copy all counters into a version-1 snapshot.
 * @c counters is an initialized collection; @c entity_id, @c sequence, and
 * @c collected_ns are caller-supplied metadata; @c out receives the snapshot.
 * @return True on success, false for a null input/output pointer.
 */
bool PGW_Counters_snapshot(const PGW_Counters *, uint32_t, uint64_t,
                          uint64_t, PGW_CounterSnapshot *);
/** @brief Initialize diagnostics using storage from an empty event sequence.
 * The source must have length zero and contiguous storage when its capacity is
 * nonzero. Its event buffer is borrowed until successful finalization.
 * @return True on success; false for invalid storage or unsupported atomics.
 */
bool PGW_Diagnostics_initialize(PGW_Diagnostics *, const PGW_EventSeq *);
/** @brief Return the borrowed event buffer and finalize diagnostics.
 * Call only when emitters/drainers have quiesced. False indicates invalid
 * state, lock contention, or sequence loan/finalization failure.
 */
bool PGW_Diagnostics_finalize(PGW_Diagnostics *);
/** @brief Configure an optional event rate limit.
 * A zero interval and zero maximum disable limiting; otherwise both must be
 * nonzero. The window advances using event timestamps. Configure only before
 * concurrent emitters start, or serialize reconfiguration with every emitter.
 * @return True on success, false for invalid/uninitialized state or mismatched
 *         zero/nonzero settings.
 */
bool PGW_Diagnostics_configure_rate(PGW_Diagnostics *, uint64_t, size_t);
/** @brief Snapshot diagnostic ring capacity and loss counters.
 * @c diagnostics is the initialized ring; @c collected_ns is a caller-supplied
 * collection timestamp; @c out receives a version-1 snapshot.
 * @return True on success; false for invalid input or uninitialized ring.
 */
bool PGW_Diagnostics_snapshot(const PGW_Diagnostics *, uint64_t, PGW_DiagnosticSnapshot *);
/** @brief Attempt to enqueue an event without blocking.
 * Events below the minimum severity are filtered and return true. A full ring,
 * rate limit, or lock contention returns false; the corresponding loss count
 * is updated where applicable.
 */
bool PGW_Diagnostics_emit(PGW_Diagnostics *, const PGW_Event *);
/** @brief Copy queued events to caller-provided sequence storage.
 * Copies up to the output sequence's capacity in queue order and leaves any
 * remaining events queued. Source and destination storage must not overlap.
 * False indicates invalid sequences, insufficient sequence setup, or lock
 * contention; on contention no events are removed.
 */
bool PGW_Diagnostics_drain(PGW_Diagnostics *, PGW_EventSeq *);

/** @brief Serialize a version-1 counter snapshot as one JSON line.
 * @c snapshot must have version 1; @c buffer and @c capacity describe the
 * destination storage, whose size includes room for the terminating NUL;
 * @c length receives JSON bytes written, excluding that terminator.
 * @return True if all output fits; false for invalid input/version or
 *         insufficient capacity.
 */
bool PGW_snapshot_json(const PGW_CounterSnapshot *, char *, size_t, size_t *);

#endif

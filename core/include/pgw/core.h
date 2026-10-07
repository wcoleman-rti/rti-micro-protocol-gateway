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

#ifndef PGW_CORE_H
#define PGW_CORE_H

/** @addtogroup pgw_core_api
 * @{
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "rti_me_c.h"
#include "pgw/sequence.h"
#include "pgw/diagnostics.h"

#define PGW_ABI_VERSION 3u

/** @brief Result codes shared by the gateway core and adapters.
 *
 * A successful operation returns @ref PGW_OK. The other values distinguish
 * nonfatal conditions and failures; callers should not treat every non-OK
 * result as a fatal error.
 */
typedef enum {
    PGW_OK,             /**< Operation succeeded. */
    PGW_NO_DATA,        /**< No input is currently available (not an error). */
    PGW_BACKPRESSURE,   /**< Output could not be accepted because capacity is full. */
    PGW_INVALID,        /**< Invalid argument, state, or input data. */
    PGW_UNSUPPORTED,    /**< A requested capability, representation, or operation is unsupported. */
    PGW_CAPACITY,       /**< Capacity is insufficient or a required size overflows. */
    PGW_IO_ERROR,       /**< Transport or other I/O operation failed. */
    PGW_LOAN_ERROR,     /**< A sequence loan could not be returned or finalized. */
    PGW_FATAL,          /**< Unrecoverable operation or initialization failure. */
    PGW_NO_CHANGE       /**< Requested state already holds; no action was taken. */
} PGW_Status;

/** @brief Error information retained when a route enters the faulted state.
 * The operation string is a static, non-owned label and may be null when no
 * operation-specific detail is available.
 */
typedef struct {
    PGW_Status status;       /**< Status that caused the fault. */
    uint32_t entity_id;      /**< Identifier of the affected route. */
    const char *operation;   /**< Non-owned operation label. */
} PGW_Error;

/** @brief Lifecycle states used by services, sessions, and routes.
 *
 * Initialization progresses through INITIALIZING to READY; starting workers
 * moves a service and its sessions to RUNNING. Routes may be paused and
 * resumed while the service is ready or running. A route that encounters a
 * fatal operation enters FAULTED. A stopped service must be finalized before
 * reinitialization;
 * finalization releases sequence loans but does not free caller storage, which
 * must be explicitly adopted again. A service whose lifecycle is FAULTED
 * cannot be recovered through this lifecycle API.
 */
typedef enum {
    PGW_UNINITIALIZED, /**< Storage has not been initialized. */
    PGW_INITIALIZING,  /**< Initialization is in progress. */
    PGW_READY,          /**< Initialized and ready to step. */
    PGW_RUNNING,        /**< Service has performed or is performing steps. */
    PGW_STOPPED,        /**< Service has been stopped. */
    PGW_FAULTED,        /**< Route or service encountered an unrecoverable failure. */
    PGW_PAUSED          /**< Route is temporarily excluded from service stepping. */
} PGW_Lifecycle;

/** @brief Schema identity used to match data representations.
 * Names and fingerprints are non-owned, null-terminated strings and must
 * remain valid while the schema is in use.
 */
typedef struct {
    const char *name;         /**< Schema name. */
    uint32_t version;         /**< Schema version. */
    const char *fingerprint;  /**< Stable schema fingerprint. */
} PGW_Schema;

/** @brief Timestamp associated with a sample or frame.
 * If @c valid is false, the time is unavailable. @c portable indicates whether
 * the timestamp is in a portable clock domain; seconds and nanoseconds are
 * otherwise supplied by the source.
 */
typedef struct {
    bool valid;            /**< Whether the timestamp is present. */
    bool portable;         /**< Whether its clock domain is portable. */
    int64_t seconds;       /**< Whole seconds in the source clock domain. */
    uint32_t nanoseconds;  /**< Fractional seconds; expected to be below 1e9. */
} PGW_Timestamp;

/** @brief Borrowed view of a sample and its adapter-specific context.
 *
 * The pointers remain valid only for the lifetime of the source sample loan.
 * Canonical views expose the representation's copy_value type. Native views
 * expose an adapter-native type identified by type_identity. A view's kind,
 * value_size, type_identity, and context_identity must match its representation
 * contract. context and context_identity must either both be null or both be
 * non-null.
 */
typedef enum {
    PGW_SAMPLE_VIEW_CANONICAL,
    PGW_SAMPLE_VIEW_NATIVE
} PGW_SampleViewKind;

typedef struct {
    PGW_SampleViewKind kind;      /**< Canonical representation or adapter-native data. */
    const void *value;            /**< Borrowed payload pointer. */
    size_t value_size;            /**< Payload size when known, otherwise zero. */
    const void *type_identity;    /**< Opaque exact identity for the payload type. */
    const void *context;          /**< Borrowed adapter-specific sample metadata. */
    const void *context_identity; /**< Opaque identity describing context's C type. */
} PGW_SampleView;

/** @brief Static contract for the borrowed sample views a representation emits.
 *
 * The descriptor is borrowed and immutable for the representation's lifetime.
 * `type_identity` is a stable opaque identity for the view payload contract;
 * for native DDS samples it is the generated type identity. Canonical views
 * must provide a nonzero value_size. Native views may use zero value_size
 * when type_identity fully identifies their layout.
 */
typedef struct {
    PGW_SampleViewKind kind;
    size_t value_size;
    const void *type_identity;
    const void *context_identity;
} PGW_SampleViewDescriptor;

/** @brief Optional operations for accessing a sample without knowing its type.
 *
 * Implementations are versioned with PGW_ABI_VERSION. They must copy into
 * caller-provided storage and must not retain the destination pointer or the
 * borrowed pointers returned by view.
 */
typedef struct {
    uint32_t version;  /**< Must equal @ref PGW_ABI_VERSION. */
    size_t size;       /**< Must equal sizeof(PGW_SampleAccessI). */
    /** Copy a sample value to caller storage.
     * The sample argument is the value to read; destination and capacity
     * describe writable caller storage. Too little space is reported by the
     * implementation's PGW_Status.
     * @return PGW_OK on success, otherwise an applicable PGW_Status.
     */
    PGW_Status (*copy_value)(const PGW_Sample *, void *, size_t);
    /** Obtain a sample's source timestamp.
     * The sample argument identifies the value to inspect; timestamp receives
     * the result on success.
     * @return PGW_OK on success, otherwise an applicable PGW_Status.
     */
    PGW_Status (*source_timestamp)(const PGW_Sample *, PGW_Timestamp *);
    /** Return borrowed sample data and context without copying.
     * @return PGW_OK when the view is available, PGW_UNSUPPORTED when this
     *         sample cannot provide one, or another applicable status.
     */
    PGW_Status (*view)(const PGW_Sample *, PGW_SampleView *);
} PGW_SampleAccessI;

/** @brief Describes a sample's schema and native in-memory representation.
 * The schema, name, and access table are borrowed and must outlive every use of
 * this representation. Alignment must be a nonzero power of two. If access
 * provides view, view_contract must declare that view's immutable payload and
 * context shape before a destination writer binds.
 */
typedef struct {
    const PGW_Schema *schema;          /**< Schema identity. */
    const char *name;                  /**< Binding name used for lookup. */
    size_t sample_size;                /**< Native sample size in bytes. */
    size_t sample_alignment;           /**< Required native sample alignment. */
    const PGW_SampleAccessI *access;   /**< Optional type-erased access methods. */
    const PGW_SampleViewDescriptor *view_contract; /**< Static contract for optional borrowed views. */
} PGW_Representation;

typedef struct PGW_Session PGW_Session;
struct PGW_Service;

/** @brief Per-sample result returned by a stream writer.
 * A write can accept some samples and apply backpressure or reject others;
 * consult one result for each submitted sample.
 */
typedef enum {
    PGW_WRITE_ACCEPTED,      /**< Sample accepted by the destination. */
    PGW_WRITE_BACKPRESSURE,  /**< Sample not accepted because output is full. */
    PGW_WRITE_INVALID,       /**< Sample is invalid for this destination. */
    PGW_WRITE_FATAL          /**< Destination cannot continue processing. */
} PGW_WriteResult;

#define T PGW_WriteResult
#define TSeq PGW_WriteResultSeq
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
/** @brief Sequence of one PGW_WriteResult per stream-writer input sample. */
typedef struct PGW_WriteResultSeq PGW_WriteResultSeq;

/** @brief Reader-to-session readiness notification.
 *
 * This callback only records a coalesced reader-ready event and signals the
 * owning session. It is thread-safe, nonblocking, allocation-free, and does
 * not read samples, route data, or retain endpoint state.
 */
typedef struct {
    void (*on_data_available)(void *context);
    void *context;
} PGW_ReaderListener;

/** @brief Stream-reader operations supplied by an adapter.
 *
 * @c read fills a caller-provided sample sequence, up to @c maximum, and may
 * loan its backing storage; each successful loan must be returned exactly once
 * with @c return_loan before the sequence is reused or finalized. Register
 * the listener before workers start and unregister it synchronously before
 * session storage is finalized. A bounded read that leaves data available
 * must notify again before returning; synchronize producer/consumer state so a
 * concurrent enqueue cannot be stranded.
 */
typedef struct {
    uint32_t version;  /**< Must equal @ref PGW_ABI_VERSION. */
    size_t size;       /**< Must equal sizeof(PGW_StreamReaderI). */
    /** Read at most the requested number of samples.
     * The state argument identifies adapter-owned reader state; samples is the
     * caller-provided output sequence, possibly loaned on success; maximum is
     * the sample limit.
     * @return PGW_OK, PGW_NO_DATA, or an error/backpressure status.
     */
    PGW_Status (*read)(void *, PGW_SampleSeq *, size_t);
    /** Return a loan previously obtained by @c read.
     * The state argument identifies adapter-owned reader state; samples is the
     * sequence whose loan is being returned.
     * @return PGW_OK on success; PGW_LOAN_ERROR or another status on failure.
     */
    PGW_Status (*return_loan)(void *, PGW_SampleSeq *);
    /** Register/unregister one session-owned listener. A consuming reader may
     * be registered to only one route. Unregister prevents future callbacks
     * and waits for callbacks already using the listener to finish.
     */
    PGW_Status (*register_listener)(void *, const PGW_ReaderListener *);
    PGW_Status (*unregister_listener)(void *, const PGW_ReaderListener *);
} PGW_StreamReaderI;

/** @brief Stream-writer operations supplied by an adapter.
 * The writer binds once to a representation before use. The writer receives
 * borrowed input samples for the duration of the call and reports acceptance
 * independently for each one.
 */
typedef struct {
    uint32_t version;  /**< Must equal @ref PGW_ABI_VERSION. */
    size_t size;       /**< Must equal sizeof(PGW_StreamWriterI). */
    /** Negotiate whether the writer accepts a source representation.
     * The state argument identifies adapter-owned writer state; representation
     * is borrowed through the connection lifetime. The writer is responsible
     * for rejecting incompatible schemas or unsupported view contracts.
     * @return PGW_OK on success, otherwise an applicable PGW_Status.
     */
    PGW_Status (*bind)(void *, const PGW_Representation *);
    /** Write samples and produce one result per input sample.
     * The state argument identifies adapter-owned writer state; samples is a
     * borrowed input sequence whose references must not be retained; results
     * is the caller-provided outcome sequence.
     * @return PGW_OK when the operation completes; per-sample outcomes are in
     *         @p results. Other statuses indicate call-level failure.
     */
    PGW_Status (*write)(void *, const PGW_SampleSeq *, PGW_WriteResultSeq *);
} PGW_StreamWriterI;

/** @brief Handle to a reader and its adapter-owned state and representation. */
typedef struct {
    void *state;                                /**< Adapter-owned state. */
    const PGW_StreamReaderI *iface;             /**< Borrowed operation table. */
    const PGW_Representation *representation;   /**< Bound sample representation. */
} PGW_StreamReader;

/** @brief Handle to a writer and its adapter-owned state and representation. */
typedef struct {
    void *state;                                /**< Adapter-owned state. */
    const PGW_StreamWriterI *iface;             /**< Borrowed operation table. */
    const PGW_Representation *representation;   /**< Bound sample representation. */
} PGW_StreamWriter;

/** @brief Opaque adapter-created connection handle.
 * The creating adapter owns connection internals; close it through the
 * corresponding PGW_ConnectionI interface before releasing its arena.
 */
typedef struct PGW_Connection PGW_Connection;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
struct PGW_ControlAdapterI;
#endif
/** @brief Operations on an adapter connection.
 * Reader/writer lookup returns handles whose lifetime is bounded by the
 * connection. Closing a connection releases adapter resources; do not use its
 * handles afterward.
 */
typedef struct {
    uint32_t version;  /**< Must equal @ref PGW_ABI_VERSION. */
    size_t size;       /**< Must equal sizeof(PGW_ConnectionI). */
    /** Look up a named reader endpoint. */
    PGW_Status (*reader)(PGW_Connection *, const char *, PGW_StreamReader *);
    /** Look up a named writer endpoint. */
    PGW_Status (*writer)(PGW_Connection *, const char *, PGW_StreamWriter *);
    /** Close the connection and release its resources. */
    PGW_Status (*close)(PGW_Connection *);
} PGW_ConnectionI;

/** @brief Bump allocator over caller-owned storage.
 * The caller owns and must keep @c storage alive for all allocations. There is
 * no individual free operation; allocation advances @c used.
 */
typedef struct {
    void *storage;     /**< Writable memory supplied by the caller. */
    size_t capacity;   /**< Total bytes available at storage. */
    size_t used;       /**< Bytes already consumed, including alignment padding. */
} PGW_Arena;

/** @brief Adapter factory descriptor registered in a gateway registry.
 * The descriptor and interface table are borrowed and must outlive any
 * registry or connection using them.
 */
typedef struct {
    uint32_t version;  /**< Must equal @ref PGW_ABI_VERSION. */
    size_t size;       /**< Must equal sizeof(PGW_AdapterI). */
    const char *name;  /**< Unique, non-empty adapter name. */
    /** Construct a connection using caller-provided arena storage.
     * The config argument is adapter-specific; arena supplies storage for
     * connection objects and adapter state; connection receives the new
     * connection on success.
     * @return PGW_OK on success, otherwise an applicable PGW_Status.
     */
    PGW_Status (*create)(const void *, PGW_Arena *, PGW_Connection **);
    const PGW_ConnectionI *connection; /**< Versioned connection operation table. */
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    const struct PGW_ControlAdapterI *control; /**< Optional versioned action manifest. */
#endif
} PGW_AdapterI;

/** @brief Non-owning reference to an adapter descriptor. */
typedef const PGW_AdapterI *PGW_AdapterRef;
#define T PGW_AdapterRef
#define TSeq PGW_AdapterSeq
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
/** @brief Sequence of borrowed adapter descriptor references. */
typedef struct PGW_AdapterSeq PGW_AdapterSeq;

/** @brief Non-owning reference to a representation descriptor. */
typedef const PGW_Representation *PGW_RepresentationRef;
#define T PGW_RepresentationRef
#define TSeq PGW_RepresentationSeq
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
/** @brief Sequence of borrowed representation descriptor references. */
typedef struct PGW_RepresentationSeq PGW_RepresentationSeq;

/** @brief Registry borrowing caller-provided adapter and representation buffers.
 * Initialize with sequences that have preallocated contiguous storage and
 * capacity for all subsequent registrations. The registry does not own those
 * buffers; finalize it before the source buffers are destroyed. Serialize
 * initialization, registration, lookup, and finalization with respect to one
 * another.
 */
typedef struct {
    PGW_AdapterSeq adapters;          /**< Borrowed adapter-pointer storage. */
    PGW_RepresentationSeq bindings;   /**< Borrowed representation-pointer storage. */
    bool frozen;                      /**< Reject registration when true. */
    bool initialized;                 /**< Internal sequence initialization state. */
    bool adapters_borrowed;           /**< Internal adapter-buffer loan state. */
    bool bindings_borrowed;           /**< Internal binding-buffer loan state. */
} PGW_Registry;

#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
#define PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS 32u

/** @brief Atomic, fixed-size route batch-latency aggregation. */
typedef struct {
    atomic_uint_fast64_t batches;       /**< Completed non-empty writer calls. */
    atomic_uint_fast64_t timed_batches; /**< Batches with valid start/end clock readings. */
    atomic_uint_fast64_t samples;       /**< Input samples in completed writer calls. */
    atomic_uint_fast64_t accepted;
    atomic_uint_fast64_t backpressure;
    atomic_uint_fast64_t invalid;
    atomic_uint_fast64_t fatal;
    atomic_uint_fast64_t clock_failures; /**< Failed or backwards clock readings. */
    atomic_uint_fast64_t total_ns;      /**< Sum of valid batch durations; wraps modulo 2^64. */
    atomic_uint_fast64_t minimum_ns;
    atomic_uint_fast64_t maximum_ns;
    atomic_uint_fast64_t histogram[PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS];
} PGW_RouteLatencyStats;

/** @brief Non-transactional copy of per-route batch latency statistics.
 *
 * Histogram bucket 0 covers durations through 1,000 ns. Each following
 * bucket doubles its upper bound; bucket 31 covers durations above
 * 1,000 * 2^30 ns. Values are nanoseconds and counters wrap modulo 2^64.
 */
typedef struct {
    uint64_t batches;
    uint64_t timed_batches;
    uint64_t samples;
    uint64_t accepted;
    uint64_t backpressure;
    uint64_t invalid;
    uint64_t fatal;
    uint64_t clock_failures;
    uint64_t total_ns;
    uint64_t minimum_ns;
    uint64_t maximum_ns;
    uint64_t histogram[PGW_ROUTE_LATENCY_HISTOGRAM_BUCKETS];
} PGW_RouteLatencySnapshot;
#endif

/** @brief One route between a reader and writer.
 * Sample and result sequences are loans over caller-provided backing buffers;
 * keep those buffers alive through service finalization. A route fault stores
 * its cause in @c error and prevents further routing for that route. Route
 * fields and backing storage are caller-owned.
 */
typedef struct {
    uint32_t id;                       /**< Caller-defined unique route identifier. */
    PGW_StreamReader reader;           /**< Borrowed reader handle. */
    PGW_StreamWriter writer;           /**< Borrowed writer handle. */
    PGW_SampleSeq samples;              /**< Internal sequence over caller sample storage. */
    PGW_WriteResultSeq results;         /**< Internal sequence over caller result storage. */
    PGW_Counters counters;               /**< Per-route counters, initialized by service. */
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    PGW_RouteLatencyStats latency;       /**< Fixed-size batch latency statistics. */
    bool latency_initialized;            /**< Internal latency atomic initialization state. */
#endif
    atomic_int lifecycle;                /**< Synchronized route lifecycle state. */
    PGW_Error error;                     /**< Last recorded route fault. */
    PGW_Session *session;                 /**< Owning session, set during service initialization. */
    PGW_ReaderListener listener;          /**< Core-owned notification registration. */
    atomic_bool pending;                   /**< Coalesced per-reader readiness flag. */
    bool listener_registered;              /**< Internal adapter listener state. */
    bool storage_initialized;            /**< Internal sequence state. */
    bool samples_borrowed;               /**< Internal sample-buffer loan state. */
    bool results_borrowed;               /**< Internal result-buffer loan state. */
} PGW_Route;

#define T PGW_Route
#define TSeq PGW_RouteSeq
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
/** @brief Sequence of caller-owned route objects borrowed by a service. */
typedef struct PGW_RouteSeq PGW_RouteSeq;

/** @brief One serialized event-driven route group.
 *
 * Configure a non-empty borrowed route sequence before service initialization.
 * Each route has one unique consuming reader and belongs to exactly one
 * session. The session owns one WaitSet, one worker, and one wake guard
 * condition. Each route has a coalescing pending flag that identifies its
 * reader. Active routes are visited in rotating order, with at most one
 * sample-budget batch per pending route per wake result. If bounded reads
 * leave input available, the adapter notifies again.
 */
struct PGW_Session {
    const char *name;                    /**< Stable, non-empty configured session name. */
    PGW_RouteSeq routes;                 /**< Borrowed route array assigned to this session. */
    DDS_WaitSet *waitset;                /**< Session-owned WaitSet. */
    DDS_GuardCondition *wake_guard;      /**< Route, lifecycle, and stop notifications. */
    struct DDS_ConditionSeq active_conditions; /**< Bounded WaitSet result storage. */
    struct OSAPI_Thread *worker;         /**< Serialized session worker. */
    struct PGW_Service *service;         /**< Owning service while initialized. */
    size_t cursor;                       /**< Internal rotating route-dispatch cursor. */
    size_t attached_conditions;          /**< Internal number of attached conditions. */
    atomic_uint_fast64_t wakeups;         /**< Successful session WaitSet wakes. */
    atomic_uint_fast64_t dispatched_routes; /**< Completed ready-route dispatches. */
    atomic_int lifecycle;                 /**< Current session lifecycle state. */
    atomic_int error;                     /**< Last route-dispatch or worker error. */
    atomic_bool stopping;                 /**< Internal worker stop request. */
    bool routes_initialized;              /**< Internal route-sequence state. */
    bool routes_borrowed;                 /**< Internal route-buffer loan state. */
    bool conditions_initialized;          /**< Internal active-condition sequence state. */
    bool initialized;                     /**< Internal WaitSet/guard setup state. */
    bool wake_attached;                   /**< Internal wake-condition attachment state. */
    bool started;                         /**< True after worker start succeeds. */
};

#define T PGW_Session
#define TSeq PGW_SessionSeq
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
/** @brief Sequence of caller-owned session objects borrowed by a service. */
typedef struct PGW_SessionSeq PGW_SessionSeq;

#if defined(PGW_ENABLE_REMOTE_CONTROL)
#define PGW_CONTROL_ABI_VERSION 2u
#define PGW_CONTROL_MAX_COMMANDS_PER_BATCH 4u

typedef enum {
    PGW_CONTROL_CONNECTION_UP,
    PGW_CONTROL_CONNECTION_DOWN,
    PGW_CONTROL_INPUT_ENABLE,
    PGW_CONTROL_INPUT_DISABLE,
    PGW_CONTROL_OUTPUT_ENABLE,
    PGW_CONTROL_OUTPUT_DISABLE,
    PGW_CONTROL_ROUTE_PAUSE,
    PGW_CONTROL_ROUTE_RESUME
} PGW_ControlAction;

typedef enum {
    PGW_CONTROL_RESOURCE_CONNECTION,
    PGW_CONTROL_RESOURCE_INPUT,
    PGW_CONTROL_RESOURCE_OUTPUT,
    PGW_CONTROL_RESOURCE_ROUTE
} PGW_ControlResourceKind;

typedef enum {
    PGW_CONTROL_STATUS_UNKNOWN,
    PGW_CONTROL_STATUS_UP,
    PGW_CONTROL_STATUS_DOWN,
    PGW_CONTROL_STATUS_PAUSED,
    PGW_CONTROL_STATUS_FAULTED
} PGW_ControlResourceStatus;

typedef enum {
    PGW_CONTROL_OUTCOME_APPLIED,
    PGW_CONTROL_OUTCOME_NO_CHANGE,
    PGW_CONTROL_OUTCOME_UNSUPPORTED,
    PGW_CONTROL_OUTCOME_INVALID,
    PGW_CONTROL_OUTCOME_FAILED
} PGW_ControlOutcome;

typedef enum {
    PGW_CONTROL_SCALAR_BOOLEAN,
    PGW_CONTROL_SCALAR_INT32,
    PGW_CONTROL_SCALAR_UINT32,
    PGW_CONTROL_SCALAR_INT64,
    PGW_CONTROL_SCALAR_UINT64,
    PGW_CONTROL_SCALAR_DOUBLE
} PGW_ControlScalarType;

typedef struct {
    PGW_ControlScalarType type;
    union {
        bool boolean_value;
        int32_t int32_value;
        uint32_t uint32_value;
        int64_t int64_value;
        uint64_t uint64_value;
        double double_value;
    } value;
} PGW_ControlScalar;

typedef struct {
    uint32_t metric_id;
    uint32_t resource_id;
    uint32_t telemetry_kind;
    PGW_ControlScalar scalar;
    char unit[33];
} PGW_ControlTelemetry;

#define PGW_CONTROL_ACTION_MASK(action) (UINT32_C(1) << (action))

typedef struct {
    uint32_t resource_id;
    PGW_ControlAction action;
} PGW_ControlCommand;

typedef struct {
    uint8_t publication_handle[16];
    int32_t publication_sequence_high;
    uint32_t publication_sequence_low;
} PGW_ControlCorrelation;

typedef struct {
    uint32_t resource_id;
    PGW_ControlResourceStatus status;
    uint32_t command_capabilities;
    uint32_t telemetry_capabilities;
} PGW_ControlState;

typedef struct {
    uint32_t resource_id;
    PGW_ControlAction action;
    PGW_ControlOutcome outcome;
    PGW_ControlCorrelation correlation;
} PGW_ControlResult;

/** Optional, versioned adapter operations described by a linked capability
 * manifest. The manifest is portable across all instances of the adapter;
 * the service selects a supported subset for each configured resource.
 */
typedef struct PGW_ControlAdapterI {
    uint32_t version;
    size_t size;
    uint32_t manifest_version;
    uint32_t resource_kind_mask;
    uint32_t action_mask;
    PGW_Status (*apply)(void *, PGW_ControlAction);
    uint32_t telemetry_metric_mask;
    PGW_Status (*read_telemetry)(void *, const char *, PGW_ControlScalar *);
} PGW_ControlAdapterI;

/** Nonblocking command/state/result transport operations. Implementations
 * return one command per take call and must not retain callback arguments.
 */
typedef struct {
    uint32_t version;
    size_t size;
    /** Register/unregister command readiness on the explicitly selected
     * session. Unregister waits for callbacks already using the listener.
     */
    PGW_Status (*register_listener)(void *, const PGW_ReaderListener *);
    PGW_Status (*unregister_listener)(void *, const PGW_ReaderListener *);
    PGW_Status (*take_command)(void *, PGW_ControlCommand *,
                               PGW_ControlCorrelation *);
    /** Re-notify if a bounded command batch leaves commands available. */
    PGW_Status (*rearm_commands)(void *);
    PGW_Status (*write_state)(void *, const PGW_ControlState *);
    PGW_Status (*write_result)(void *, const PGW_ControlResult *);
    PGW_Status (*write_telemetry)(void *, const PGW_ControlTelemetry *);
} PGW_ControlEndpointI;

typedef struct {
    void *state;
    const PGW_ControlEndpointI *iface;
} PGW_ControlEndpoint;

/** Caller-owned selected resource and its latest logical state. Routes use
 * core actions directly; other resource kinds require versioned adapter ops.
 */
typedef struct {
    uint32_t id;
    PGW_ControlResourceKind kind;
    uint32_t command_capabilities;
    uint32_t telemetry_capabilities;
    PGW_ControlResourceStatus status;
    PGW_Route *route;
    const PGW_ControlAdapterI *adapter;
    void *adapter_state;
    bool state_dirty;
} PGW_ControlResource;

typedef struct {
    uint32_t id;
    uint32_t resource_id;
    uint32_t telemetry_kind;
    uint32_t adapter_metric_bit;
    const char *name;
    const char *unit;
    PGW_ControlScalarType scalar_type;
    const PGW_ControlAdapterI *adapter;
    void *adapter_state;
} PGW_ControlTelemetryMetric;

typedef struct {
    uint64_t commands_processed;
    uint64_t command_read_failures;
    uint64_t commands_invalid;
    uint64_t commands_unsupported;
    uint64_t commands_failed;
    uint64_t state_write_failures;
    uint64_t state_retries;
    uint64_t result_write_failures;
    uint64_t telemetry_read_failures;
    uint64_t telemetry_write_failures;
    uint64_t telemetry_clock_failures;
    uint64_t telemetry_samples;
} PGW_ControlCounters;
#endif

/** @brief Event-driven service over explicitly configured sessions.
 *
 * Configure every session and its routes before initialization. Service,
 * session, route, and sample storage are caller-owned; all borrowed buffers
 * outlive service finalization. Each session serializes its own route access.
 * Endpoint adapters synchronize endpoints shared across sessions. Lifecycle
 * calls are serialized by the owner; route pause/resume and diagnostic/counter
 * snapshots may be used concurrently with workers.
 */
typedef struct PGW_Service {
    PGW_SessionSeq sessions;         /**< Borrowed explicit session array. */
    size_t sample_budget;            /**< Maximum samples processed per route. */
    PGW_Diagnostics *diagnostics;    /**< Optional borrowed event sink. */
    atomic_int lifecycle;            /**< Current service lifecycle state. */
    /** Clock callback for diagnostic event timestamps and optional latency metrics.
     * Must be monotonic; required by PGW_ENABLE_ROUTE_LATENCY_METRICS builds.
     */
    bool (*clock_ns)(void *, uint64_t *);
    void *clock_state;               /**< Context passed to @c clock_ns. */
    bool sessions_initialized;       /**< Internal session-sequence state. */
    bool sessions_borrowed;          /**< Internal session-buffer loan state. */
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    PGW_ControlEndpoint control;         /**< Optional preconfigured control transport. */
    PGW_Session *control_session;         /**< Session that serializes control and timer events. */
    PGW_ReaderListener control_listener;  /**< Core-owned command readiness callback. */
    atomic_bool control_pending;          /**< Coalesced command-ready state. */
    bool control_listener_registered;     /**< Internal listener registration state. */
    atomic_flag control_lock;             /**< Protects control state and counter snapshots. */
    PGW_ControlResource *control_resources; /**< Borrowed selected resources. */
    size_t control_resource_count;       /**< Number of selected resources. */
    size_t control_state_retry_cursor;   /**< Internal dirty-state retry position. */
    PGW_ControlCounters control_counters; /**< Bounded control failure counters. */
    PGW_ControlTelemetryMetric *control_telemetry_metrics; /**< Borrowed static metric catalog. */
    size_t control_telemetry_metric_count;
    uint64_t control_telemetry_period_ns;
    uint64_t control_telemetry_last_ns;
    bool control_telemetry_clock_initialized;
#endif
} PGW_Service;

/** @brief Byte-level core storage estimate for configured sessions and routes. */
typedef struct {
    size_t objects_bytes;             /**< Service, session, and route objects. */
    size_t sample_references_bytes;   /**< Sample-reference arrays. */
    size_t write_results_bytes;       /**< Per-route write-result arrays. */
    size_t diagnostics_bytes;         /**< Optional diagnostic object and events. */
    size_t total_bytes;               /**< Sum of all reported storage categories. */
} PGW_CoreResourceReport;

#define T size_t
#define TSeq PGW_SizeSeq
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
/** @brief Sequence of per-route sample capacities for resource reporting. */
typedef struct PGW_SizeSeq PGW_SizeSeq;

/** @brief Add sizes with overflow checking.
 * The first two arguments are operands; the output pointer receives their sum
 * and is unchanged when null or when arithmetic overflows.
 * @return True on success, false for a null output or arithmetic overflow.
 */
bool PGW_size_add(size_t, size_t, size_t *);
/** @brief Multiply sizes with overflow checking.
 * The first two arguments are operands; the output pointer receives their
 * product and is unchanged when null or when arithmetic overflows.
 * @return True on success, false for a null output or arithmetic overflow.
 */
bool PGW_size_multiply(size_t, size_t, size_t *);
/** @brief Estimate core object, session, sample, result, and diagnostic storage.
 * Parameters: @c capacities is a non-empty sequence with one nonzero sample
 * capacity per route; @c sessions is the nonzero number of sessions;
 * @c diagnostics requests storage for a diagnostic ring;
 * @c events is its event capacity and must be zero when diagnostics is false;
 * @c out receives the report on success.
 * @return PGW_OK, PGW_INVALID for invalid arguments, or PGW_CAPACITY on overflow.
 */
PGW_Status PGW_core_resource_report(const PGW_SizeSeq *, size_t, bool, size_t,
                                   PGW_CoreResourceReport *);
/** @brief Allocate an aligned region from a caller-owned arena.
 * @c arena is advanced by the allocation and any alignment padding; @c bytes
 * is the requested size (zero is permitted); @c alignment must be a nonzero
 * power of two; @c out receives the address on success.
 * @return PGW_OK, PGW_INVALID for invalid arena/alignment, or PGW_CAPACITY
 *         when the remaining storage cannot satisfy the request.
 */
PGW_Status PGW_Arena_allocate(PGW_Arena *, size_t, size_t, void **);
/** @brief Compare schema identity by non-empty name, version, and fingerprint. */
bool PGW_schema_equal(const PGW_Schema *, const PGW_Schema *);
/** @brief Initialize a registry by borrowing the input sequence buffers.
 * Registration buffers must be contiguous and have capacity reserved in
 * advance. @p adapters and @p bindings remain caller-owned and must outlive
 * the registry. Serialize initialization, registration, lookup, and
 * finalization with respect to one another.
 * @return PGW_OK, PGW_INVALID for invalid/already initialized input, or
 *         PGW_FATAL if sequence initialization fails.
 */
PGW_Status PGW_Registry_initialize(PGW_Registry *, const PGW_AdapterSeq *,
                                  const PGW_RepresentationSeq *);
/** @brief Return registry loans and finalize its sequences.
 * @return PGW_OK on success; PGW_INVALID for an invalid state or
 *         PGW_LOAN_ERROR if a loan/finalization fails.
 */
PGW_Status PGW_Registry_finalize(PGW_Registry *);
/** @brief Register a unique, ABI-compatible adapter while the registry is mutable.
 * The adapter descriptor and its callbacks remain borrowed.
 * @return PGW_OK, PGW_INVALID for incompatible/duplicate/frozen input, or
 *         PGW_CAPACITY when the preallocated registry is full.
 */
PGW_Status PGW_Registry_register_adapter(PGW_Registry *, const PGW_AdapterI *);
/** @brief Register a unique, valid representation while the registry is mutable.
 * The representation and its referenced schema/access descriptors remain borrowed.
 * @return PGW_OK, PGW_INVALID for incompatible/duplicate/frozen input, or
 *         PGW_CAPACITY when the preallocated registry is full.
 */
PGW_Status PGW_Registry_register_binding(PGW_Registry *, const PGW_Representation *);
/** @brief Find an adapter by exact name; returns null when absent or invalid. */
const PGW_AdapterI *PGW_Registry_find_adapter(const PGW_Registry *, const char *);
/** @brief Find a representation by exact name; returns null when absent or invalid. */
const PGW_Representation *PGW_Registry_find_binding(const PGW_Registry *, const char *);
/** @brief Initialize route sample/result sequences over caller-provided buffers.
 * Both source sequences must be empty and contiguous; result capacity must be
 * at least the sample capacity. Their buffers are borrowed and must remain
 * valid until service finalization. The route must be UNINITIALIZED.
 */
PGW_Status PGW_Route_initialize_storage(PGW_Route *, const PGW_SampleSeq *,
                                       const PGW_WriteResultSeq *);
/** @brief Pause a ready/running route without changing service scheduling.
 * The operation is synchronous and allocation-free. Repeating pause returns
 * PGW_NO_CHANGE; a faulted route returns PGW_FATAL.
 */
PGW_Status PGW_Route_pause(PGW_Route *);
/** @brief Resume a paused route; it becomes eligible on the next service step.
 * The operation is synchronous and allocation-free. Repeating resume returns
 * PGW_NO_CHANGE; a faulted route returns PGW_FATAL.
 */
PGW_Status PGW_Route_resume(PGW_Route *);
/** @brief Configure a session to borrow a non-empty contiguous route array.
 * Call only while the session is uninitialized. The route array and nested
 * route storage remain caller-owned through service finalization.
 * @return PGW_OK, PGW_INVALID for an invalid state/sequence, PGW_FATAL if
 *         sequence initialization fails, or PGW_CAPACITY if the loan fails.
 */
PGW_Status PGW_Session_set_routes(PGW_Session *, const PGW_RouteSeq *);
/** @brief Configure a service to borrow a non-empty contiguous session array.
 * Every session must have routes configured before service initialization.
 * @return PGW_OK, PGW_INVALID for an invalid state/sequence, PGW_FATAL if
 *         sequence initialization fails, or PGW_CAPACITY if the loan fails.
 */
PGW_Status PGW_Service_set_sessions(PGW_Service *, const PGW_SessionSeq *);
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
/** @brief Copy bounded route batch-latency statistics.
 *
 * The build must enable PGW_ENABLE_ROUTE_LATENCY_METRICS and the service must
 * supply a monotonic clock callback before initialization. Timing covers the
 * reader callback through return from the writer callback for each non-empty
 * batch. Timing clock errors are counted and omit the duration without
 * changing route outcomes; diagnostic timestamp errors remain independent.
 */
PGW_Status PGW_Route_latency_snapshot(const PGW_Route *,
                                     PGW_RouteLatencySnapshot *);
#endif
#if defined(PGW_ENABLE_REMOTE_CONTROL)
/** @brief Freeze a non-empty control resource set and its optional endpoint.
 * Configure only before initialization. The resource array and adapter
 * contexts remain caller-owned; all callbacks are synchronous and nonblocking.
 */
PGW_Status PGW_Service_set_control(PGW_Service *, PGW_Session *,
                                  PGW_ControlEndpoint,
                                  PGW_ControlResource *, size_t);
/** @brief Copy bounded control write/processing counters. */
PGW_Status PGW_Service_control_counters(PGW_Service *,
                                       PGW_ControlCounters *);
/** @brief Select static telemetry metrics and freeze the runtime period.
 * A zero period disables publication. Nonzero periods must meet the configured
 * minimum and require the service's monotonic clock callback. The control
 * session's timed WaitSet wait delivers telemetry deadlines.
 */
PGW_Status PGW_Service_set_telemetry(PGW_Service *, PGW_ControlTelemetryMetric *,
    size_t, uint32_t, uint32_t);
#endif
/** @brief Initialize sessions, readiness conditions, and routes.
 * Requires configured session/route storage and a nonzero sample budget.
 * Validates route interfaces and storage, binds readiness and asks each writer
 * to negotiate its source representation. Builds with
 * PGW_ENABLE_ROUTE_LATENCY_METRICS also require a monotonic clock callback.
 * @return PGW_OK on success or a status describing invalid configuration,
 *         unsupported counters, or a route initialization failure.
 */
PGW_Status PGW_Service_initialize(PGW_Service *);
/** @brief Start one serialized worker for each configured session.
 * All workers wait for readiness conditions and are woken by session-owned
 * shutdown conditions. On partial start failure, already-started workers are
 * stopped and joined before an error is returned.
 */
PGW_Status PGW_Service_start(PGW_Service *);
/** @brief Wake and join all session workers and stop their routes.
 * @return PGW_OK on success; PGW_INVALID for an unsupported lifecycle state.
 */
PGW_Status PGW_Service_stop(PGW_Service *);
/** @brief Finalize a stopped service and return borrowed loans.
 * The service must be PGW_STOPPED. Caller-owned route and sequence backing
 * memory is not freed. Returns PGW_INVALID for any other lifecycle state and
 * PGW_LOAN_ERROR if a borrowed sequence cannot be released.
 */
PGW_Status PGW_Service_finalize(PGW_Service *);
/** @brief Return a static human-readable name for a status value.
 * The returned string is not owned by the caller.
 */
const char *PGW_status_name(PGW_Status);

/** @} */
#endif

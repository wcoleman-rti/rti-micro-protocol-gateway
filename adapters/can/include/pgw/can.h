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

#ifndef PGW_CAN_H
#define PGW_CAN_H

#include "pgw/core.h"
#include "pgw/signal.h"

/** @addtogroup pgw_can_api
 * @{
 */

/** Maximum payload size supported by a normalized CAN frame. */
#define PGW_CAN_MAX_PAYLOAD 64u
/** Frame uses a 29-bit extended identifier rather than an 11-bit identifier. */
#define PGW_CAN_FLAG_EXTENDED 1u
/** Frame is CAN FD rather than classic CAN. */
#define PGW_CAN_FLAG_FD 2u
/** Frame is a remote-transmission request. */
#define PGW_CAN_FLAG_RTR 4u
/** Frame represents a CAN error notification. */
#define PGW_CAN_FLAG_ERROR 8u
/** Frame is an echo of a locally transmitted frame. */
#define PGW_CAN_FLAG_ECHO 16u
/** CAN FD bit-rate switching is active. */
#define PGW_CAN_FLAG_BRS 32u
/** CAN FD error-state indicator is set. */
#define PGW_CAN_FLAG_ESI 64u

/** @brief CAN or CAN FD frame in the gateway's normalized format.
 * Standard identifiers fit 11 bits and extended identifiers fit 29 bits.
 * Classic frames have at most 8 data bytes; CAN FD frames have at most 64.
 * A timestamp may be absent. Frame payload and metadata are copied by value.
 */
typedef struct {
    uint32_t id;                         /**< Arbitration/error identifier. */
    uint32_t flags;                      /**< PGW_CAN_FLAG_* bit mask. */
    uint8_t length;                      /**< Number of payload bytes. */
    uint8_t data[PGW_CAN_MAX_PAYLOAD];   /**< Payload; bytes beyond length are unused. */
    PGW_Timestamp timestamp;             /**< Optional receive timestamp. */
    uint32_t interface_index;            /**< Platform interface index, if available. */
} PGW_CANFrame;

/** @brief CAN metadata retained with a decoded gateway sample. */
typedef struct {
    PGW_Timestamp timestamp;
    uint32_t frame_id;
    uint32_t flags;
    uint32_t interface_index;
} PGW_CANMetadata;

extern const unsigned char PGW_CAN_SIGNAL_VALUE_IDENTITY;
extern const unsigned char PGW_CAN_METADATA_IDENTITY;

#define REDA_SEQUENCE_USER_API
#define T PGW_CANFrame
#define TSeq PGW_CANFrameSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
/** @brief Sequence used for caller-owned CAN frame ring storage. */
typedef struct PGW_CANFrameSeq PGW_CANFrameSeq;

/** @brief Transport operations consumed by the CAN adapter.
 * Calls are expected to be serialized by the gateway. A receive operation
 * returns one frame at a time; PGW_NO_DATA means the receive queue is empty.
 * A close callback may be null when the transport has no resources to release.
 */
typedef struct {
    /** Receive one frame into @p frame. */
    PGW_Status (*receive)(void *, PGW_CANFrame *);
    /** Send one frame; the frame is borrowed only for this call. */
    PGW_Status (*send)(void *, const PGW_CANFrame *);
    /** Release transport resources, if required. */
    PGW_Status (*close)(void *);
} PGW_CANTransportI;

/** @brief Transport state and operations borrowed by a CAN connection.
 * The pointed-to state and interface table must remain valid until the
 * connection is closed.
 */
typedef struct {
    void *state;                        /**< Transport-owned state. */
    const PGW_CANTransportI *iface;     /**< Transport operation table. */
} PGW_CANTransport;

/** @brief Read-only alias for a CAN message descriptor. */
typedef const PGW_MessageDescriptor PGW_CANMessageDefinition;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANMessageDefinition
#define TSeq PGW_CANMessageDefinitionSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
/** @brief Sequence of borrowed message descriptor definitions. */
typedef struct PGW_CANMessageDefinitionSeq PGW_CANMessageDefinitionSeq;
/** @brief Read-only alias for a CAN signal descriptor. */
typedef const PGW_SignalDescriptor PGW_CANSignalDefinition;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANSignalDefinition
#define TSeq PGW_CANSignalDefinitionSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
/** @brief Sequence of borrowed signal descriptor definitions. */
typedef struct PGW_CANSignalDefinitionSeq PGW_CANSignalDefinitionSeq;

/** @brief Decoder callback for one CAN message payload.
 * @param context Caller-provided codec state.
 * @param message_index Index into the configured message-definition array.
 * @param data Frame payload, borrowed for this call.
 * @param length Payload length in bytes.
 * @param signals Writable output array.
 * @param capacity Number of available signal entries.
 * @param count Receives the number of output signals on success.
 * @return PGW_CODEC_OK or a codec error. The callback must not retain any
 *         input or output pointers.
 */
typedef PGW_CodecStatus (*PGW_CANDecode)(void *context, size_t message_index,
                                        const uint8_t *data, size_t length,
                                        PGW_Signal *signals, size_t capacity,
                                        size_t *count);
/** @brief Patch one signal value into a mutable frame payload.
 * @param context Caller-provided codec state.
 * @param signal_id Identifier of the signal to encode.
 * @param value Typed signal value to encode.
 * @param data Mutable payload buffer, borrowed for this call.
 * @param length Payload size in bytes.
 * @return PGW_CODEC_OK or a codec error; the callback must not retain pointers.
 */
typedef PGW_CodecStatus (*PGW_CANPatch)(void *context, uint32_t signal_id,
                                       const PGW_Value *value, uint8_t *data,
                                       size_t length);

/** @brief Borrowed message/signal definition tables and codec callbacks.
 * Initialize with caller-owned immutable definition arrays. Their elements,
 * strings, choice arrays, context, and callback code must remain valid through
 * mapping finalization and all connections that use the mapping.
 */
typedef struct {
    PGW_CANMessageDefinitionSeq messages; /**< Loaned message definitions. */
    PGW_CANSignalDefinitionSeq signals;   /**< Loaned signal definitions. */
    void *context;                        /**< Passed to codec callbacks. */
    PGW_CANDecode decode;                 /**< Required frame decoder. */
    PGW_CANPatch patch;                   /**< Required signal encoder. */
    bool initialized;                     /**< Internal initialization state. */
    bool messages_borrowed;               /**< Internal message-loan state. */
    bool signals_borrowed;                /**< Internal signal-loan state. */
} PGW_CANMapping;

/** @brief Output category backed by a bounded sample queue.
 * @c name and @c schema are borrowed. The capacity is the maximum number of
 * samples retained for this category when consumers apply backpressure.
 */
typedef struct {
    const char *name;           /**< Unique category name. */
    size_t capacity;            /**< Nonzero queued sample capacity. */
    const PGW_Schema *schema;   /**< Schema advertised for category samples. */
} PGW_CANCategory;
/** @brief Category definition element alias used in sequences. */
typedef PGW_CANCategory PGW_CANCategoryDefinition;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANCategoryDefinition
#define TSeq PGW_CANCategorySeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
/** @brief Sequence of category definitions borrowed by a CAN config. */
typedef struct PGW_CANCategorySeq PGW_CANCategorySeq;

/** @brief Configuration used to create a CAN connection.
 * This object refers to caller-owned transports, mappings, definitions, and
 * category storage. Keep those resources alive until connection close and
 * configuration finalization. Each connection owns one receiver thread;
 * receiver queue access and concurrent writers are serialized by the adapter.
 */
typedef struct {
    uint32_t entity_id;                    /**< Diagnostic entity identifier. */
    PGW_CANTransport transport;            /**< Borrowed transport handle. */
    PGW_CANMapping mapping;                /**< Initialized message/signal mapping. */
    PGW_CANCategorySeq categories;         /**< Borrowed output category definitions. */
    size_t receive_budget;                 /**< Maximum frames handled per receiver batch. */
    size_t write_capacity;                 /**< Maximum queued write commands. */
    PGW_Diagnostics *diagnostics;           /**< Optional borrowed diagnostic ring. */
    bool disable_metadata_capture;         /**< Suppress per-sample frame metadata. */
    bool categories_initialized;            /**< Internal sequence state. */
    bool categories_borrowed;               /**< Internal category-loan state. */
} PGW_CANConfig;

/** @brief Counters and high-water measurements for a CAN connection.
 * Values are copied by PGW_CAN_stats; they are not synchronized independently
 * of serialized adapter calls.
 */
typedef struct {
    uint64_t received_frames;       /**< Frames returned by the transport. */
    uint64_t decoded_samples;       /**< Decoded signal samples. */
    uint64_t receive_drops;         /**< Frames dropped during processing. */
    uint64_t unknown_frames;        /**< Frames with no configured definition. */
    uint64_t malformed_frames;      /**< Frames that fail validation/decoding. */
    uint64_t echoes;                /**< Locally echoed frames observed. */
    uint64_t missing_baseline;      /**< Writes rejected without a received baseline. */
    uint64_t invalid_commands;      /**< Invalid signal write commands. */
    uint64_t accepted_commands;     /**< Commands accepted by the transport. */
    uint64_t backpressure_commands; /**< Commands not sent due to backpressure. */
    uint64_t io_errors;             /**< Transport I/O failures. */
    size_t queue_high_water;        /**< Maximum observed category queue depth. */
    size_t mutable_bytes;           /**< Bytes of mutable frame staging storage. */
} PGW_CANStats;

/** @brief CAN adapter descriptor for registration in a PGW_Registry. */
extern const PGW_AdapterI PGW_CANAdapter;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
/** @brief Resolve a configured CAN connection/category to its control context. */
PGW_Status PGW_CAN_control_target(PGW_Connection *, PGW_ControlResourceKind,
                                  const char *, const PGW_ControlAdapterI **,
                                  void **);
#endif
/** @brief Initialize a mapping by loaning immutable definition arrays.
 * @param mapping Mapping storage, not already initialized.
 * @param messages Non-empty message definitions.
 * @param message_count Number of message definitions.
 * @param signals Non-empty signal definitions.
 * @param signal_count Number of signal definitions.
 * @param context Codec callback context (may be null if callbacks allow it).
 * @param decode Required decoder callback.
 * @param patch Required signal patch callback.
 * @return PGW_OK on success, PGW_INVALID for bad inputs, PGW_CAPACITY if a
 *         sequence cannot accept the loans, or PGW_FATAL on sequence setup failure.
 */
PGW_Status PGW_CANMapping_initialize(PGW_CANMapping *mapping,
    const PGW_MessageDescriptor *messages, size_t message_count,
    const PGW_SignalDescriptor *signals, size_t signal_count, void *context,
    PGW_CANDecode decode, PGW_CANPatch patch);
/** @brief Return mapping loans and finalize its sequences.
 * Definition storage remains caller-owned. Returns PGW_INVALID for a null
 * pointer and PGW_LOAN_ERROR if unloan/finalization fails.
 */
PGW_Status PGW_CANMapping_finalize(PGW_CANMapping *);
/** @brief Loan a non-empty category sequence into a CAN configuration.
 * The sequence's contiguous category storage remains borrowed until
 * PGW_CANConfig_finalize().
 */
PGW_Status PGW_CANConfig_set_categories(PGW_CANConfig *, const PGW_CANCategorySeq *);
/** @brief Finalize categories and mapping borrowed by a configuration.
 * @return PGW_OK on success, PGW_INVALID for null input, or PGW_LOAN_ERROR if
 *         any sequence loan/finalization fails.
 */
PGW_Status PGW_CANConfig_finalize(PGW_CANConfig *);
/** @brief Validate a normalized frame's flags, identifier, length, and timestamp.
 * @return True if the frame is structurally valid; false for null input,
 *         unknown flag bits, out-of-range identifiers, invalid classic/FD
 *         combinations, or invalid timestamp nanoseconds.
 */
bool PGW_CANFrame_valid(const PGW_CANFrame *);
/** @brief Calculate conservative arena storage needed by a CAN connection.
 * Includes alignment padding for the connection and all subobjects. Requires
 * a fully initialized mapping/category configuration and nonzero write capacity.
 * @param config Initialized CAN configuration.
 * @param bytes Receives required arena bytes.
 * @return PGW_OK on success, PGW_INVALID for an incomplete configuration, or
 *         PGW_CAPACITY on arithmetic overflow.
 */
PGW_Status PGW_CAN_storage_size(const PGW_CANConfig *config, size_t *bytes);
/** @brief Copy the connection's current CAN statistics.
 * The adapter synchronizes the snapshot with its receive thread and route
 * writers. @return PGW_OK on success or an error for invalid input/lock failure.
 */
PGW_Status PGW_CAN_stats(const PGW_Connection *, PGW_CANStats *);
/** @brief Retrieve the latest received baseline timestamp for a message index.
 * @param connection Open CAN connection.
 * @param message Index into the configured message-definition array.
 * @param out Receives a valid baseline timestamp.
 * @return PGW_OK when a baseline with valid timestamp exists; PGW_NO_DATA when
 *         no usable baseline exists; PGW_INVALID for invalid connection/index.
 */
PGW_Status PGW_CAN_baseline_timestamp(const PGW_Connection *connection,
                                     size_t message, PGW_Timestamp *out);
/** @brief Get the connection's counter collection.
 * The returned pointer is borrowed and remains valid only while the connection
 * is alive; returns null for a null connection.
 */
const PGW_Counters *PGW_CAN_counters(const PGW_Connection *);

/** @} */
#endif

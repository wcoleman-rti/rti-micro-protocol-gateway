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

#ifndef PGW_SIGNAL_H
#define PGW_SIGNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Native value arm used by a signal. */
typedef enum PGW_ValueKind {
    PGW_VALUE_BOOLEAN = 0, /**< Boolean value. */
    PGW_VALUE_INT64 = 1,   /**< Signed 64-bit engineering value. */
    PGW_VALUE_DOUBLE = 2   /**< Double-precision engineering value. */
} PGW_ValueKind;

/** @brief Typed value carried by a signal.
 * Read only the union member selected by @c kind.
 */
typedef struct PGW_Value {
    PGW_ValueKind kind; /**< Active member of @c data. */
    union {
        bool boolean;   /**< Value when kind is PGW_VALUE_BOOLEAN. */
        int64_t integer; /**< Value when kind is PGW_VALUE_INT64. */
        double real;    /**< Value when kind is PGW_VALUE_DOUBLE. */
    } data;
} PGW_Value;

/** @brief A signal value identified by its schema-defined signal ID. */
typedef struct PGW_Signal {
    uint32_t id;      /**< Signal identifier. */
    PGW_Value value;  /**< Typed engineering value. */
} PGW_Signal;

/** @brief Versioned identity of a signal schema.
 * Strings are borrowed, null-terminated, and must outlive descriptors using
 * this schema. The fingerprint identifies the complete schema contents.
 */
typedef struct PGW_SignalSchema {
    const char *name;         /**< Schema name. */
    uint32_t version;         /**< Schema version. */
    const char *fingerprint;  /**< Stable schema fingerprint. */
} PGW_SignalSchema;

/** @brief Label for one raw enumerated signal value.
 * Labels are borrowed strings; @c raw is the encoded, unscaled value.
 */
typedef struct PGW_SignalChoice {
    int64_t raw;         /**< Raw encoded value. */
    const char *label;   /**< Human-readable choice label. */
} PGW_SignalChoice;

/** @brief DBC-derived description of a signal's frame layout and value mapping.
 *
 * The descriptor is immutable after construction. String pointers and the
 * choices array are borrowed and must remain valid while the descriptor is in
 * use. @c start_bit and @c bit_length describe the encoded field; byte order
 * and signedness control raw extraction. Engineering values use the physical
 * range and, for integer values, the integer scale/offset mapping. For
 * multiplexed messages, @c multiplexer_index identifies the selector within
 * the descriptor array (negative means no selector) and @c multiplex_value
 * specifies the selector value that activates this branch.
 */
typedef struct PGW_SignalDescriptor {
    uint32_t id;                    /**< Unique signal identifier. */
    uint32_t message_index;          /**< Index into the message descriptor array. */
    const char *name;                /**< Signal name. */
    const char *category;            /**< Output category name. */
    const char *unit;                /**< Engineering unit; may be empty. */
    PGW_ValueKind kind;              /**< Engineering value type. */
    uint16_t start_bit;              /**< DBC start bit in the containing message. */
    uint8_t bit_length;              /**< Encoded field width in bits. */
    bool little_endian;              /**< True for little-endian bit numbering. */
    bool is_signed;                  /**< Whether the raw field is signed. */
    bool is_multiplexer;             /**< True when this signal selects a branch. */
    int64_t multiplex_value;         /**< Selector value required for this branch. */
    int32_t multiplexer_index;       /**< Selector descriptor index; negative if none. */
    double scale;                    /**< Physical-value scale applied to raw value. */
    double offset;                   /**< Physical-value offset applied to raw value. */
    double minimum;                  /**< Minimum allowed engineering value. */
    double maximum;                  /**< Maximum allowed engineering value. */
    int64_t raw_minimum;             /**< Minimum value representable by raw field. */
    int64_t raw_maximum;             /**< Maximum value representable by raw field. */
    int64_t integer_minimum;         /**< Minimum allowed integer engineering value. */
    int64_t integer_maximum;         /**< Maximum allowed integer engineering value. */
    int64_t integer_scale;           /**< Integer raw-to-engineering multiplier. */
    int64_t integer_offset;          /**< Integer raw-to-engineering offset. */
    const PGW_SignalChoice *choices; /**< Borrowed raw-value labels, if any. */
    size_t choice_count;             /**< Number of entries in @c choices. */
} PGW_SignalDescriptor;

/** @brief CAN message identity and payload layout.
 * The name is borrowed and must remain valid while the descriptor is used.
 */
typedef struct PGW_MessageDescriptor {
    uint32_t frame_id;      /**< CAN identifier without format flag bits. */
    const char *name;       /**< Message name. */
    uint8_t length;         /**< Expected payload length in bytes. */
    bool extended;          /**< Whether the identifier uses extended format. */
    bool fd;                /**< Whether this is a CAN FD message. */
    size_t signal_count;    /**< Number of descriptors associated with message. */
} PGW_MessageDescriptor;

/** @brief Outcomes reported by signal codec operations.
 * Codec callbacks return these codes instead of PGW_Status; callers map them
 * to gateway-level errors as appropriate.
 */
typedef enum PGW_CodecStatus {
    PGW_CODEC_OK = 0,              /**< Decode/patch completed successfully. */
    PGW_CODEC_INVALID_ARGUMENT,    /**< Null, inconsistent, or otherwise invalid input. */
    PGW_CODEC_UNKNOWN_MESSAGE,     /**< No descriptor matches the requested message. */
    PGW_CODEC_UNKNOWN_SIGNAL,      /**< No descriptor matches the signal ID. */
    PGW_CODEC_BUFFER_TOO_SMALL,    /**< Output buffer cannot hold decoded values/payload. */
    PGW_CODEC_WRONG_TYPE,          /**< Value kind does not match the signal descriptor. */
    PGW_CODEC_OUT_OF_RANGE,        /**< Raw or engineering value is outside the valid range. */
    PGW_CODEC_INACTIVE_MULTIPLEX,  /**< Signal branch is inactive for the current selector. */
    PGW_CODEC_SELECTOR_CHANGE      /**< Patch would change a multiplexer selector. */
} PGW_CodecStatus;

#ifdef __cplusplus
}
#endif
#endif

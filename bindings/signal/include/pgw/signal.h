#ifndef PGW_SIGNAL_H
#define PGW_SIGNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PGW_ValueKind {
    PGW_VALUE_BOOLEAN = 0,
    PGW_VALUE_INT64 = 1,
    PGW_VALUE_DOUBLE = 2
} PGW_ValueKind;

typedef struct PGW_Value {
    PGW_ValueKind kind;
    union {
        bool boolean;
        int64_t integer;
        double real;
    } data;
} PGW_Value;

typedef struct PGW_Signal {
    uint32_t id;
    PGW_Value value;
} PGW_Signal;

typedef struct PGW_SignalSchema {
    const char *name;
    uint32_t version;
    const char *fingerprint;
} PGW_SignalSchema;

typedef struct PGW_SignalChoice {
    int64_t raw;
    const char *label;
} PGW_SignalChoice;

typedef struct PGW_SignalDescriptor {
    uint32_t id;
    uint32_t message_index;
    const char *name;
    const char *category;
    const char *unit;
    PGW_ValueKind kind;
    uint16_t start_bit;
    uint8_t bit_length;
    bool little_endian;
    bool is_signed;
    bool is_multiplexer;
    int64_t multiplex_value;
    int32_t multiplexer_index;
    double scale;
    double offset;
    double minimum;
    double maximum;
    int64_t raw_minimum;
    int64_t raw_maximum;
    int64_t integer_minimum;
    int64_t integer_maximum;
    int64_t integer_scale;
    int64_t integer_offset;
    const PGW_SignalChoice *choices;
    size_t choice_count;
} PGW_SignalDescriptor;

typedef struct PGW_MessageDescriptor {
    uint32_t frame_id;
    const char *name;
    uint8_t length;
    bool extended;
    bool fd;
    size_t signal_count;
} PGW_MessageDescriptor;

typedef enum PGW_CodecStatus {
    PGW_CODEC_OK = 0,
    PGW_CODEC_INVALID_ARGUMENT,
    PGW_CODEC_UNKNOWN_MESSAGE,
    PGW_CODEC_UNKNOWN_SIGNAL,
    PGW_CODEC_BUFFER_TOO_SMALL,
    PGW_CODEC_WRONG_TYPE,
    PGW_CODEC_OUT_OF_RANGE,
    PGW_CODEC_INACTIVE_MULTIPLEX,
    PGW_CODEC_SELECTOR_CHANGE
} PGW_CodecStatus;

#ifdef __cplusplus
}
#endif
#endif

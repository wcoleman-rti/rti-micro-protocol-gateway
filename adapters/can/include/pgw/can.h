#ifndef PGW_CAN_H
#define PGW_CAN_H

#include "pgw/core.h"
#include "pgw/signal.h"

#define PGW_CAN_MAX_PAYLOAD 64u
#define PGW_CAN_FLAG_EXTENDED 1u
#define PGW_CAN_FLAG_FD 2u
#define PGW_CAN_FLAG_RTR 4u
#define PGW_CAN_FLAG_ERROR 8u
#define PGW_CAN_FLAG_ECHO 16u
#define PGW_CAN_FLAG_BRS 32u
#define PGW_CAN_FLAG_ESI 64u

typedef struct {
    uint32_t id;
    uint32_t flags;
    uint8_t length;
    uint8_t data[PGW_CAN_MAX_PAYLOAD];
    PGW_Timestamp timestamp;
    uint32_t interface_index;
} PGW_CANFrame;

#define REDA_SEQUENCE_USER_API
#define T PGW_CANFrame
#define TSeq PGW_CANFrameSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANFrameSeq PGW_CANFrameSeq;

typedef struct {
    PGW_Status (*receive)(void *, PGW_CANFrame *);
    PGW_Status (*send)(void *, const PGW_CANFrame *);
    PGW_Status (*close)(void *);
} PGW_CANTransportI;

typedef struct {
    void *state;
    const PGW_CANTransportI *iface;
} PGW_CANTransport;

typedef const PGW_MessageDescriptor PGW_CANMessageDefinition;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANMessageDefinition
#define TSeq PGW_CANMessageDefinitionSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANMessageDefinitionSeq PGW_CANMessageDefinitionSeq;
typedef const PGW_SignalDescriptor PGW_CANSignalDefinition;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANSignalDefinition
#define TSeq PGW_CANSignalDefinitionSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANSignalDefinitionSeq PGW_CANSignalDefinitionSeq;

typedef PGW_CodecStatus (*PGW_CANDecode)(void *, size_t, const uint8_t *, size_t,
                                        PGW_Signal *, size_t, size_t *);
typedef PGW_CodecStatus (*PGW_CANPatch)(void *, uint32_t, const PGW_Value *,
                                       uint8_t *, size_t);

typedef struct {
    PGW_CANMessageDefinitionSeq messages;
    PGW_CANSignalDefinitionSeq signals;
    void *context;
    PGW_CANDecode decode;
    PGW_CANPatch patch;
    bool initialized;
    bool messages_borrowed;
    bool signals_borrowed;
} PGW_CANMapping;

typedef struct {
    const char *name;
    size_t capacity;
    const PGW_Schema *schema;
} PGW_CANCategory;
typedef PGW_CANCategory PGW_CANCategoryDefinition;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANCategoryDefinition
#define TSeq PGW_CANCategorySeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANCategorySeq PGW_CANCategorySeq;

typedef struct {
    uint32_t entity_id;
    PGW_CANTransport transport;
    PGW_CANMapping mapping;
    PGW_CANCategorySeq categories;
    size_t receive_budget;
    size_t write_capacity;
    PGW_Diagnostics *diagnostics;
    bool disable_metadata_capture;
    bool categories_initialized;
    bool categories_borrowed;
} PGW_CANConfig;

typedef struct {
    uint64_t received_frames;
    uint64_t decoded_samples;
    uint64_t receive_drops;
    uint64_t unknown_frames;
    uint64_t malformed_frames;
    uint64_t echoes;
    uint64_t missing_baseline;
    uint64_t invalid_commands;
    uint64_t accepted_commands;
    uint64_t backpressure_commands;
    uint64_t io_errors;
    size_t queue_high_water;
    size_t mutable_bytes;
} PGW_CANStats;

extern const PGW_AdapterI PGW_CANAdapter;
PGW_Status PGW_CANMapping_initialize(PGW_CANMapping *,
    const PGW_MessageDescriptor *, size_t, const PGW_SignalDescriptor *, size_t,
    void *, PGW_CANDecode, PGW_CANPatch);
PGW_Status PGW_CANMapping_finalize(PGW_CANMapping *);
PGW_Status PGW_CANConfig_set_categories(PGW_CANConfig *, const PGW_CANCategorySeq *);
PGW_Status PGW_CANConfig_finalize(PGW_CANConfig *);
bool PGW_CANFrame_valid(const PGW_CANFrame *);
/* Conservative arena bytes, including worst-case initial/subobject padding. */
PGW_Status PGW_CAN_storage_size(const PGW_CANConfig *, size_t *);
PGW_Status PGW_CAN_poll(PGW_Connection *, size_t);
PGW_Status PGW_CAN_stats(const PGW_Connection *, PGW_CANStats *);
PGW_Status PGW_CAN_baseline_timestamp(const PGW_Connection *, size_t,
                                     PGW_Timestamp *);
const PGW_Counters *PGW_CAN_counters(const PGW_Connection *);

#endif

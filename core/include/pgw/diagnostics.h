#ifndef PGW_DIAGNOSTICS_H
#define PGW_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "pgw/sequence.h"

typedef enum {
    PGW_COUNT_RECEIVED, PGW_COUNT_ACCEPTED, PGW_COUNT_BACKPRESSURE,
    PGW_COUNT_INVALID, PGW_COUNT_FATAL, PGW_COUNT_LOANS,
    PGW_COUNT_LOAN_ERRORS, PGW_COUNT_FRAMES, PGW_COUNT_OVERFLOW,
    PGW_COUNT_EXPORT_ERRORS, PGW_COUNT_HIGH_WATER, PGW_COUNT_ROUTE_FAULTS,
    PGW_COUNT_TOTAL
} PGW_CounterId;

typedef struct {
    atomic_uint_fast64_t values[PGW_COUNT_TOTAL];
} PGW_Counters;

typedef struct {
    uint32_t version;
    uint32_t entity_id;
    uint64_t sequence;
    uint64_t collected_ns;
    uint64_t values[PGW_COUNT_TOTAL];
} PGW_CounterSnapshot;

typedef struct {
    uint64_t time_ns;
    bool time_valid;
    uint32_t entity_id;
    uint32_t code;
    uint32_t severity;
    uint64_t count;
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
typedef struct PGW_EventSeq PGW_EventSeq;

typedef struct {
    PGW_EventSeq events;
    size_t head;
    size_t count;
    atomic_flag lock;
    atomic_uint_fast64_t overflow;
    atomic_uint_fast64_t contention;
    atomic_uint_fast64_t drain_contention;
    atomic_uint_fast64_t rate_limited;
    uint32_t minimum_severity;
    uint64_t rate_interval_ns;
    size_t rate_max_events;
    uint64_t rate_window_ns;
    size_t rate_window_events;
    bool initialized;
    bool events_borrowed;
} PGW_Diagnostics;

typedef struct {
    uint32_t version;
    uint64_t collected_ns;
    size_t event_capacity;
    uint64_t overflow;
    uint64_t contention;
    uint64_t drain_contention;
    uint64_t rate_limited;
} PGW_DiagnosticSnapshot;

bool PGW_Counters_initialize(PGW_Counters *);
bool PGW_Counters_add(PGW_Counters *, PGW_CounterId, uint64_t);
bool PGW_Counters_snapshot(const PGW_Counters *, uint32_t, uint64_t,
                          uint64_t, PGW_CounterSnapshot *);
bool PGW_Diagnostics_initialize(PGW_Diagnostics *, const PGW_EventSeq *);
bool PGW_Diagnostics_finalize(PGW_Diagnostics *);
bool PGW_Diagnostics_configure_rate(PGW_Diagnostics *, uint64_t, size_t);
bool PGW_Diagnostics_snapshot(const PGW_Diagnostics *, uint64_t, PGW_DiagnosticSnapshot *);
bool PGW_Diagnostics_emit(PGW_Diagnostics *, const PGW_Event *);
bool PGW_Diagnostics_drain(PGW_Diagnostics *, PGW_EventSeq *);
bool PGW_snapshot_json(const PGW_CounterSnapshot *, char *, size_t, size_t *);

#endif

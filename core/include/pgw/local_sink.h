#ifndef PGW_LOCAL_SINK_H
#define PGW_LOCAL_SINK_H
#include "pgw/core.h"

typedef struct {
    int descriptor;
    char *buffer;
    size_t capacity;
    PGW_Counters counters;
} PGW_LocalSink;

/* Run consumers outside the routing thread: regular-file writes may block. */
PGW_Status PGW_LocalSink_initialize(PGW_LocalSink *, int, char *, size_t);
PGW_Status PGW_LocalSink_snapshot(PGW_LocalSink *, const PGW_CounterSnapshot *);
#endif

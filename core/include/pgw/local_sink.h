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

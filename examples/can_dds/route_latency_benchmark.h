/*
 * (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software. Licensee has the right to distribute object form only
 * for use with RTI products. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose.
 */

#ifndef PGW_EXAMPLE_ROUTE_LATENCY_BENCHMARK_H
#define PGW_EXAMPLE_ROUTE_LATENCY_BENCHMARK_H

#include "pgw/can_memory.h"
#include "pgw/core.h"

#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
bool PGW_example_route_latency_clock(void *, uint64_t *);
int PGW_example_benchmark_routed_translation(
    PGW_Service *, PGW_Route *, const PGW_SampleRepresentation *,
    PGW_Connection *, PGW_Connection *, PGW_Connection *,
    PGW_CANMemory *, const PGW_CANFrame *);
#endif

#endif

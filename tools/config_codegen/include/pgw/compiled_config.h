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

#ifndef PGW_COMPILED_CONFIG_H
#define PGW_COMPILED_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "reda/reda_sequence.h"
typedef struct PGW_CompiledNativeStream {
    const char *connection, *name, *endpoint, *binding;
    size_t capacity;
    bool reader;
} PGW_CompiledNativeStream;
typedef const PGW_CompiledNativeStream PGW_CompiledNativeStreamElement;
#define REDA_SEQUENCE_USER_API
#define T PGW_CompiledNativeStreamElement
#define TSeq PGW_CompiledNativeStreamSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CompiledNativeStreamSeq PGW_CompiledNativeStreamSeq;

typedef struct PGW_CompiledControlResource {
    uint32_t id;
    const char *kind;
    const char *name;
    const char *adapter;
    uint32_t command_capabilities;
    uint32_t telemetry_capabilities;
} PGW_CompiledControlResource;

extern const PGW_CompiledControlResource pgw_control_resources[];
extern const size_t pgw_control_resource_count;
typedef struct PGW_CompiledControlTelemetry {
    uint32_t id, resource_id, telemetry_kind, adapter_metric_bit;
    const char *resource_kind, *resource, *name, *unit, *adapter;
    uint32_t scalar_type;
} PGW_CompiledControlTelemetry;
extern const PGW_CompiledControlTelemetry pgw_control_telemetry_metrics[];
extern const size_t pgw_control_telemetry_metric_count;
extern const unsigned pgw_control_telemetry_minimum_period_ms;

typedef struct PGW_CompiledRoute {
    uint32_t id;
    const char *name, *input_connection, *input_stream;
    const char *output_connection, *output_stream;
} PGW_CompiledRoute;
typedef const PGW_CompiledRoute PGW_CompiledRouteElement;
#define REDA_SEQUENCE_USER_API
#define T PGW_CompiledRouteElement
#define TSeq PGW_CompiledRouteSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CompiledRouteSeq PGW_CompiledRouteSeq;

extern const PGW_CompiledRouteSeq pgw_config_routes;
typedef struct PGW_CompiledSession {
    const char *name;
    size_t route_offset, route_count;
} PGW_CompiledSession;
typedef const PGW_CompiledSession PGW_CompiledSessionElement;
#define REDA_SEQUENCE_USER_API
#define T PGW_CompiledSessionElement
#define TSeq PGW_CompiledSessionSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CompiledSessionSeq PGW_CompiledSessionSeq;
extern const PGW_CompiledSessionSeq pgw_config_sessions;
extern const char *const pgw_config_control_session;
extern const PGW_CompiledNativeStreamSeq pgw_config_native_streams;
extern const unsigned pgw_config_sample_budget;
#endif

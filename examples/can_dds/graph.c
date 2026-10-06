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

#include "graph.h"
#include "pgw/dds/connext_micro.h"
#include "pgw/can.h"
#include "pgw/compiled_config.h"
#if defined(PGW_ENABLE_REMOTE_CONTROL)
#include "control_resources.h"
#endif
#include <string.h>

#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"
#define REDA_SEQUENCE_USER_API
#define T PGW_CompiledNativeStreamElement
#define TSeq PGW_CompiledNativeStreamSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#define REDA_SEQUENCE_USER_API
#define T PGW_CompiledRouteElement
#define TSeq PGW_CompiledRouteSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

static const char *native_endpoint(const char *connection, const char *stream, bool reader)
{
    for (RTI_INT32 i = 0; i < PGW_CompiledNativeStreamSeq_get_length(&pgw_config_native_streams); ++i) {
        const PGW_CompiledNativeStream *entry =
            PGW_CompiledNativeStreamSeq_get_reference(&pgw_config_native_streams, i);
        if (!strcmp(entry->connection, connection) && !strcmp(entry->name, stream) &&
            entry->reader == reader) return entry->endpoint;
    }
    return NULL;
}
PGW_Status PGW_example_can_categories(const PGW_Schema *schema, PGW_CANCategorySeq *out)
{
    if (!schema || !out) return PGW_INVALID;
    RTI_INT32 capacity = PGW_CANCategorySeq_get_maximum(out);
    if (capacity <= 0 || !PGW_CANCategorySeq_get_contiguous_buffer(out) ||
        !PGW_CANCategorySeq_set_length(out, 0)) return PGW_INVALID;
    RTI_INT32 length = 0;
    for (RTI_INT32 i = 0; i < PGW_CompiledNativeStreamSeq_get_length(&pgw_config_native_streams); ++i) {
        const PGW_CompiledNativeStream *entry =
            PGW_CompiledNativeStreamSeq_get_reference(&pgw_config_native_streams, i);
        if (!entry->reader || strcmp(entry->connection, "can")) continue;
        if (length == capacity) {
            (void)PGW_CANCategorySeq_set_length(out, 0);
            return PGW_CAPACITY;
        }
        if (!PGW_CANCategorySeq_set_length(out, length + 1)) return PGW_CAPACITY;
        *PGW_CANCategorySeq_get_reference(out, length) =
            (PGW_CANCategory){entry->endpoint, entry->capacity, schema};
        ++length;
    }
    return length ? PGW_OK : PGW_INVALID;
}
PGW_Status PGW_example_attach_routes(PGW_Connection *can, PGW_Connection *dds,
                                    PGW_RouteSeq *routes)
{
    if (!routes || PGW_RouteSeq_get_length(routes) !=
        PGW_CompiledRouteSeq_get_length(&pgw_config_routes)) return PGW_CAPACITY;
    for (RTI_INT32 i = 0; i < PGW_RouteSeq_get_length(routes); ++i) {
        const PGW_CompiledRoute *route = PGW_CompiledRouteSeq_get_reference(&pgw_config_routes, i);
        PGW_Route *target = PGW_RouteSeq_get_reference(routes, i);
        PGW_Status status;
        target->id = route->id;
        if (!strcmp(route->input_connection, "gateway"))
            status = PGW_DDSConnextMicroAdapter.connection->reader(dds, route->input_stream,
                                                           &target->reader);
        else {
            const char *endpoint = native_endpoint(route->input_connection, route->input_stream, true);
            status = endpoint ? PGW_CANAdapter.connection->reader(can, endpoint,
                                     &target->reader) : PGW_INVALID;
        }
        if (status != PGW_OK) return status;
        if (!strcmp(route->output_connection, "gateway"))
            status = PGW_DDSConnextMicroAdapter.connection->writer(dds, route->output_stream,
                                                           &target->writer);
        else {
            const char *endpoint = native_endpoint(route->output_connection, route->output_stream, false);
            status = endpoint ? PGW_CANAdapter.connection->writer(can, endpoint,
                                     &target->writer) : PGW_INVALID;
        }
        if (status != PGW_OK) return status;
    }
    return PGW_OK;
}
#if defined(PGW_ENABLE_REMOTE_CONTROL)
PGW_Status PGW_example_control_resources(PGW_Connection *can, PGW_Connection *dds,
    PGW_Route *routes, size_t route_count, PGW_ControlResource *resources,
    size_t capacity, size_t *resource_count)
{
    if (!resources || !resource_count || capacity < pgw_control_resource_count ||
        (pgw_control_resource_count && (!can || !dds || !routes)))
        return PGW_INVALID;
    for (size_t i = 0; i < pgw_control_resource_count; ++i) {
        const PGW_CompiledControlResource *compiled =
            &pgw_control_resources[i];
        PGW_ControlResource *resource = &resources[i];
        *resource = (PGW_ControlResource){
            .id = compiled->id,
            .command_capabilities = compiled->command_capabilities,
            .telemetry_capabilities = compiled->telemetry_capabilities
        };
        PGW_Status status;
        if (!strcmp(compiled->kind, "route")) {
            resource->kind = PGW_CONTROL_RESOURCE_ROUTE;
            bool found = false;
            for (RTI_INT32 j = 0;
                 j < PGW_CompiledRouteSeq_get_length(&pgw_config_routes); ++j) {
                const PGW_CompiledRoute *route =
                    PGW_CompiledRouteSeq_get_reference(&pgw_config_routes, j);
                if (!strcmp(route->name, compiled->name) && (size_t)j < route_count) {
                    resource->route = &routes[j];
                    found = true;
                    break;
                }
            }
            if (!found) return PGW_INVALID;
        } else {
            PGW_Connection *connection =
                !strcmp(compiled->adapter, "can") ? can :
                !strcmp(compiled->adapter, "connext_micro") ? dds : NULL;
            PGW_ControlResourceKind kind;
            const char *resource_name = compiled->name;
            char stream_name[128];
            if (!connection) return PGW_UNSUPPORTED;
            if (!strcmp(compiled->kind, "connection")) {
                kind = PGW_CONTROL_RESOURCE_CONNECTION;
            } else if (!strcmp(compiled->kind, "input") ||
                       !strcmp(compiled->kind, "output")) {
                kind = !strcmp(compiled->kind, "input") ?
                    PGW_CONTROL_RESOURCE_INPUT : PGW_CONTROL_RESOURCE_OUTPUT;
                const char *separator = strstr(compiled->name, "::");
                if (!separator || strlen(separator + 2) >= sizeof(stream_name))
                    return PGW_INVALID;
                strcpy(stream_name, separator + 2);
                resource_name = stream_name;
                if (!strcmp(compiled->adapter, "can")) {
                    const char *native = native_endpoint(
                        "can", resource_name, kind == PGW_CONTROL_RESOURCE_INPUT);
                    if (!native) return PGW_INVALID;
                    resource_name = native;
                }
            } else return PGW_INVALID;
            resource->kind = kind;
            if (!strcmp(compiled->adapter, "can"))
                status = PGW_CAN_control_target(connection, kind, resource_name,
                    &resource->adapter, &resource->adapter_state);
            else
                status = PGW_DDS_control_target(connection, kind, resource_name,
                    &resource->adapter, &resource->adapter_state);
            if (status != PGW_OK) return status;
        }
    }
    *resource_count = pgw_control_resource_count;
    return PGW_OK;
}

PGW_Status PGW_example_control_telemetry(PGW_Connection *can, PGW_Connection *dds,
    PGW_ControlTelemetryMetric *metrics, size_t capacity, size_t *metric_count)
{
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT == 0
    (void)can;
    (void)dds;
    (void)metrics;
    (void)capacity;
    if (!metric_count) return PGW_INVALID;
    *metric_count = 0;
    return PGW_OK;
#else
    if (!metric_count || (pgw_control_telemetry_metric_count &&
        (!metrics || capacity < pgw_control_telemetry_metric_count)))
        return PGW_INVALID;
    for (size_t i = 0; i < pgw_control_telemetry_metric_count; ++i) {
        const PGW_CompiledControlTelemetry *compiled =
            &pgw_control_telemetry_metrics[i];
        PGW_ControlTelemetryMetric *metric = &metrics[i];
        PGW_ControlResourceKind kind;
        if (!strcmp(compiled->resource_kind, "connection"))
            kind = PGW_CONTROL_RESOURCE_CONNECTION;
        else if (!strcmp(compiled->resource_kind, "input"))
            kind = PGW_CONTROL_RESOURCE_INPUT;
        else if (!strcmp(compiled->resource_kind, "output"))
            kind = PGW_CONTROL_RESOURCE_OUTPUT;
        else return PGW_INVALID;
        PGW_Connection *connection =
            !strcmp(compiled->adapter, "can") ? can :
            !strcmp(compiled->adapter, "connext_micro") ? dds : NULL;
        if (!connection) return PGW_UNSUPPORTED;
        PGW_Status status;
        if (!strcmp(compiled->adapter, "can"))
            status = PGW_CAN_control_target(connection, kind, compiled->resource,
                &metric->adapter, &metric->adapter_state);
        else
            status = PGW_DDS_control_target(connection, kind, compiled->resource,
                &metric->adapter, &metric->adapter_state);
        if (status != PGW_OK) return status;
        *metric = (PGW_ControlTelemetryMetric){
            .id = compiled->id,
            .resource_id = compiled->resource_id,
            .telemetry_kind = compiled->telemetry_kind,
            .adapter_metric_bit = compiled->adapter_metric_bit,
            .name = compiled->name,
            .unit = compiled->unit,
            .scalar_type = (PGW_ControlScalarType)compiled->scalar_type,
            .adapter = metric->adapter,
            .adapter_state = metric->adapter_state
        };
    }
    *metric_count = pgw_control_telemetry_metric_count;
    return PGW_OK;
#endif
}
#endif

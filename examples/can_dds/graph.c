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

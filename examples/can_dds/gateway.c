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

#include "pgw/dds/connext_micro.h"
#include "pgw/can_socketcan.h"
#include "pgw/can_memory.h"
#include "pgw_codec.h"
#include "pgw/compiled_config.h"
#include "ddsAppgen.h"
#include "diagnostics_binding.h"
#include "graph.h"
#include "pgw/runtime.h"
#include "osapi/osapi_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(PGW_ENABLE_REMOTE_CONTROL)
#include "control_binding.h"
#include "control_resources.h"
#endif
extern const PGW_DDSConfig pgw_config_gateway;
extern const unsigned pgw_config_sample_budget;
static PGW_CodecStatus decode(void *context, size_t index, const uint8_t *bytes,
                             size_t length, PGW_Signal *values, size_t capacity, size_t *count)
{
    const PGW_MessageDescriptor *message = &PGW_codec_messages[index];
    (void)context;
    return PGW_codec_decode(message->frame_id, message->extended, message->fd,
                            bytes, length, values, capacity, count);
}
static PGW_CodecStatus patch(void *context, uint32_t id, const PGW_Value *value,
                            uint8_t *bytes, size_t length)
{
    PGW_Signal signal = {id, *value};
    (void)context;
    return PGW_codec_patch(&signal, bytes, length);
}

static bool release_route_storage(PGW_Route *route)
{
    if (!route->storage_initialized) return true;
    bool samples = !route->samples_borrowed || PGW_SampleSeq_unloan(&route->samples);
    bool results = !route->results_borrowed || PGW_WriteResultSeq_unloan(&route->results);
    if (samples) samples = PGW_SampleSeq_finalize(&route->samples);
    if (results) results = PGW_WriteResultSeq_finalize(&route->results);
    if (samples && results) {
        route->storage_initialized = false;
        route->samples_borrowed = route->results_borrowed = false;
        return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    const size_t memory_capacity = 262144;
    void *memory = NULL;
    PGW_Arena arena = {NULL, memory_capacity, 0};
    PGW_Connection *dds = NULL, *can = NULL;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    PGW_DDSRemoteControlOptions control_options = {
        PGW_DDS_REMOTE_CONTROL_OPTIONS_VERSION,
        sizeof(PGW_DDSRemoteControlOptions),
        false,
        0,
        PGW_CONTROL_MAX_CONTROLLER_PEERS,
        0,
        PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS
    };
    DDS_DomainParticipant *control_participant = NULL;
    PGW_DDSStaticEndpoint control_endpoints[
        3 + (PGW_CONTROL_TELEMETRY_METRIC_COUNT ? 1 : 0)];
    PGW_DDSControlTransport control_transport = {0};
    PGW_ControlResource control_resources[PGW_CONTROL_RESOURCE_COUNT];
    DDS_InstanceHandle_t control_state_handles[PGW_CONTROL_RESOURCE_COUNT];
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
    PGW_ControlTelemetryMetric control_telemetry_metrics[
        PGW_CONTROL_TELEMETRY_METRIC_COUNT];
    DDS_InstanceHandle_t control_telemetry_handles[
        PGW_CONTROL_TELEMETRY_METRIC_COUNT];
#else
    PGW_ControlTelemetryMetric *control_telemetry_metrics = NULL;
    DDS_InstanceHandle_t *control_telemetry_handles = NULL;
#endif
    size_t attached_control_resources = 0;
    size_t attached_telemetry_metrics = 0;
#endif
    PGW_CANSocket socket;
    PGW_CANMemory memory_transport;
    PGW_CANFrame memory_rx[8], memory_tx[8];
    PGW_CANFrame memory_baseline = {.id = 256, .length = 8,
        .data = {0x10, 0x27, 0, 0, 1, 1, 0xab, 0xcd}};
    PGW_CANSocketConfig socket_config = {.enable_fd = true};
    PGW_Schema schema = {PGW_codec_schema.name, PGW_codec_schema.version,
                         PGW_codec_schema.fingerprint};
    PGW_CANCategory categories[2];
    PGW_CANCategorySeq category_sequence;
    bool category_sequence_initialized = false, category_sequence_borrowed = false;
    PGW_CANConfig config = {.entity_id = 1,
        .receive_budget = pgw_config_can_receive_budget,
        .write_capacity = pgw_config_can_write_capacity};
    PGW_Route routes[4] = {{0}};
    PGW_RouteSeq route_sequence;
    bool route_sequence_initialized = false, route_sequence_borrowed = false;
    PGW_Session sessions[1] = {{0}};
    PGW_SessionSeq session_sequence;
    bool session_sequence_initialized = false, session_sequence_borrowed = false;
    PGW_SampleSeq route_samples[4];
    PGW_WriteResultSeq route_results[4];
    bool samples_initialized[4] = {false}, samples_borrowed[4] = {false};
    bool results_initialized[4] = {false}, results_borrowed[4] = {false};
    PGW_SampleRef refs[4][8];
    PGW_WriteResult results[4][8];
    PGW_Service service = {.sample_budget = 8,
                          .clock_ns = PGW_Runtime_monotonic_clock};
    unsigned long duration_ms = 10000;
#if PGW_DDS_DIAGNOSTICS
    PGW_StreamWriter exporter;
#endif
    int failed = 1;
    bool use_memory;
    bool closed = true;
    if (argc < 2) {
        fprintf(stderr, "usage: %s (--memory | explicitly-selected-CAN-interface)"
                " [--control-domain id] [duration-ms]\n", argv[0]);
        return 2;
    }
    bool duration_seen = false;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--control-domain")) {
#if defined(PGW_ENABLE_REMOTE_CONTROL)
            if (control_options.enabled || i + 1 >= argc) return 2;
            char *end;
            long domain = strtol(argv[++i], &end, 10);
            if (*end || domain < 0 || domain > 232) return 2;
            control_options.enabled = true;
            control_options.domain_id = (DDS_DomainId_t)domain;
#else
            fprintf(stderr, "remote control is not compiled into this gateway\n");
            return 2;
#endif
        } else if (!strcmp(argv[i], "--telemetry-period-ms")) {
#if defined(PGW_ENABLE_REMOTE_CONTROL)
            if (i + 1 >= argc) return 2;
            char *end;
            unsigned long period = strtoul(argv[++i], &end, 10);
            if (*end || period > UINT32_MAX) return 2;
            control_options.telemetry_period_ms = (uint32_t)period;
#else
            fprintf(stderr, "remote control is not compiled into this gateway\n");
            return 2;
#endif
        } else {
            if (duration_seen) return 2;
            char *end;
            duration_ms = strtoul(argv[i], &end, 10);
            if (!duration_ms || *end || duration_ms > 100000000) return 2;
            duration_seen = true;
        }
    }
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (PGW_DDS_remote_control_options_validate(&control_options,
            PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0,
            PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS) != PGW_OK)
        return 2;
#endif
    socket_config.interface_name = argv[1];
    use_memory = !strcmp(argv[1], "--memory");
    if (!PGW_Runtime_initialize()) return 1;
    if (!PGW_CANCategorySeq_initialize(&category_sequence)) goto done;
    category_sequence_initialized = true;
    if (!PGW_CANCategorySeq_loan_contiguous(&category_sequence, categories, 0, 2))
        goto done;
    category_sequence_borrowed = true;
    if (PGW_example_can_categories(&schema, &category_sequence) != PGW_OK) goto done;
    if (PGW_CANMapping_initialize(&config.mapping, PGW_codec_messages, PGW_codec_message_count,
            PGW_codec_signals, PGW_codec_signal_count, NULL, decode, patch) != PGW_OK ||
        PGW_CANConfig_set_categories(&config, &category_sequence) != PGW_OK)
        goto done;
    if (use_memory) {
        if (PGW_CANMemory_initialize(&memory_transport, memory_rx, 8, memory_tx, 8) != PGW_OK)
            goto done;
        config.transport = PGW_CANMemory_transport(&memory_transport);
    } else {
        if (!PGW_CANSocketFilterSeq_initialize(&socket_config.filters)) goto done;
        socket_config.filters_initialized = true;
        if (PGW_CANSocket_open(&socket, &socket_config) != PGW_OK) {
            fprintf(stderr, "cannot open explicitly selected CAN interface %s\n", argv[1]);
            goto done;
        }
        config.transport = PGW_CANSocket_transport(&socket);
    }
    memory = malloc(memory_capacity);
    if (!memory) goto done;
    arena.storage = memory;
    if (PGW_DDS_register_model(APPGEN_get_library_seq()) != PGW_OK ||
        PGW_DDSConnextMicroAdapter.create(&pgw_config_gateway, &arena, &dds) != PGW_OK ||
        PGW_CANAdapter.create(&config, &arena, &can) != PGW_OK) goto done;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (control_options.enabled) {
        if (PGW_CONTROL_TELEMETRY_METRIC_COUNT &&
            (PGW_example_control_telemetry(can, dds, control_telemetry_metrics,
                PGW_CONTROL_TELEMETRY_METRIC_COUNT, &attached_telemetry_metrics) != PGW_OK ||
             attached_telemetry_metrics != pgw_control_telemetry_metric_count))
            goto done;
        size_t endpoint_count = 0;
        if (PGW_DDS_create_control_participant(&control_options, control_endpoints,
                sizeof(control_endpoints) / sizeof(control_endpoints[0]),
                &endpoint_count, &control_participant) != PGW_OK ||
            PGW_DDS_control_transport_initialize(&control_transport,
                control_endpoints, endpoint_count,
                PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0,
                &PGW_example_control_types,
                pgw_control_resource_count, control_state_handles,
                PGW_CONTROL_RESOURCE_COUNT, control_telemetry_metrics,
                attached_telemetry_metrics, control_telemetry_handles,
                PGW_CONTROL_TELEMETRY_METRIC_COUNT) != PGW_OK)
            goto done;
        struct DDS_DomainParticipantQos control_qos =
            DDS_DomainParticipantQos_INITIALIZER;
        if (DDS_DomainParticipant_get_qos(control_participant, &control_qos) !=
            DDS_RETCODE_OK) goto done;
        size_t control_application_storage_bytes = sizeof(control_resources) +
            sizeof(control_state_handles) + sizeof(control_transport) +
            sizeof(control_endpoints);
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
        control_application_storage_bytes += sizeof(control_telemetry_metrics) +
            sizeof(control_telemetry_handles);
#endif
        printf("remote control: domain=%d resources=%zu endpoints=%zu "
               "local_readers=%d local_writers=%d local_topics=%d local_types=%d "
               "remote_participants=%d remote_readers=%d remote_writers=%d "
               "application_storage_bytes=%zu\n",
               (int)control_options.domain_id, pgw_control_resource_count, endpoint_count,
               control_qos.resource_limits.local_reader_allocation,
               control_qos.resource_limits.local_writer_allocation,
               control_qos.resource_limits.local_topic_allocation,
               control_qos.resource_limits.local_type_allocation,
               control_qos.resource_limits.remote_participant_allocation,
               control_qos.resource_limits.remote_reader_allocation,
               control_qos.resource_limits.remote_writer_allocation,
               control_application_storage_bytes);
        DDS_DomainParticipantQos_finalize(&control_qos);
    }
#endif
    {
        PGW_DDSResources resources;
        if (PGW_DDS_effective_resources(dds, &resources) != PGW_OK) goto done;
        printf("MAG effective: factory_participants=%d components=%d local_readers=%d "
               "local_writers=%d local_topics=%d remote_participants=%d remote_readers=%d "
               "remote_writers=%d adapter_arena_bytes=%zu (middleware memory not inferred)\n",
               resources.factory_participants, resources.factory_components, resources.local_readers,
               resources.local_writers, resources.local_topics, resources.remote_participants,
               resources.remote_readers, resources.remote_writers, resources.gateway_storage_bytes);
        for (RTI_INT32 i = 0; i < PGW_DDSEndpointConfigSeq_get_length(
                 &pgw_config_gateway.endpoints); ++i) {
            PGW_DDSHistoryResources history;
            const char *name = PGW_DDSEndpointConfigSeq_get_reference(
                &pgw_config_gateway.endpoints, i)->name;
            if (PGW_DDS_effective_history(dds, name, &history) != PGW_OK) goto done;
            printf("DDS %s: instances=%d samples=%d per_instance=%d depth=%d blocking=%d.%09u\n",
                   name, history.instances, history.samples, history.samples_per_instance,
                   history.history_depth, history.blocking_seconds, history.blocking_nanoseconds);
        }
        if (PGW_CompiledSessionSeq_get_length(&pgw_config_sessions) != 1 ||
            PGW_CompiledRouteSeq_get_length(&pgw_config_routes) != 4)
            goto done;
        const PGW_CompiledSession *compiled_session =
            PGW_CompiledSessionSeq_get_reference(&pgw_config_sessions, 0);
        if (!compiled_session || compiled_session->route_offset != 0 ||
            compiled_session->route_count != 4)
            goto done;
        sessions[0].name = compiled_session->name;
    }
#if PGW_DDS_DIAGNOSTICS
    if (PGW_DDSConnextMicroAdapter.connection->writer(dds, "diagnostics", &exporter) != PGW_OK ||
        exporter.iface->bind(exporter.state, PGW_diagnostics_binding.representation) != PGW_OK)
        goto done;
#endif
    for (RTI_INT32 i = 0; i < 4; ++i) {
        routes[i].id = (uint32_t)i + 1;
        if (!PGW_SampleSeq_initialize(&route_samples[i])) goto done;
        samples_initialized[i] = true;
        if (!PGW_SampleSeq_loan_contiguous(&route_samples[i], refs[i], 0, 8)) goto done;
        samples_borrowed[i] = true;
        if (!PGW_WriteResultSeq_initialize(&route_results[i])) goto done;
        results_initialized[i] = true;
        if (!PGW_WriteResultSeq_loan_contiguous(&route_results[i], results[i], 0, 8))
            goto done;
        results_borrowed[i] = true;
        if (PGW_Route_initialize_storage(&routes[i], &route_samples[i],
                                         &route_results[i]) != PGW_OK) goto done;
    }
    if (!PGW_RouteSeq_initialize(&route_sequence)) goto done;
    route_sequence_initialized = true;
    if (!PGW_RouteSeq_loan_contiguous(&route_sequence, routes, 4, 4)) goto done;
    route_sequence_borrowed = true;
    if (PGW_example_attach_routes(can, dds, &route_sequence) != PGW_OK) goto done;
    if (PGW_Session_set_routes(&sessions[0], &route_sequence) != PGW_OK)
        goto done;
    if (!PGW_SessionSeq_initialize(&session_sequence)) goto done;
    session_sequence_initialized = true;
    if (!PGW_SessionSeq_loan_contiguous(&session_sequence, sessions, 1, 1))
        goto done;
    session_sequence_borrowed = true;
    if (PGW_Service_set_sessions(&service, &session_sequence) != PGW_OK)
        goto done;
    service.sample_budget = pgw_config_sample_budget;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (control_options.enabled &&
        (PGW_example_control_resources(can, dds, routes, 4, control_resources,
                PGW_CONTROL_RESOURCE_COUNT, &attached_control_resources) != PGW_OK ||
         attached_control_resources != pgw_control_resource_count ||
         PGW_Service_set_control(&service,
             &sessions[0],
             PGW_DDS_control_endpoint(&control_transport),
             control_resources, attached_control_resources) != PGW_OK))
        goto done;
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
    if (control_options.enabled &&
        PGW_Service_set_telemetry(&service, control_telemetry_metrics,
                attached_telemetry_metrics, control_options.telemetry_period_ms,
                PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS) != PGW_OK)
        goto done;
#endif
#endif
    if (PGW_Service_initialize(&service) != PGW_OK) goto done;
    if (PGW_Service_start(&service) != PGW_OK) goto stopped;
    printf("gateway ready: interface=%s arena_bytes=%zu arena_reserved_bytes=%zu "
           "duration_ms=%lu sessions=%u\n",
           argv[1], arena.used, memory_capacity, duration_ms,
           (unsigned)PGW_CompiledSessionSeq_get_length(&pgw_config_sessions));
    uint64_t start_ns, now_ns, next_injection_ns = 0;
    if (!PGW_Runtime_monotonic_time_ns(&start_ns)) goto stopped;
    now_ns = start_ns;
    next_injection_ns = start_ns;
    while (now_ns - start_ns < (uint64_t)duration_ms * UINT64_C(1000000)) {
        if (!PGW_Runtime_monotonic_time_ns(&now_ns)) goto stopped;
        if (use_memory && now_ns >= next_injection_ns) {
            if (PGW_CANMemory_inject(&memory_transport, &memory_baseline) != PGW_OK)
                goto stopped;
            next_injection_ns = now_ns + UINT64_C(25000000);
        }
        OSAPI_Thread_sleep(1);
    }
    failed = 0;
stopped:
    if ((service.lifecycle == PGW_READY || service.lifecycle == PGW_RUNNING) &&
        PGW_Service_stop(&service) != PGW_OK) {
        closed = false;
        goto done;
    }
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (control_options.enabled) {
        PGW_ControlCounters counters;
        if (PGW_Service_control_counters(&service, &counters) == PGW_OK)
            printf("remote-control counters: commands=%llu state_write_failures=%llu "
                   "state_retries=%llu result_write_failures=%llu "
                   "telemetry_samples=%llu telemetry_read_failures=%llu "
                   "telemetry_write_failures=%llu telemetry_clock_failures=%llu\n",
                   (unsigned long long)counters.commands_processed,
                   (unsigned long long)counters.state_write_failures,
                   (unsigned long long)counters.state_retries,
                   (unsigned long long)counters.result_write_failures,
                   (unsigned long long)counters.telemetry_samples,
                   (unsigned long long)counters.telemetry_read_failures,
                   (unsigned long long)counters.telemetry_write_failures,
                   (unsigned long long)counters.telemetry_clock_failures);
    }
#endif
#if PGW_DDS_DIAGNOSTICS
    for (size_t i = 0; i < 4; ++i) {
        PGW_CounterSnapshot snapshot;
        uint64_t collected_ns;
        if (!PGW_Runtime_monotonic_time_ns(&collected_ns) ||
            !PGW_Counters_snapshot(&routes[i].counters, routes[i].id, 1,
                                   collected_ns, &snapshot) ||
            PGW_DDS_export_snapshot(&exporter, &snapshot) != PGW_OK)
            PGW_Counters_add(&routes[i].counters, PGW_COUNT_EXPORT_ERRORS, 1);
    }
#endif
    for (size_t i = 0; i < 4; ++i) {
        PGW_CounterSnapshot snapshot;
        char text[1024];
        size_t length;
        uint64_t collected_ns;
        if (PGW_Runtime_monotonic_time_ns(&collected_ns) &&
            PGW_Counters_snapshot(&routes[i].counters, routes[i].id, 1,
                collected_ns, &snapshot) &&
            PGW_snapshot_json(&snapshot, text, sizeof(text), &length))
            fwrite(text, 1, length, stdout);
    }
    if (PGW_Service_finalize(&service) != PGW_OK) {
        closed = false;
        goto done;
    }
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (control_transport.initialized &&
        PGW_DDS_control_transport_finalize(&control_transport) != PGW_OK) closed = false;
    if (control_participant &&
        PGW_DDS_delete_dynamic_participant(control_participant) != PGW_OK) closed = false;
#endif
    if (use_memory) {
        PGW_CANFrame frame;
        while (PGW_CANMemory_take_sent(&memory_transport, &frame) == PGW_OK) {
            printf("CAN memory sent id=%u bytes=", frame.id);
            for (size_t i = 0; i < frame.length; ++i) printf("%02x", frame.data[i]);
            putchar('\n');
        }
    }
done:
    if (service.lifecycle == PGW_READY || service.lifecycle == PGW_RUNNING) {
        if (PGW_Service_stop(&service) != PGW_OK ||
            PGW_Service_finalize(&service) != PGW_OK) closed = false;
    } else if (service.lifecycle == PGW_STOPPED &&
               PGW_Service_finalize(&service) != PGW_OK) {
        closed = false;
    } else if (service.lifecycle == PGW_UNINITIALIZED &&
               service.sessions_initialized &&
               PGW_Service_finalize(&service) != PGW_OK) {
        closed = false;
    }
    if (service.sessions_initialized) {
        closed = false;
        fprintf(stderr, "service sessions remain attached; leaving adapter connections open\n");
        return 1;
    }
    if (sessions[0].routes_initialized) {
        if (sessions[0].routes_borrowed &&
            !PGW_RouteSeq_unloan(&sessions[0].routes)) closed = false;
        if (!PGW_RouteSeq_finalize(&sessions[0].routes)) closed = false;
        sessions[0].routes_initialized = false;
        sessions[0].routes_borrowed = false;
    }
    if (session_sequence_borrowed &&
        !PGW_SessionSeq_unloan(&session_sequence)) closed = false;
    if (session_sequence_initialized &&
        !PGW_SessionSeq_finalize(&session_sequence)) closed = false;
    for (size_t i = 0; i < 4; ++i)
        if (!release_route_storage(&routes[i])) closed = false;
    if (route_sequence_borrowed && !PGW_RouteSeq_unloan(&route_sequence)) closed = false;
    if (route_sequence_initialized && !PGW_RouteSeq_finalize(&route_sequence)) closed = false;
    for (size_t i = 0; i < 4; ++i) {
        if (samples_borrowed[i] && !PGW_SampleSeq_unloan(&route_samples[i])) closed = false;
        if (samples_initialized[i] && !PGW_SampleSeq_finalize(&route_samples[i])) closed = false;
        if (results_borrowed[i] && !PGW_WriteResultSeq_unloan(&route_results[i])) closed = false;
        if (results_initialized[i] && !PGW_WriteResultSeq_finalize(&route_results[i])) closed = false;
    }
    if (can) {
        if (PGW_CANAdapter.connection->close(can) != PGW_OK) closed = false;
    }
    else if (config.transport.iface && config.transport.iface->close)
        (void)config.transport.iface->close(config.transport.state);
    (void)PGW_CANConfig_finalize(&config);
    if (category_sequence_borrowed && !PGW_CANCategorySeq_unloan(&category_sequence))
        closed = false;
    if (category_sequence_initialized && !PGW_CANCategorySeq_finalize(&category_sequence))
        closed = false;
    if (socket_config.filters_initialized &&
        PGW_CANSocketConfig_finalize(&socket_config) != PGW_OK) closed = false;
    if (dds && PGW_DDSConnextMicroAdapter.connection->close(dds) != PGW_OK) closed = false;
    if (closed) free(memory);
    else failed = 1;
    return failed;
}

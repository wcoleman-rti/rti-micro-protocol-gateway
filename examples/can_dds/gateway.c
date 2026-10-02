#include "pgw/dds_micro.h"
#include "pgw/can_socketcan.h"
#include "pgw/can_memory.h"
#include "pgw_codec.h"
#include "ddsAppgen.h"
#include "diagnostics_binding.h"
#include "graph.h"
#include "pgw/runtime.h"
#include "osapi/osapi_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const PGW_DDSConfig pgw_config_gateway;
extern const unsigned pgw_config_route_budget, pgw_config_sample_budget;
extern const unsigned pgw_config_diagnostic_period_steps;
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
    PGW_SampleSeq route_samples[4];
    PGW_WriteResultSeq route_results[4];
    bool samples_initialized[4] = {false}, samples_borrowed[4] = {false};
    bool results_initialized[4] = {false}, results_borrowed[4] = {false};
    PGW_SampleRef refs[4][8];
    PGW_WriteResult results[4][8];
    PGW_Service service = {.route_budget = 4, .sample_budget = 8,
                          .clock_ns = PGW_Runtime_monotonic_clock};
    unsigned long steps = 10000;
#if PGW_DDS_DIAGNOSTICS
    PGW_StreamWriter exporter;
#endif
    int failed = 1;
    bool use_memory;
    bool closed = true;
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s (--memory | explicitly-selected-CAN-interface) [steps]\n", argv[0]);
        return 2;
    }
    if (argc == 3) {
        char *end;
        steps = strtoul(argv[2], &end, 10);
        if (!steps || *end || steps > 100000000) return 2;
    }
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
        PGW_DDSMicroAdapter.create(&pgw_config_gateway, &arena, &dds) != PGW_OK ||
        PGW_CANAdapter.create(&config, &arena, &can) != PGW_OK) goto done;
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
    }
#if PGW_DDS_DIAGNOSTICS
    if (PGW_DDSMicroAdapter.connection->writer(dds, "diagnostics", &exporter) != PGW_OK ||
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
    if (PGW_Service_set_routes(&service, &route_sequence) != PGW_OK) goto done;
    service.route_budget = pgw_config_route_budget;
    service.sample_budget = pgw_config_sample_budget;
    if (PGW_Service_initialize(&service) != PGW_OK) goto done;
    printf("gateway ready: interface=%s arena_bytes=%zu arena_reserved_bytes=%zu steps=%lu\n",
           argv[1], arena.used, memory_capacity, steps);
    for (unsigned long i = 0; i < steps; ++i) {
        if (use_memory && i % 25 == 0 &&
            PGW_CANMemory_inject(&memory_transport, &memory_baseline) != PGW_OK)
            goto stopped;
        PGW_Status polled = PGW_CAN_poll(can, pgw_config_can_receive_budget);
        if ((polled != PGW_OK && polled != PGW_NO_DATA) ||
            PGW_Service_step(&service) != PGW_OK) goto stopped;
#if PGW_DDS_DIAGNOSTICS
        if (pgw_config_diagnostic_period_steps &&
            (i + 1) % pgw_config_diagnostic_period_steps == 0) {
            for (size_t route = 0; route < 4; ++route) {
                PGW_CounterSnapshot snapshot;
                uint64_t collected_ns;
                if (!PGW_Runtime_monotonic_time_ns(&collected_ns) ||
                    !PGW_Counters_snapshot(&routes[route].counters,
                        routes[route].id, i + 1, collected_ns, &snapshot) ||
                    PGW_DDS_export_snapshot(&exporter, &snapshot) != PGW_OK)
                    PGW_Counters_add(&routes[route].counters, PGW_COUNT_EXPORT_ERRORS, 1);
            }
        }
#endif
        OSAPI_Thread_sleep(1);
    }
    failed = 0;
stopped:
    (void)PGW_Service_stop(&service);
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
    (void)PGW_Service_finalize(&service);
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
    }
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
    if (dds && PGW_DDSMicroAdapter.connection->close(dds) != PGW_OK) closed = false;
    if (closed) free(memory);
    else failed = 1;
    return failed;
}

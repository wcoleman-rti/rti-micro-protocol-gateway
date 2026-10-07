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

#define _POSIX_C_SOURCE 200809L
#include "pgw/dds/connext_micro.h"
#include "pgw/can_memory.h"
#include "pgw/runtime.h"
#include "pgw/compiled_config.h"
#include "pgw_codec.h"
#include "probe_binding.h"
#include "ddsAppgen.h"
#include "diagnostics_binding.h"
#if defined(PGW_ENABLE_REMOTE_CONTROL)
#include "control_binding.h"
#include "control_resources.h"
#endif
#include "graph.h"
#include "allocation.h"
#include "signalsSupport.h"
#include "osapi/osapi_thread.h"
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
#include "route_latency_benchmark.h"
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>

extern const PGW_DDSConfig pgw_config_gateway, pgw_config_companion;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
extern const size_t pgw_control_resource_count;
#endif
static atomic_bool frozen;
static atomic_uint_fast64_t runtime_arena_calls;
PGW_Status __real_PGW_Arena_allocate(PGW_Arena *, size_t, size_t, void **);
PGW_Status __wrap_PGW_Arena_allocate(PGW_Arena *arena, size_t bytes,
                                    size_t alignment, void **out)
{
    if (atomic_load(&frozen)) atomic_fetch_add(&runtime_arena_calls, 1);
    return __real_PGW_Arena_allocate(arena, bytes, alignment, out);
}
#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "DDS requirement failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
    return 1; } } while (0)
typedef struct { PGW_Signal value; PGW_Timestamp timestamp; } SignalSample;
typedef struct { PGW_ProbeValue value; PGW_Timestamp timestamp; } ProbeSample;
static PGW_Status signal_copy(const PGW_Sample *opaque, void *out, size_t size)
{
    if (size != sizeof(PGW_Signal)) return PGW_INVALID;
    *(PGW_Signal *)out = ((const SignalSample *)opaque)->value;
    return PGW_OK;
}
static PGW_Status probe_copy(const PGW_Sample *opaque, void *out, size_t size)
{
    if (size != sizeof(PGW_ProbeValue)) return PGW_INVALID;
    *(PGW_ProbeValue *)out = ((const ProbeSample *)opaque)->value;
    return PGW_OK;
}
static PGW_Status probe_timestamp(const PGW_Sample *opaque, PGW_Timestamp *out)
{
    *out = ((const ProbeSample *)opaque)->timestamp;
    return PGW_OK;
}
static const PGW_SampleAccessI signal_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), signal_copy, NULL, NULL
};
static const PGW_SampleAccessI probe_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), probe_copy, probe_timestamp, NULL
};
static PGW_Representation benchmark_signal_representation;
static const PGW_SampleAccessI probe_payload_only = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), probe_copy, NULL, NULL
};
static bool benchmark_clock_ns(uint64_t *out)
{
    struct timespec time;
    if (!out || clock_gettime(CLOCK_MONOTONIC, &time) != 0) return false;
    *out = (uint64_t)time.tv_sec * UINT64_C(1000000000) +
        (uint64_t)time.tv_nsec;
    return true;
}

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
static int wait_matches(PGW_Connection *gateway, PGW_Connection *companion)
{
    DDS_DataWriter *a = DDS_DomainParticipant_lookup_datawriter_by_name(
        PGW_DDS_participant(gateway), "Out::StatePowertrain");
    DDS_DataWriter *b = DDS_DomainParticipant_lookup_datawriter_by_name(
        PGW_DDS_participant(companion), "Out::CommandPowertrain");
    DDS_DataWriter *c = DDS_DomainParticipant_lookup_datawriter_by_name(
        PGW_DDS_participant(gateway), "Out::Probe");
#if PGW_DDS_DIAGNOSTICS
    DDS_DataWriter *d = DDS_DomainParticipant_lookup_datawriter_by_name(
        PGW_DDS_participant(gateway), "Out::Diagnostics");
#endif
    for (unsigned i = 0; i < 200; ++i) {
        struct DDS_PublicationMatchedStatus sa, sb, sc;
#if PGW_DDS_DIAGNOSTICS
        struct DDS_PublicationMatchedStatus sd;
#endif
        if (DDS_DataWriter_get_publication_matched_status(a, &sa) == DDS_RETCODE_OK &&
            DDS_DataWriter_get_publication_matched_status(b, &sb) == DDS_RETCODE_OK &&
            DDS_DataWriter_get_publication_matched_status(c, &sc) == DDS_RETCODE_OK &&
#if PGW_DDS_DIAGNOSTICS
            DDS_DataWriter_get_publication_matched_status(d, &sd) == DDS_RETCODE_OK &&
            sd.current_count == 1 &&
#endif
            sa.current_count == 1 && sb.current_count == 1 &&
            sc.current_count == 1)
            return 0;
        OSAPI_Thread_sleep(25);
    }
    return 1;
}

static bool wait_route_counter(const PGW_Route *route, PGW_CounterId counter,
                               uint64_t expected)
{
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        if (atomic_load_explicit(&route->counters.values[counter],
                                 memory_order_acquire) >= expected)
            return true;
        OSAPI_Thread_sleep(1);
    }
    return false;
}

static int benchmark_write_batch(PGW_StreamWriter *writer,
                                 const PGW_SampleSeq *samples,
                                 PGW_WriteResultSeq *results,
                                 PGW_WriteResult *result, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) {
        if (writer->iface->write(writer->state, samples, results) != PGW_OK ||
            *result != PGW_WRITE_ACCEPTED)
            return 1;
    }
    return 0;
}

static int compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static int benchmark_dds_write_paths(PGW_Connection *gateway,
                                     PGW_Connection *companion)
{
    const uint64_t writes_per_round = 10000;
    const unsigned rounds = 5;
    PGW_StreamReader source_reader;
    PGW_StreamWriter ingress, output;
    PGW_SampleSeq input_samples, ingress_samples;
    PGW_SampleRef input_refs[1], ingress_ref;
    PGW_WriteResult output_result, ingress_result;
    PGW_WriteResultSeq output_results, ingress_results;
    PGW_SampleAccessI fallback_access;
    PGW_Representation fallback_representation, direct_alias;
    PGW_Schema direct_alias_schema = {
        "generated.type.alias", 99,
        "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
    };
    PGW_DDSStatistics before, after;
    SignalSample input = {
        .value = {1001, {.kind = PGW_VALUE_DOUBLE, .data.real = 123.4}}
    };
    uint64_t direct_times[5], converted_times[5];
    if (PGW_DDSConnextMicroAdapter.connection->reader(
            gateway, "command_powertrain", &source_reader) != PGW_OK ||
        PGW_DDSConnextMicroAdapter.connection->writer(
            gateway, "state_powertrain", &output) != PGW_OK ||
        PGW_DDSConnextMicroAdapter.connection->writer(
            companion, "command_powertrain", &ingress) != PGW_OK)
        return 1;

    benchmark_signal_representation = *ingress.representation;
    benchmark_signal_representation.access = &signal_access;
    if (ingress.iface->bind(ingress.state, &benchmark_signal_representation) != PGW_OK ||
        !PGW_SampleSeq_initialize(&input_samples) ||
        !PGW_SampleSeq_loan_contiguous(&input_samples, input_refs, 0, 1) ||
        !PGW_SampleSeq_initialize(&ingress_samples) ||
        !PGW_SampleSeq_loan_contiguous(&ingress_samples, &ingress_ref, 0, 1) ||
        !PGW_WriteResultSeq_initialize(&output_results) ||
        !PGW_WriteResultSeq_loan_contiguous(&output_results, &output_result, 0, 1) ||
        !PGW_WriteResultSeq_initialize(&ingress_results) ||
        !PGW_WriteResultSeq_loan_contiguous(&ingress_results, &ingress_result, 0, 1))
        return 1;

    ingress_ref = (const PGW_Sample *)&input;
    if (!PGW_SampleSeq_set_length(&ingress_samples, 1) ||
        ingress.iface->write(ingress.state, &ingress_samples, &ingress_results) != PGW_OK ||
        ingress_result != PGW_WRITE_ACCEPTED)
        return 1;

    PGW_Status read_status = PGW_NO_DATA;
    for (unsigned attempt = 0; attempt < 200 && read_status == PGW_NO_DATA; ++attempt) {
        read_status = source_reader.iface->read(source_reader.state, &input_samples, 1);
        if (read_status == PGW_NO_DATA) OSAPI_Thread_sleep(5);
    }
    if (read_status != PGW_OK || PGW_SampleSeq_get_length(&input_samples) != 1)
        return 1;
    fallback_access = *source_reader.representation->access;
    fallback_access.view = NULL;
    fallback_representation = *source_reader.representation;
    fallback_representation.access = &fallback_access;
    fallback_representation.view_contract = NULL;
    direct_alias = *source_reader.representation;
    direct_alias.schema = &direct_alias_schema;
    if (output.iface->bind(output.state, &direct_alias) != PGW_OK ||
        output.iface->bind(output.state, source_reader.representation) != PGW_OK ||
        benchmark_write_batch(&output, &input_samples, &output_results,
                              &output_result, 250) ||
        output.iface->bind(output.state, &fallback_representation) != PGW_OK ||
        benchmark_write_batch(&output, &input_samples, &output_results,
                              &output_result, 250) ||
        output.iface->bind(output.state, source_reader.representation) != PGW_OK ||
        PGW_DDS_statistics(gateway, "state_powertrain", &before) != PGW_OK)
        return 1;
    for (unsigned round = 0; round < rounds; ++round) {
        uint64_t direct_start, direct_end, converted_start, converted_end;
        bool direct_first = (round & 1u) == 0;
        if (direct_first) {
            if (output.iface->bind(output.state, source_reader.representation) != PGW_OK ||
                !benchmark_clock_ns(&direct_start) ||
                benchmark_write_batch(&output, &input_samples, &output_results,
                                      &output_result, writes_per_round) ||
                !benchmark_clock_ns(&direct_end) ||
                output.iface->bind(output.state, &fallback_representation) != PGW_OK ||
                !benchmark_clock_ns(&converted_start) ||
                benchmark_write_batch(&output, &input_samples, &output_results,
                                      &output_result, writes_per_round) ||
                !benchmark_clock_ns(&converted_end))
                return 1;
        } else {
            if (output.iface->bind(output.state, &fallback_representation) != PGW_OK ||
                !benchmark_clock_ns(&converted_start) ||
                benchmark_write_batch(&output, &input_samples, &output_results,
                                      &output_result, writes_per_round) ||
                !benchmark_clock_ns(&converted_end) ||
                output.iface->bind(output.state, source_reader.representation) != PGW_OK ||
                !benchmark_clock_ns(&direct_start) ||
                benchmark_write_batch(&output, &input_samples, &output_results,
                                      &output_result, writes_per_round) ||
                !benchmark_clock_ns(&direct_end))
                return 1;
        }
        direct_times[round] = direct_end - direct_start;
        converted_times[round] = converted_end - converted_start;
    }
    if (PGW_DDS_statistics(gateway, "state_powertrain", &after) != PGW_OK ||
        after.direct_write_attempts - before.direct_write_attempts !=
            rounds * writes_per_round ||
        after.converted_write_attempts - before.converted_write_attempts !=
            rounds * writes_per_round)
        return 1;
    qsort(direct_times, rounds, sizeof(direct_times[0]), compare_u64);
    qsort(converted_times, rounds, sizeof(converted_times[0]), compare_u64);
    uint64_t measured_per_path = rounds * writes_per_round;
    printf("Micro target DDS write-path benchmark: samples_per_path=%llu rounds=%u "
           "direct_median_ns_per_sample=%llu canonical_median_ns_per_sample=%llu\n",
           (unsigned long long)measured_per_path, rounds,
           (unsigned long long)(direct_times[rounds / 2] / writes_per_round),
           (unsigned long long)(converted_times[rounds / 2] / writes_per_round));

    if (source_reader.iface->return_loan(source_reader.state, &input_samples) != PGW_OK)
        return 1;
    if (output.iface->bind(output.state, source_reader.representation) != PGW_OK)
        return 1;
    if (!PGW_SampleSeq_unloan(&input_samples) ||
        !PGW_SampleSeq_finalize(&input_samples) ||
        !PGW_SampleSeq_unloan(&ingress_samples) ||
        !PGW_SampleSeq_finalize(&ingress_samples) ||
        !PGW_WriteResultSeq_unloan(&output_results) ||
        !PGW_WriteResultSeq_finalize(&output_results) ||
        !PGW_WriteResultSeq_unloan(&ingress_results) ||
        !PGW_WriteResultSeq_finalize(&ingress_results))
        return 1;
    return 0;
}

int main(void)
{
    const size_t memory_capacity = 262144;
    void *memory = malloc(memory_capacity);
    CHECK(memory);
    PGW_Arena arena = {memory, memory_capacity, 0};
    PGW_Connection *gateway, *companion, *can;
    PGW_DDSResources resources;
    PGW_DDSHistoryResources history;
    PGW_CANMemory transport;
    PGW_CANFrame rx[8], tx[8], sent;
    PGW_CANFrame baseline = {.id = 256, .length = 8,
        .data = {0x10, 0x27, 0x00, 0x00, 0x01, 0x01, 0xab, 0xcd}};
    PGW_Schema signal_schema = {PGW_codec_schema.name, PGW_codec_schema.version,
                               PGW_codec_schema.fingerprint};
    PGW_Schema alternate_route_schema = {
        "can.signal.alternate", 2,
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    };
    PGW_Representation canonical_route_source, cross_schema_route_source;
    PGW_CANCategory categories[2];
    PGW_CANCategorySeq category_sequence;
    PGW_CANConfig can_config = {
        .entity_id = 1,
        .receive_budget = pgw_config_can_receive_budget,
        .write_capacity = pgw_config_can_write_capacity};
    PGW_Route routes[4] = {{0}};
    PGW_RouteSeq route_sequence;
        PGW_Session session = {0};
        PGW_SessionSeq session_sequence;
    PGW_SampleSeq route_sample_storage[4];
    PGW_WriteResultSeq route_result_storage[4];
    PGW_SampleRef route_refs[4][8];
    PGW_WriteResult route_results[4][8];
    PGW_Service service = {
        .sample_budget = 8,
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
        .clock_ns = PGW_example_route_latency_clock,
#endif
    };
    PGW_StreamReader reader, probe_reader;
    PGW_StreamWriter command, probe_writer;
    PGW_SampleSeq loan;
    PGW_SampleRef refs[8];
    PGW_WriteResult result;
    PGW_WriteResultSeq result_sequence;
    PGW_Representation signal_rep = {&signal_schema, "test.signal", sizeof(SignalSample),
                                     _Alignof(SignalSample), &signal_access, NULL};
    PGW_Representation probe_rep = *PGW_probe_binding.representation;
    SignalSample update = {.value = {1001, {.kind = PGW_VALUE_DOUBLE,
                                            .data.real = 123.4}}};
    ProbeSample probe = {.value = {1, 42}, .timestamp = {true, true, 123, 456}};
    {
        const PGW_AdapterI *slots[1];
        PGW_RepresentationRef binding_slots[1];
        PGW_AdapterSeq adapter_sequence;
        PGW_RepresentationSeq binding_sequence;
        PGW_Registry registry = {0};
        CHECK(PGW_AdapterSeq_initialize(&adapter_sequence));
        CHECK(PGW_AdapterSeq_loan_contiguous(&adapter_sequence, slots, 0, 1));
        CHECK(PGW_RepresentationSeq_initialize(&binding_sequence));
        CHECK(PGW_RepresentationSeq_loan_contiguous(&binding_sequence, binding_slots, 0, 1));
        CHECK(PGW_Registry_initialize(&registry, &adapter_sequence, &binding_sequence) == PGW_OK);
        CHECK(PGW_DDS_register_adapter(&registry) == PGW_OK);
        CHECK(PGW_Registry_find_adapter(&registry, "connext_micro") == &PGW_DDSConnextMicroAdapter);
        CHECK(PGW_DDSConnextMicroAdapter.connection == &PGW_DDSConnextMicroConnection);
        CHECK(PGW_Registry_finalize(&registry) == PGW_OK);
        CHECK(PGW_AdapterSeq_unloan(&adapter_sequence));
        CHECK(PGW_AdapterSeq_finalize(&adapter_sequence));
        CHECK(PGW_RepresentationSeq_unloan(&binding_sequence));
        CHECK(PGW_RepresentationSeq_finalize(&binding_sequence));
    }
    PGW_allocation_monitor(true);
    {
        void *volatile control = malloc(32);
        CHECK(control);
        free(control);
        CHECK(PGW_allocation_calls() == 1);
    }
    PGW_allocation_monitor(false);
    CHECK(PGW_DDS_register_model(APPGEN_get_library_seq()) == PGW_OK);
    CHECK(PGW_DDS_register_model(APPGEN_get_library_seq()) == PGW_OK);
    {
        PGW_DDSEndpointConfig endpoint_storage[1];
        PGW_DDSEndpointConfigSeq endpoint_sequence;
        CHECK(PGW_DDSEndpointConfigSeq_initialize(&endpoint_sequence));
        CHECK(PGW_DDSEndpointConfigSeq_loan_contiguous(&endpoint_sequence,
                                                       endpoint_storage, 1, 1));
        CHECK(PGW_DDSEndpointConfigSeq_set_length(&endpoint_sequence, 1));
        endpoint_storage[0] = *PGW_DDSEndpointConfigSeq_get_reference(
            &pgw_config_gateway.endpoints, 0);
        PGW_DDSConfig invalid = {.participant_name = pgw_config_gateway.participant_name,
            .endpoints = endpoint_sequence, .endpoints_initialized = true};
        PGW_Connection *unused = NULL;
        size_t used = arena.used;
        endpoint_storage[0].binding = NULL;
        CHECK(PGW_DDSConnextMicroAdapter.create(&invalid, &arena, &unused) == PGW_INVALID);
        CHECK(arena.used == used && !unused);
        endpoint_storage[0] = *PGW_DDSEndpointConfigSeq_get_reference(
            &pgw_config_gateway.endpoints, 0);
        endpoint_storage[0].entity_name = "In::CommandPowertrain";
        CHECK(PGW_DDSConnextMicroAdapter.create(&invalid, &arena, &unused) == PGW_INVALID);
        CHECK(arena.used == used && !unused);
        CHECK(PGW_DDSEndpointConfigSeq_unloan(&invalid.endpoints));
        CHECK(PGW_DDSEndpointConfigSeq_finalize(&invalid.endpoints));
    }
    CHECK(PGW_DDS_create(&pgw_config_gateway, &arena, &gateway) == PGW_OK);
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    {
        DDS_DomainParticipant *dynamic_participant = NULL;
        PGW_DDSStaticEndpoint dynamic_endpoints[
            3 + (PGW_CONTROL_TELEMETRY_METRIC_COUNT ? 1 : 0)];
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
        PGW_DDSStaticEndpoint controller_endpoints[4];
        DDS_DomainParticipant *controller_participant = NULL;
        size_t controller_endpoint_count = 0;
#endif
        DDS_InstanceHandle_t state_handles[PGW_CONTROL_RESOURCE_COUNT];
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
        DDS_InstanceHandle_t telemetry_handles[PGW_CONTROL_TELEMETRY_METRIC_COUNT];
        PGW_ControlTelemetryMetric telemetry_metrics[PGW_CONTROL_TELEMETRY_METRIC_COUNT];
#else
        DDS_InstanceHandle_t *telemetry_handles = NULL;
        PGW_ControlTelemetryMetric *telemetry_metrics = NULL;
#endif
        PGW_DDSControlTransport control_transport = {0};
        size_t telemetry_definition_count = 0;
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
        telemetry_definition_count = pgw_control_telemetry_metric_count;
        for (size_t i = 0; i < telemetry_definition_count; ++i) {
            const PGW_CompiledControlTelemetry *compiled =
                &pgw_control_telemetry_metrics[i];
            telemetry_metrics[i] = (PGW_ControlTelemetryMetric){
                .id = compiled->id,
                .resource_id = compiled->resource_id,
                .telemetry_kind = compiled->telemetry_kind,
                .adapter_metric_bit = compiled->adapter_metric_bit,
                .name = compiled->name,
                .unit = compiled->unit,
                .scalar_type = (PGW_ControlScalarType)compiled->scalar_type
            };
        }
#endif
        size_t dynamic_endpoint_count = 0;
        PGW_DDSRemoteControlOptions control_options = {
            PGW_DDS_REMOTE_CONTROL_OPTIONS_VERSION,
            sizeof(PGW_DDSRemoteControlOptions), true,
            (DDS_DomainId_t)PGW_EXAMPLE_DYNAMIC_DOMAIN,
            PGW_CONTROL_MAX_CONTROLLER_PEERS, 0,
            PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS
        };
        CHECK(PGW_DDS_remote_control_options_validate(&control_options,
              PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0,
              PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS) == PGW_OK);
        if (PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0) {
            control_options.telemetry_period_ms = 10;
            CHECK(PGW_DDS_remote_control_options_validate(&control_options, true,
                  PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS) == PGW_INVALID);
            control_options.telemetry_period_ms = PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS;
            CHECK(PGW_DDS_remote_control_options_validate(&control_options, true,
                  PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS) == PGW_OK);
            control_options.telemetry_period_ms = 0;
        } else {
            control_options.telemetry_period_ms = 10;
            CHECK(PGW_DDS_remote_control_options_validate(&control_options, false, 0) ==
                  PGW_INVALID);
            control_options.telemetry_period_ms = 0;
        }
        CHECK(PGW_DDS_create_control_participant(&control_options,
              dynamic_endpoints,
              sizeof(dynamic_endpoints) / sizeof(dynamic_endpoints[0]),
              &dynamic_endpoint_count,
              &dynamic_participant) == PGW_OK);
        struct DDS_DomainParticipantQos control_participant_qos =
            DDS_DomainParticipantQos_INITIALIZER;
        CHECK(DDS_DomainParticipant_get_qos(dynamic_participant,
              &control_participant_qos) == DDS_RETCODE_OK);
        printf("Micro target dedicated participant limits: local_readers=%d "
               "local_writers=%d local_publishers=%d local_subscribers=%d "
               "local_topics=%d local_types=%d remote_participants=%d "
               "remote_readers=%d remote_writers=%d\n",
               control_participant_qos.resource_limits.local_reader_allocation,
               control_participant_qos.resource_limits.local_writer_allocation,
               control_participant_qos.resource_limits.local_publisher_allocation,
               control_participant_qos.resource_limits.local_subscriber_allocation,
               control_participant_qos.resource_limits.local_topic_allocation,
               control_participant_qos.resource_limits.local_type_allocation,
               control_participant_qos.resource_limits.remote_participant_allocation,
               control_participant_qos.resource_limits.remote_reader_allocation,
               control_participant_qos.resource_limits.remote_writer_allocation);
        CHECK(control_participant_qos.resource_limits.local_reader_allocation >= 1 &&
              control_participant_qos.resource_limits.local_writer_allocation >=
                  2 + (DDS_Long)PGW_CONTROL_TELEMETRY_METRIC_COUNT &&
              control_participant_qos.resource_limits.local_topic_allocation >=
                  3 + (DDS_Long)PGW_CONTROL_TELEMETRY_METRIC_COUNT &&
              control_participant_qos.resource_limits.local_type_allocation >=
                  3 + (DDS_Long)PGW_CONTROL_TELEMETRY_METRIC_COUNT &&
              control_participant_qos.resource_limits.remote_participant_allocation ==
                  (DDS_Long)PGW_CONTROL_MAX_CONTROLLER_PEERS &&
              control_participant_qos.resource_limits.remote_reader_allocation ==
                  (2 + (DDS_Long)PGW_CONTROL_TELEMETRY_METRIC_COUNT) *
                      (DDS_Long)PGW_CONTROL_MAX_CONTROLLER_PEERS &&
              control_participant_qos.resource_limits.remote_writer_allocation ==
                  (DDS_Long)PGW_CONTROL_MAX_CONTROLLER_PEERS);
        DDS_DomainParticipantQos_finalize(&control_participant_qos);
        CHECK(DDS_DomainParticipant_get_domain_id(dynamic_participant) ==
              (DDS_DomainId_t)PGW_EXAMPLE_DYNAMIC_DOMAIN);
        CHECK(DDS_DomainParticipant_lookup_topicdescription(
                  dynamic_participant, "ControlState") != NULL);
        CHECK(DDS_DomainParticipant_lookup_topicdescription(
                  dynamic_participant, "ControlCommand") != NULL);
        CHECK(DDS_DomainParticipant_lookup_topicdescription(
                  dynamic_participant, "ControlResult") != NULL);
        CHECK(dynamic_endpoint_count ==
              3 + PGW_CONTROL_TELEMETRY_METRIC_COUNT);
        DDS_DataWriter *state_writer = NULL, *result_writer = NULL,
                       *telemetry_writer = NULL;
        DDS_DataReader *command_reader = NULL;
        for (size_t i = 0; i < dynamic_endpoint_count; ++i) {
            if (!strcmp(dynamic_endpoints[i].name, "ControlState"))
                state_writer = dynamic_endpoints[i].datawriter;
            if (!strcmp(dynamic_endpoints[i].name, "ControlResult"))
                result_writer = dynamic_endpoints[i].datawriter;
            if (!strcmp(dynamic_endpoints[i].name, "ControlTelemetry"))
                telemetry_writer = dynamic_endpoints[i].datawriter;
            if (!strcmp(dynamic_endpoints[i].name, "ControlCommand"))
                command_reader = dynamic_endpoints[i].datareader;
        }
        CHECK(state_writer && result_writer && command_reader);
        CHECK((PGW_CONTROL_TELEMETRY_METRIC_COUNT != 0) == (telemetry_writer != NULL));
        struct DDS_DataWriterQos state_qos = DDS_DataWriterQos_INITIALIZER;
        CHECK(DDS_DataWriter_get_qos(state_writer, &state_qos) == DDS_RETCODE_OK);
        CHECK(state_qos.history.kind == DDS_KEEP_LAST_HISTORY_QOS &&
              state_qos.history.depth == 1 &&
              state_qos.durability.kind == DDS_TRANSIENT_LOCAL_DURABILITY_QOS &&
              state_qos.reliability.kind == DDS_RELIABLE_RELIABILITY_QOS &&
              state_qos.resource_limits.max_instances == (DDS_Long)pgw_control_resource_count &&
              state_qos.reliability.max_blocking_time.sec == 0 &&
              state_qos.reliability.max_blocking_time.nanosec == 0);
        DDS_Long control_state_depth = state_qos.history.depth;
        DDS_Long control_state_instances = state_qos.resource_limits.max_instances;
        DDS_Long control_state_samples = state_qos.resource_limits.max_samples;
        DDS_Long control_state_blocking_seconds =
            state_qos.reliability.max_blocking_time.sec;
        DDS_UnsignedLong control_state_blocking_nanoseconds =
            state_qos.reliability.max_blocking_time.nanosec;
        DDS_DataWriterQos_finalize(&state_qos);
        struct DDS_DataWriterQos result_qos = DDS_DataWriterQos_INITIALIZER;
        CHECK(DDS_DataWriter_get_qos(result_writer, &result_qos) == DDS_RETCODE_OK);
        CHECK(result_qos.history.depth == 4 &&
              result_qos.reliability.kind == DDS_RELIABLE_RELIABILITY_QOS &&
              result_qos.reliability.max_blocking_time.sec == 0 &&
              result_qos.reliability.max_blocking_time.nanosec == 0);
        DDS_Long control_result_depth = result_qos.history.depth;
        DDS_Long control_result_samples = result_qos.resource_limits.max_samples;
        DDS_Long control_result_blocking_seconds =
            result_qos.reliability.max_blocking_time.sec;
        DDS_UnsignedLong control_result_blocking_nanoseconds =
            result_qos.reliability.max_blocking_time.nanosec;
        DDS_DataWriterQos_finalize(&result_qos);
        struct DDS_DataReaderQos command_qos = DDS_DataReaderQos_INITIALIZER;
        CHECK(DDS_DataReader_get_qos(command_reader, &command_qos) == DDS_RETCODE_OK);
        CHECK(command_qos.history.depth == 4 &&
              command_qos.reliability.kind == DDS_RELIABLE_RELIABILITY_QOS &&
              command_qos.resource_limits.max_samples_per_instance == 4);
        DDS_Long control_command_depth = command_qos.history.depth;
        DDS_Long control_command_samples = command_qos.resource_limits.max_samples;
        printf("Micro target control profile: domain=%d resources=%u endpoints=%zu "
               "command_reader(depth=%d,samples=%d) "
               "state_writer(depth=%d,instances=%d,samples=%d,block=%d.%09u) "
               "result_writer(depth=%d,samples=%d,block=%d.%09u)\n",
               (int)PGW_EXAMPLE_DYNAMIC_DOMAIN, PGW_CONTROL_RESOURCE_COUNT,
               dynamic_endpoint_count,
               control_command_depth, control_command_samples,
               control_state_depth, control_state_instances, control_state_samples,
               control_state_blocking_seconds, control_state_blocking_nanoseconds,
               control_result_depth, control_result_samples,
               control_result_blocking_seconds, control_result_blocking_nanoseconds);
        DDS_DataReaderQos_finalize(&command_qos);
        if (telemetry_writer) {
            struct DDS_DataWriterQos telemetry_qos = DDS_DataWriterQos_INITIALIZER;
            CHECK(DDS_DataWriter_get_qos(telemetry_writer, &telemetry_qos) == DDS_RETCODE_OK);
            CHECK(telemetry_qos.history.depth == 1 &&
                  telemetry_qos.resource_limits.max_instances ==
                      (DDS_Long)PGW_CONTROL_TELEMETRY_METRIC_COUNT &&
                  telemetry_qos.reliability.kind == DDS_BEST_EFFORT_RELIABILITY_QOS &&
                  telemetry_qos.durability.kind == DDS_VOLATILE_DURABILITY_QOS);
            printf("Micro target telemetry profile: metrics=%u depth=%d instances=%d\n",
                   PGW_CONTROL_TELEMETRY_METRIC_COUNT, telemetry_qos.history.depth,
                   telemetry_qos.resource_limits.max_instances);
            DDS_DataWriterQos_finalize(&telemetry_qos);
        }
        CHECK(PGW_DDS_control_transport_initialize(&control_transport,
            dynamic_endpoints, dynamic_endpoint_count,
            PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0,
            &PGW_example_control_types, pgw_control_resource_count, state_handles,
            PGW_CONTROL_RESOURCE_COUNT, telemetry_metrics,
            telemetry_definition_count, telemetry_handles,
            PGW_CONTROL_TELEMETRY_METRIC_COUNT) == PGW_OK);
        PGW_ControlEndpoint control_endpoint =
            PGW_DDS_control_endpoint(&control_transport);
        PGW_ControlState initial_state = {
            0, PGW_CONTROL_STATUS_UNKNOWN, 3u, 0u
        };
        CHECK(control_endpoint.iface->write_state(
                  control_endpoint.state, &initial_state) == PGW_OK);
        PGW_ControlResult result_event = {
            .resource_id = 0,
            .action = PGW_CONTROL_CONNECTION_UP,
            .outcome = PGW_CONTROL_OUTCOME_APPLIED,
            .correlation = {{1}, 4, 5}
        };
        CHECK(control_endpoint.iface->write_result(
                  control_endpoint.state, &result_event) == PGW_OK);
        if (PGW_CONTROL_TELEMETRY_METRIC_COUNT) {
            PGW_ControlTelemetry telemetry = {
                0, 0, 0,
                {PGW_CONTROL_SCALAR_UINT64, {.uint64_value = 17}},
                "frames"
            };
            CHECK(control_endpoint.iface->write_telemetry(
                      control_endpoint.state, &telemetry) == PGW_OK);
        }
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
        CHECK(PGW_DDS_create_controller_participant(&control_options,
            controller_endpoints, 4, &controller_endpoint_count,
            &controller_participant) == PGW_OK);
        CHECK(controller_endpoint_count == 4);
        DDS_DataReader *telemetry_reader = NULL;
        for (size_t i = 0; i < controller_endpoint_count; ++i)
            if (!strcmp(controller_endpoints[i].name, "ControlTelemetry") &&
                controller_endpoints[i].reader)
                telemetry_reader = controller_endpoints[i].datareader;
        CHECK(telemetry_reader);
        struct DDS_PublicationMatchedStatus telemetry_matches;
        telemetry_matches.current_count = 0;
        for (unsigned attempt = 0; attempt < 200 &&
             telemetry_matches.current_count == 0; ++attempt) {
            CHECK(DDS_DataWriter_get_publication_matched_status(
                telemetry_writer, &telemetry_matches) == DDS_RETCODE_OK);
            if (!telemetry_matches.current_count) OSAPI_Thread_sleep(25);
        }
        CHECK(telemetry_matches.current_count == 1);
        const uint64_t write_count = 10000;
        PGW_ControlTelemetry measured_telemetry = {
            0, 0, 0,
            {PGW_CONTROL_SCALAR_UINT64, {0}},
            "frames"
        };
        uint64_t start_ns, finish_ns;
        CHECK(PGW_Runtime_monotonic_time_ns(&start_ns));
        PGW_allocation_monitor(true);
        for (uint64_t i = 0; i < write_count; ++i) {
            measured_telemetry.scalar.value.uint64_value = i;
            CHECK(control_endpoint.iface->write_telemetry(
                control_endpoint.state, &measured_telemetry) == PGW_OK);
        }
        PGW_allocation_monitor(false);
        CHECK(PGW_Runtime_monotonic_time_ns(&finish_ns));
        CHECK(finish_ns > start_ns);
        CHECK(PGW_allocation_calls() == 0 && PGW_osapi_allocation_calls() == 0);
        PGW_ControlTelemetry observed_telemetry = {0};
        PGW_Status telemetry_status = PGW_NO_DATA;
        for (unsigned attempt = 0; attempt < 200 && telemetry_status == PGW_NO_DATA; ++attempt) {
            telemetry_status = PGW_example_control_types.take_telemetry(
                telemetry_reader, &observed_telemetry);
            if (telemetry_status == PGW_NO_DATA) OSAPI_Thread_sleep(5);
        }
        CHECK(telemetry_status == PGW_OK);
        CHECK(observed_telemetry.resource_id == 0 &&
              observed_telemetry.telemetry_kind == 0 &&
              observed_telemetry.scalar.type == PGW_CONTROL_SCALAR_UINT64 &&
              observed_telemetry.scalar.value.uint64_value < write_count);
        printf("Micro target telemetry benchmark: writes=%llu elapsed_ns=%llu "
               "average_ns_per_write=%llu ceiling_hz=%llu allocation_calls=0\n",
               (unsigned long long)write_count,
               (unsigned long long)(finish_ns - start_ns),
               (unsigned long long)((finish_ns - start_ns) / write_count),
               (unsigned long long)(write_count * UINT64_C(1000000000) /
                                    (finish_ns - start_ns)));
        CHECK(PGW_DDS_delete_dynamic_participant(controller_participant) == PGW_OK);
#endif
        PGW_ControlCommand no_command;
        PGW_ControlCorrelation no_correlation;
        CHECK(control_endpoint.iface->take_command(control_endpoint.state,
            &no_command, &no_correlation) == PGW_NO_DATA);
        CHECK(PGW_DDS_control_transport_finalize(&control_transport) == PGW_OK);
        CHECK(PGW_DDS_delete_dynamic_participant(dynamic_participant) == PGW_OK);
    }
#endif
    CHECK(PGW_DDSConnextMicroAdapter.create(&pgw_config_companion, &arena, &companion) == PGW_OK);
    CHECK(PGW_DDS_effective_resources(gateway, &resources) == PGW_OK);
    CHECK(resources.local_readers > 0 && resources.local_writers > 0 &&
          resources.remote_readers > 0 && resources.remote_writers > 0 &&
          resources.factory_participants >= 2);
    CHECK(PGW_DDS_effective_history(gateway, "state_powertrain", &history) == PGW_OK);
    CHECK(history.instances == 4 && history.samples >= 4 &&
          history.samples_per_instance == 1 && history.history_depth == 1 &&
          history.blocking_seconds == 0 && history.blocking_nanoseconds == 0);
    printf("MAG effective gateway: readers=%d writers=%d topics=%d remote_participants=%d "
           "remote_readers=%d remote_writers=%d\n", resources.local_readers,
           resources.local_writers, resources.local_topics, resources.remote_participants,
           resources.remote_readers, resources.remote_writers);
    CHECK(wait_matches(gateway, companion) == 0);
    CHECK(PGW_CANMemory_initialize(&transport, rx, 8, tx, 8) == PGW_OK);
    CHECK(PGW_CANCategorySeq_initialize(&category_sequence));
    CHECK(PGW_CANCategorySeq_loan_contiguous(&category_sequence, categories, 2, 2));
    CHECK(PGW_example_can_categories(&signal_schema, &category_sequence) == PGW_OK);
    CHECK(PGW_CANMapping_initialize(&can_config.mapping, PGW_codec_messages,
        PGW_codec_message_count, PGW_codec_signals, PGW_codec_signal_count,
        NULL, decode, patch) == PGW_OK);
    CHECK(PGW_CANConfig_set_categories(&can_config, &category_sequence) == PGW_OK);
    can_config.transport = PGW_CANMemory_transport(&transport);
    CHECK(PGW_CANAdapter.create(&can_config, &arena, &can) == PGW_OK);
    CHECK(PGW_RouteSeq_initialize(&route_sequence));
    CHECK(PGW_RouteSeq_loan_contiguous(&route_sequence, routes, 4, 4));
    for (RTI_INT32 i = 0; i < 4; ++i) {
        routes[i].id = (uint32_t)i + 1;
        CHECK(PGW_SampleSeq_initialize(&route_sample_storage[i]));
        CHECK(PGW_SampleSeq_loan_contiguous(&route_sample_storage[i], route_refs[i], 0, 8));
        CHECK(PGW_WriteResultSeq_initialize(&route_result_storage[i]));
        CHECK(PGW_WriteResultSeq_loan_contiguous(&route_result_storage[i], route_results[i], 0, 8));
        CHECK(PGW_Route_initialize_storage(&routes[i], &route_sample_storage[i],
                                           &route_result_storage[i]) == PGW_OK);
    }
    CHECK(PGW_example_attach_routes(can, gateway, &route_sequence) == PGW_OK);
    canonical_route_source = *routes[0].reader.representation;
    cross_schema_route_source = canonical_route_source;
    cross_schema_route_source.schema = &alternate_route_schema;
    routes[0].reader.representation = &cross_schema_route_source;
    CHECK(PGW_CompiledSessionSeq_get_length(&pgw_config_sessions) == 1);
    const PGW_CompiledSession *compiled_session =
        PGW_CompiledSessionSeq_get_reference(&pgw_config_sessions, 0);
    CHECK(compiled_session && compiled_session->route_offset == 0 &&
          compiled_session->route_count == 4);
    session.name = compiled_session->name;
    CHECK(PGW_Session_set_routes(&session, &route_sequence) == PGW_OK);
    CHECK(PGW_SessionSeq_initialize(&session_sequence));
    CHECK(PGW_SessionSeq_loan_contiguous(&session_sequence, &session, 1, 1));
    CHECK(PGW_Service_set_sessions(&service, &session_sequence) == PGW_OK);
    CHECK(PGW_Service_initialize(&service) == PGW_OK);
    CHECK(PGW_Service_start(&service) == PGW_OK);
    {
        void *control;
        atomic_store(&frozen, true);
        CHECK(PGW_Arena_allocate(&arena, 1, 1, &control) == PGW_OK);
        CHECK(atomic_load(&runtime_arena_calls) == 1);
        atomic_store(&frozen, false);
        atomic_store(&runtime_arena_calls, 0);
    }
    CHECK(PGW_DDSConnextMicroAdapter.connection->reader(companion, "state_powertrain", &reader) == PGW_OK);
    CHECK(PGW_SampleSeq_initialize(&loan));
    CHECK(PGW_WriteResultSeq_initialize(&result_sequence));
    CHECK(PGW_WriteResultSeq_loan_contiguous(&result_sequence, &result, 0, 1));
    CHECK(PGW_SampleSeq_loan_contiguous(&loan, refs, 0, 8));
    size_t ready_arena_used = arena.used;
    atomic_store(&frozen, true);
    PGW_allocation_monitor(true);
    CHECK(PGW_CANMemory_inject(&transport, &baseline) == PGW_OK);
    bool received = false;
    unsigned state_keys = 0;
    for (unsigned attempt = 0; attempt < 100 && !received; ++attempt) {
        PGW_Status status = reader.iface->read(reader.state, &loan, 8);
        CHECK(status == PGW_OK || status == PGW_NO_DATA);
        if (status == PGW_OK) {
            for (RTI_INT32 i = 0; i < PGW_SampleSeq_get_length(&loan); ++i) {
                PGW_Signal value;
                CHECK(reader.representation->access->copy_value(*PGW_SampleSeq_get_reference(&loan, i),
                      &value, sizeof(value)) == PGW_OK);
                if (value.id == 1001) {
                    CHECK(value.value.kind == PGW_VALUE_DOUBLE);
                    CHECK(value.value.data.real == 1000.0);
                    state_keys |= 1u;
                } else if (value.id == 1002) {
                    CHECK(value.value.kind == PGW_VALUE_INT64 &&
                          value.value.data.integer == 0);
                    state_keys |= 2u;
                } else if (value.id == 1003) {
                    CHECK(value.value.kind == PGW_VALUE_BOOLEAN && value.value.data.boolean);
                    state_keys |= 4u;
                } else if (value.id == 1004) {
                    CHECK(value.value.kind == PGW_VALUE_INT64 &&
                          value.value.data.integer == 1);
                    state_keys |= 8u;
                }
            }
            CHECK(reader.iface->read(reader.state, &loan, 8) == PGW_LOAN_ERROR);
            CHECK(PGW_DDSConnextMicroAdapter.connection->close(companion) == PGW_LOAN_ERROR);
            CHECK(reader.iface->return_loan(reader.state, &loan) == PGW_OK);
            CHECK(reader.iface->return_loan(reader.state, &loan) == PGW_LOAN_ERROR);
            received = state_keys == 15u;
        }
        OSAPI_Thread_sleep(10);
    }
    CHECK(received);
#if PGW_DDS_DIAGNOSTICS
    {
        PGW_StreamWriter exporter;
        PGW_StreamReader subscriber;
        PGW_CounterSnapshot snapshot;
        CHECK(PGW_DDSConnextMicroAdapter.connection->writer(gateway, "diagnostics", &exporter) == PGW_OK);
        CHECK(exporter.iface->bind(exporter.state, PGW_diagnostics_binding.representation) == PGW_OK);
        CHECK(PGW_DDSConnextMicroAdapter.connection->reader(companion, "diagnostics", &subscriber) == PGW_OK);
        CHECK(PGW_Counters_snapshot(&routes[0].counters, 1, 17, 0, &snapshot));
        CHECK(snapshot.values[PGW_COUNT_ACCEPTED] == 4);
        snapshot.entity_id = 5;
        CHECK(PGW_DDS_export_snapshot(&exporter, &snapshot) == PGW_INVALID);
        PGW_Counters_add(&routes[0].counters, PGW_COUNT_EXPORT_ERRORS, 1);
        CHECK(PGW_Counters_snapshot(&routes[0].counters, 1, 18, 0, &snapshot));
        snapshot.values[PGW_COUNT_FATAL] = 2;
        snapshot.values[PGW_COUNT_ROUTE_FAULTS] = 1;
        CHECK(PGW_DDS_export_snapshot(&exporter, &snapshot) == PGW_OK);
        received = false;
        for (unsigned attempt = 0; attempt < 100 && !received; ++attempt) {
            PGW_Status status = subscriber.iface->read(subscriber.state, &loan, 4);
            CHECK(status == PGW_OK || status == PGW_NO_DATA);
            if (status == PGW_OK) {
                PGW_CounterSnapshot observed;
                CHECK(subscriber.representation->access->copy_value(
                    *PGW_SampleSeq_get_reference(&loan, 0), &observed, sizeof(observed)) == PGW_OK);
                CHECK(observed.entity_id == 1 && observed.sequence == 18);
                CHECK(observed.version == 1 && observed.version == snapshot.version);
                CHECK(observed.values[PGW_COUNT_FATAL] == 2 &&
                      observed.values[PGW_COUNT_ROUTE_FAULTS] == 1);
                CHECK(observed.values[PGW_COUNT_ACCEPTED] == 4 &&
                      observed.values[PGW_COUNT_EXPORT_ERRORS] == 1);
                CHECK(subscriber.iface->return_loan(subscriber.state, &loan) == PGW_OK);
                received = true;
            }
            OSAPI_Thread_sleep(10);
        }
        CHECK(received);
    }
#endif
    CHECK(PGW_DDSConnextMicroAdapter.connection->writer(companion, "command_powertrain", &command) == PGW_OK);
    CHECK(command.iface->bind(command.state, &signal_rep) == PGW_OK);
    CHECK(PGW_SampleSeq_set_length(&loan, 1));
    refs[0] = (const PGW_Sample *)&update;
    CHECK(command.iface->write(command.state, &loan, &result_sequence) == PGW_OK);
    CHECK(result == PGW_WRITE_ACCEPTED);
    CHECK(PGW_SampleSeq_set_length(&loan, 0));
    received = false;
    for (unsigned attempt = 0; attempt < 100 && !received; ++attempt) {
        if (PGW_CANMemory_take_sent(&transport, &sent) == PGW_OK) received = true;
        OSAPI_Thread_sleep(10);
    }
    CHECK(received);
    CHECK(sent.id == baseline.id && sent.length == baseline.length);
    CHECK(sent.data[0] == 0xd2 && sent.data[1] == 0x04);
    CHECK(memcmp(sent.data + 2, baseline.data + 2, 6) == 0);
    {
        PGW_DDS_Signal lifecycle;
        PGW_DDSStatistics statistics;
        DDS_DataWriter *writer = DDS_DomainParticipant_lookup_datawriter_by_name(
            PGW_DDS_participant(companion), "Out::CommandPowertrain");
        CHECK(PGW_DDS_Signal_initialize(&lifecycle));
        lifecycle.id = 1001;
        CHECK(PGW_DDS_SignalDataWriter_dispose(PGW_DDS_SignalDataWriter_narrow(writer),
                                             &lifecycle, &DDS_HANDLE_NIL) == DDS_RETCODE_OK);
        received = false;
        for (unsigned attempt = 0; attempt < 100 && !received; ++attempt) {
            CHECK(PGW_CANMemory_take_sent(&transport, &sent) == PGW_NO_DATA);
            CHECK(PGW_DDS_statistics(gateway, "command_powertrain", &statistics) == PGW_OK);
            received = statistics.lifecycle_samples > 0;
            OSAPI_Thread_sleep(10);
        }
        CHECK(received);
    }
    CHECK(PGW_DDSConnextMicroAdapter.connection->writer(gateway, "probe", &probe_writer) == PGW_OK);
    CHECK(PGW_DDSConnextMicroAdapter.connection->reader(companion, "probe", &probe_reader) == PGW_OK);
    probe_rep.access = &probe_access;
    CHECK(probe_writer.iface->bind(probe_writer.state, &signal_rep) == PGW_UNSUPPORTED);
    probe_rep.access = &probe_payload_only;
    CHECK(probe_writer.iface->bind(probe_writer.state, &probe_rep) == PGW_UNSUPPORTED);
    probe_rep.access = &probe_access;
    CHECK(probe_writer.iface->bind(probe_writer.state, &probe_rep) == PGW_OK);
    CHECK(PGW_SampleSeq_set_length(&loan, 1));
    refs[0] = (const PGW_Sample *)&probe;
    probe.timestamp.portable = false;
    CHECK(probe_writer.iface->write(probe_writer.state, &loan, &result_sequence) == PGW_OK);
    CHECK(result == PGW_WRITE_INVALID);
    probe.timestamp.portable = true;
    probe.timestamp.nanoseconds = 1000000000u;
    CHECK(probe_writer.iface->write(probe_writer.state, &loan, &result_sequence) == PGW_OK);
    CHECK(result == PGW_WRITE_INVALID);
    probe.timestamp.nanoseconds = 456;
    probe.timestamp.seconds = -1;
    CHECK(probe_writer.iface->write(probe_writer.state, &loan, &result_sequence) == PGW_OK);
    CHECK(result == PGW_WRITE_INVALID);
    probe.timestamp.seconds = INT64_C(2147483648);
    CHECK(probe_writer.iface->write(probe_writer.state, &loan, &result_sequence) == PGW_OK);
    CHECK(result == PGW_WRITE_INVALID);
    probe.timestamp.seconds = 123;
    CHECK(probe_writer.iface->write(probe_writer.state, &loan, &result_sequence) == PGW_OK);
    CHECK(result == PGW_WRITE_ACCEPTED);
    CHECK(PGW_SampleSeq_set_length(&loan, 0));
    received = false;
    for (unsigned attempt = 0; attempt < 100 && !received; ++attempt) {
        PGW_Status status = probe_reader.iface->read(probe_reader.state, &loan, 1);
        CHECK(status == PGW_OK || status == PGW_NO_DATA);
        if (status == PGW_OK) {
            PGW_ProbeValue value;
            PGW_Timestamp timestamp;
            PGW_DDSMetadata metadata;
            CHECK(probe_reader.representation->access->copy_value(
                *PGW_SampleSeq_get_reference(&loan, 0), &value, sizeof(value)) == PGW_OK);
            CHECK(value.id == 1 && value.reading == 42);
            CHECK(probe_reader.representation->access->source_timestamp(
                *PGW_SampleSeq_get_reference(&loan, 0), &timestamp) == PGW_OK);
            CHECK(timestamp.valid && timestamp.portable &&
                  timestamp.seconds == 123 && timestamp.nanoseconds == 456);
            CHECK(PGW_DDS_metadata(probe_reader.representation,
                *PGW_SampleSeq_get_reference(&loan, 0), &metadata) == PGW_OK);
            CHECK(metadata.valid_data && metadata.source_timestamp.sec == 123 &&
                  metadata.source_timestamp.nanosec == 456 &&
                  metadata.publication_sequence_number.low > 0);
            CHECK(probe_reader.iface->return_loan(probe_reader.state, &loan) == PGW_OK);
            received = true;
        }
        OSAPI_Thread_sleep(10);
    }
    CHECK(received);
    {
        PGW_DDSStatistics statistics;
        CHECK(PGW_DDS_statistics(gateway, "probe", &statistics) == PGW_OK);
        CHECK(statistics.accepted == 1 && statistics.invalid == 4 &&
              statistics.matched == 1 && statistics.incompatible_qos == 0);
        CHECK(PGW_DDS_statistics(companion, "state_powertrain", &statistics) == PGW_OK);
        CHECK(statistics.valid_samples == 4 && statistics.loans >= 1 &&
              statistics.loan_errors >= 1 && statistics.matched == 1);
    }
    for (unsigned step = 0; step < 1000; ++step) {
        CHECK(PGW_CANMemory_inject(&transport, &baseline) == PGW_OK);
        CHECK(wait_route_counter(&routes[0], PGW_COUNT_ACCEPTED,
            UINT64_C(4) + (uint64_t)(step + 1) * 4));
    }
    {
        PGW_CounterSnapshot snapshot;
        CHECK(PGW_Counters_snapshot(&routes[0].counters, 1, 19, 0, &snapshot));
        CHECK(snapshot.values[PGW_COUNT_ACCEPTED] == 4004);
        CHECK(snapshot.values[PGW_COUNT_LOANS] == 0);
        CHECK(snapshot.values[PGW_COUNT_FATAL] == 0);
    }
    CHECK(arena.used == ready_arena_used && atomic_load(&runtime_arena_calls) == 0);
    {
        uint64_t libc = PGW_allocation_calls(), osapi = PGW_osapi_allocation_calls();
        PGW_allocation_monitor(false);
        atomic_store(&frozen, false);
        printf("Runtime allocation coverage: gateway arena requests=0; "
               "observed libc=%llu OSAPI=%llu (process-wide middleware-inclusive; "
               "libc internal and kernel allocation not intercepted)\n",
               (unsigned long long)libc, (unsigned long long)osapi);
    }
#if defined(PGW_ENABLE_ROUTE_LATENCY_METRICS)
    CHECK(PGW_example_benchmark_routed_translation(
        &service, &routes[0], &canonical_route_source, can, gateway,
        companion, &transport, &baseline) == 0);
#endif
    CHECK(PGW_Service_stop(&service) == PGW_OK);
    CHECK(PGW_Service_finalize(&service) == PGW_OK);
    CHECK(PGW_SessionSeq_unloan(&session_sequence));
    CHECK(PGW_SessionSeq_finalize(&session_sequence));
    CHECK(PGW_RouteSeq_unloan(&route_sequence));
    CHECK(PGW_RouteSeq_finalize(&route_sequence));
    for (RTI_INT32 i = 0; i < 4; ++i) {
        CHECK(PGW_SampleSeq_unloan(&route_sample_storage[i]));
        CHECK(PGW_SampleSeq_finalize(&route_sample_storage[i]));
        CHECK(PGW_WriteResultSeq_unloan(&route_result_storage[i]));
        CHECK(PGW_WriteResultSeq_finalize(&route_result_storage[i]));
    }
    CHECK(PGW_SampleSeq_unloan(&loan));
    CHECK(PGW_SampleSeq_finalize(&loan));
    CHECK(PGW_WriteResultSeq_unloan(&result_sequence));
    CHECK(PGW_WriteResultSeq_finalize(&result_sequence));
    CHECK(benchmark_dds_write_paths(gateway, companion) == 0);
    CHECK(PGW_CANAdapter.connection->close(can) == PGW_OK);
    CHECK(PGW_CANConfig_finalize(&can_config) == PGW_OK);
    CHECK(PGW_CANCategorySeq_unloan(&category_sequence));
    CHECK(PGW_CANCategorySeq_finalize(&category_sequence));
    CHECK(PGW_DDSConnextMicroAdapter.connection->close(companion) == PGW_OK);
    CHECK(PGW_DDSConnextMicroAdapter.connection->close(gateway) == PGW_OK);
    free(memory);
    printf("Arena backing: fixed initialization malloc bytes=%zu; freed after shutdown\n",
           memory_capacity);
    puts("PASS: actual MAG DDS, CAN state/command bytes, second schema, timestamps and diagnostic export isolation");
    return 0;
}

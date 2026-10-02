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
#include "pgw/can_memory.h"
#include "pgw_codec.h"
#include "probe_binding.h"
#include "ddsAppgen.h"
#include "diagnostics_binding.h"
#include "graph.h"
#include "allocation.h"
#include "signalsSupport.h"
#include "osapi/osapi_thread.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

extern const PGW_DDSConfig pgw_config_gateway, pgw_config_companion;
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
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), signal_copy, NULL
};
static const PGW_SampleAccessI probe_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), probe_copy, probe_timestamp
};
static const PGW_SampleAccessI probe_payload_only = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), probe_copy, NULL
};
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
    PGW_CANCategory categories[2];
    PGW_CANCategorySeq category_sequence;
    PGW_CANConfig can_config = {
        .entity_id = 1,
        .receive_budget = pgw_config_can_receive_budget,
        .write_capacity = pgw_config_can_write_capacity};
    PGW_Route routes[4] = {{0}};
    PGW_RouteSeq route_sequence;
    PGW_SampleSeq route_sample_storage[4];
    PGW_WriteResultSeq route_result_storage[4];
    PGW_SampleRef route_refs[4][8];
    PGW_WriteResult route_results[4][8];
    PGW_Service service = {.route_budget = 4, .sample_budget = 8};
    PGW_StreamReader reader, probe_reader;
    PGW_StreamWriter command, probe_writer;
    PGW_SampleSeq loan;
    PGW_SampleRef refs[8];
    PGW_WriteResult result;
    PGW_WriteResultSeq result_sequence;
    PGW_Representation signal_rep = {&signal_schema, "test.signal", sizeof(SignalSample),
                                     _Alignof(SignalSample), &signal_access};
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
    CHECK(PGW_Service_set_routes(&service, &route_sequence) == PGW_OK);
    CHECK(PGW_Service_initialize(&service) == PGW_OK);
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
    CHECK(PGW_CAN_poll(can, 4) == PGW_OK);
    CHECK(PGW_Service_step(&service) == PGW_OK);
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
        CHECK(PGW_Service_step(&service) == PGW_OK);
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
        CHECK(PGW_Service_step(&service) == PGW_OK);
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
            CHECK(PGW_Service_step(&service) == PGW_OK);
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
        CHECK(PGW_CAN_poll(can, 4) == PGW_OK);
        CHECK(PGW_Service_step(&service) == PGW_OK);
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
    CHECK(PGW_Service_stop(&service) == PGW_OK);
    CHECK(PGW_Service_finalize(&service) == PGW_OK);
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

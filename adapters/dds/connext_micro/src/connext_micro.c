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
#include "app_gen/app_gen_plugin.h"
#include "dds_c/dds_c_rh_plugin.h"
#include "dds_c/dds_c_wh_plugin.h"
#include "rh_sm/rh_sm_history.h"
#include "wh_sm/wh_sm_history.h"
#if defined(PGW_ENABLE_REMOTE_CONTROL)
#include "control_manifest.h"
#endif
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct PGW_DDSSample {
    const PGW_DDSBinding *binding;
    const void *data;
    PGW_DDSMetadata info;
} PGW_DDSSample;
typedef struct PGW_DDSEndpoint {
    const PGW_DDSEndpointConfig *config;
    struct PGW_DDSConnection *connection;
    DDS_DataReader *reader;
    DDS_DataWriter *writer;
    void *typed;
    void *scratch;
    PGW_DDSSample *samples;
    PGW_Representation representation;
    const PGW_Representation *source;
    bool loaned;
    PGW_SampleSeq *loan;
    PGW_DDSStatistics statistics;
    bool input_enabled;
    bool output_enabled;
} PGW_DDSEndpoint;
#define REDA_SEQUENCE_USER_API
#define T PGW_DDSEndpoint
#define TSeq PGW_DDSEndpointSeq
#include "reda/reda_sequence_decl.h"
#undef T
#undef TSeq
typedef struct PGW_DDSEndpointSeq PGW_DDSEndpointSeq;

typedef struct PGW_DDSConnection {
    DDS_DomainParticipant *participant;
    PGW_DDSEndpointSeq endpoints;
    size_t storage_bytes;
    bool endpoints_initialized;
    bool endpoints_borrowed;
    bool control_enabled;
} PGW_DDSConnection;
#if defined(PGW_ENABLE_REMOTE_CONTROL)
static const PGW_ControlAdapterI dds_control;
#endif

#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"

#define REDA_SEQUENCE_USER_API
#define T PGW_DDSEndpointConfigElement
#define TSeq PGW_DDSEndpointConfigSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T PGW_DDSEndpoint
#define TSeq PGW_DDSEndpointSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
static const struct APPGEN_LibraryModelSeq *registered_model;
static struct APPGEN_FactoryProperty model_property = APPGEN_FactoryProperty_INITIALIZER;

PGW_Status PGW_DDS_register_model(const struct APPGEN_LibraryModelSeq *model)
{
    DDS_DomainParticipantFactory *factory;
    const struct APPGEN_LibraryModel *library;
    if (!model) return PGW_INVALID;
    if (registered_model) return registered_model == model ? PGW_OK : PGW_INVALID;
    factory = DDS_DomainParticipantFactory_get_instance();
    if (!factory) return PGW_FATAL;
    if (APPGEN_LibraryModelSeq_get_length(model) < 1) return PGW_INVALID;
    library = APPGEN_LibraryModelSeq_get_reference(model, 0);
    if (!library || !library->participant_count) return PGW_INVALID;
    for (DDS_Long i = 0; i < APPGEN_LibraryModelSeq_get_length(model); ++i) {
        const struct APPGEN_LibraryModel *other = APPGEN_LibraryModelSeq_get_reference(model, i);
        for (DDS_UnsignedLong j = 0; j < other->participant_count; ++j) {
            const struct DDS_DomainParticipantFactoryQos *first =
                &library->participants[0].domain_participant_factory.factory_qos;
            const struct DDS_DomainParticipantFactoryQos *candidate =
                &other->participants[j].domain_participant_factory.factory_qos;
            if (first->resource_limits.max_participants != candidate->resource_limits.max_participants ||
                first->resource_limits.max_components != candidate->resource_limits.max_components ||
                first->entity_factory.autoenable_created_entities !=
                    candidate->entity_factory.autoenable_created_entities)
                return PGW_UNSUPPORTED;
        }
    }
    /* Provision the process-global factory from MAG, before its first pool use. */
    if (DDS_DomainParticipantFactory_set_qos(factory,
            &library->participants[0].domain_participant_factory.factory_qos) !=
            DDS_RETCODE_OK) return PGW_FATAL;
    RT_Registry_T *registry = DDS_DomainParticipantFactory_get_registry(factory);
    if (!RT_Registry_lookup(registry, DDSHST_WRITER_DEFAULT_HISTORY_NAME) &&
        !RT_Registry_register(registry, DDSHST_WRITER_DEFAULT_HISTORY_NAME,
            WHSM_HistoryFactory_get_interface(), NULL, NULL)) return PGW_FATAL;
    if (!RT_Registry_lookup(registry, DDSHST_READER_DEFAULT_HISTORY_NAME) &&
        !RT_Registry_register(registry, DDSHST_READER_DEFAULT_HISTORY_NAME,
            RHSM_HistoryFactory_get_interface(), NULL, NULL)) return PGW_FATAL;
    model_property._model = model;
    if (!APPGEN_Factory_register(DDS_DomainParticipantFactory_get_registry(factory),
                                &model_property)) return PGW_FATAL;
    registered_model = model;
    return PGW_OK;
}
#if defined(PGW_ENABLE_REMOTE_CONTROL)
static const struct APPGEN_DomainParticipantModel *find_participant_model(const char *name)
{
    for (DDS_Long i = 0; i < APPGEN_LibraryModelSeq_get_length(registered_model); ++i) {
        const struct APPGEN_LibraryModel *library =
            APPGEN_LibraryModelSeq_get_reference(registered_model, i);
        for (DDS_UnsignedLong j = 0; j < library->participant_count; ++j) {
            const struct APPGEN_DomainParticipantModel *participant =
                &library->participants[j];
            if (!strcmp(participant->name, name)) return participant;
        }
    }
    return NULL;
}

PGW_Status PGW_DDS_remote_control_options_validate(
    const PGW_DDSRemoteControlOptions *options, bool telemetry_selected,
    uint32_t minimum_telemetry_period_ms)
{
    if (!options || options->version != PGW_DDS_REMOTE_CONTROL_OPTIONS_VERSION ||
        options->size != sizeof(*options)) return PGW_INVALID;
    if (options->minimum_telemetry_period_ms != minimum_telemetry_period_ms ||
        (telemetry_selected != (minimum_telemetry_period_ms != 0)))
        return PGW_INVALID;
    if (options->enabled &&
        (options->domain_id < 0 || options->domain_id > 232)) return PGW_INVALID;
    if (options->enabled &&
        (!options->max_controller_peers || options->max_controller_peers > 32))
        return PGW_INVALID;
    if (!options->enabled && options->telemetry_period_ms) return PGW_INVALID;
    if (!telemetry_selected && options->telemetry_period_ms) return PGW_INVALID;
    if (options->telemetry_period_ms &&
        (!minimum_telemetry_period_ms ||
         options->telemetry_period_ms < minimum_telemetry_period_ms))
        return PGW_INVALID;
    return PGW_OK;
}

static PGW_Status control_take_command(void *opaque, PGW_ControlCommand *command,
                                       PGW_ControlCorrelation *correlation)
{
    PGW_DDSControlTransport *transport = opaque;
    return transport->types->take_command(transport->command_reader, command, correlation);
}

static PGW_Status control_write_state(void *opaque, const PGW_ControlState *state)
{
    PGW_DDSControlTransport *transport = opaque;
    if (state->resource_id >= transport->state_handle_count) return PGW_INVALID;
    PGW_Status status = transport->types->write_state(transport->state_writer, state,
        &transport->state_handles[state->resource_id]);
    return status;
}

static PGW_Status control_write_result(void *opaque, const PGW_ControlResult *result)
{
    PGW_DDSControlTransport *transport = opaque;
    return transport->types->write_result(transport->result_writer, result);
}

static PGW_Status control_write_telemetry(void *opaque,
                                          const PGW_ControlTelemetry *sample)
{
    PGW_DDSControlTransport *transport = opaque;
    if (!transport->telemetry_writer || !sample ||
        sample->metric_id >= transport->telemetry_handle_count)
        return PGW_INVALID;
    return transport->types->write_telemetry(transport->telemetry_writer, sample,
        &transport->telemetry_handles[sample->metric_id]);
}

static const PGW_ControlEndpointI control_endpoint_iface = {
    PGW_CONTROL_ABI_VERSION,
    sizeof(PGW_ControlEndpointI),
    control_take_command,
    control_write_state,
    control_write_result,
    control_write_telemetry
};

static PGW_Status control_validate_reader(DDS_DataReader *reader)
{
    struct DDS_DataReaderQos qos = DDS_DataReaderQos_INITIALIZER;
    if (DDS_DataReader_get_qos(reader, &qos) != DDS_RETCODE_OK) return PGW_IO_ERROR;
    bool valid = qos.history.kind == DDS_KEEP_LAST_HISTORY_QOS &&
        qos.history.depth == 4 &&
        qos.resource_limits.max_instances == 1 &&
        qos.resource_limits.max_samples == 4 &&
        qos.resource_limits.max_samples_per_instance == 4 &&
        qos.reliability.kind == DDS_RELIABLE_RELIABILITY_QOS &&
        qos.durability.kind == DDS_VOLATILE_DURABILITY_QOS;
    DDS_DataReaderQos_finalize(&qos);
    return valid ? PGW_OK : PGW_INVALID;
}

static PGW_Status control_validate_writer(DDS_DataWriter *writer, size_t instances,
                                          DDS_Long depth, DDS_DurabilityQosPolicyKind durability,
                                          DDS_Long samples_per_instance)
{
    struct DDS_DataWriterQos qos = DDS_DataWriterQos_INITIALIZER;
    if (DDS_DataWriter_get_qos(writer, &qos) != DDS_RETCODE_OK) return PGW_IO_ERROR;
    bool valid = qos.history.kind == DDS_KEEP_LAST_HISTORY_QOS &&
        qos.history.depth == depth &&
        qos.resource_limits.max_instances == (DDS_Long)instances &&
        qos.resource_limits.max_samples == (DDS_Long)(instances * (size_t)samples_per_instance) &&
        qos.resource_limits.max_samples_per_instance == samples_per_instance &&
        qos.reliability.kind == DDS_RELIABLE_RELIABILITY_QOS &&
        qos.reliability.max_blocking_time.sec == 0 &&
        qos.reliability.max_blocking_time.nanosec == 0 &&
        qos.durability.kind == durability;
    DDS_DataWriterQos_finalize(&qos);
    return valid ? PGW_OK : PGW_INVALID;
}

static PGW_Status control_validate_telemetry_writer(DDS_DataWriter *writer,
                                                     size_t metrics)
{
    struct DDS_DataWriterQos qos = DDS_DataWriterQos_INITIALIZER;
    if (DDS_DataWriter_get_qos(writer, &qos) != DDS_RETCODE_OK) return PGW_IO_ERROR;
    bool valid = qos.history.kind == DDS_KEEP_LAST_HISTORY_QOS &&
        qos.history.depth == 1 &&
        qos.resource_limits.max_instances == (DDS_Long)metrics &&
        qos.resource_limits.max_samples == (DDS_Long)metrics &&
        qos.resource_limits.max_samples_per_instance == 1 &&
        qos.reliability.kind == DDS_BEST_EFFORT_RELIABILITY_QOS &&
        qos.durability.kind == DDS_VOLATILE_DURABILITY_QOS;
    DDS_DataWriterQos_finalize(&qos);
    return valid ? PGW_OK : PGW_INVALID;
}

PGW_Status PGW_DDS_control_transport_initialize(
    PGW_DDSControlTransport *transport, const PGW_DDSStaticEndpoint *endpoints,
    size_t endpoint_count, bool telemetry_selected, const PGW_DDSControlTypeI *types,
    size_t resource_count, DDS_InstanceHandle_t *state_handles,
    size_t state_handle_capacity, const PGW_ControlTelemetryMetric *metrics,
    size_t metric_count, DDS_InstanceHandle_t *telemetry_handles,
    size_t telemetry_handle_capacity)
{
    if (!transport || !endpoints || endpoint_count != 3 + (telemetry_selected ? 1 : 0) ||
        telemetry_selected != (metric_count > 0) ||
        (telemetry_selected && (!metrics || !telemetry_handles ||
                                telemetry_handle_capacity < metric_count)) ||
        !types || types->version != PGW_CONTROL_ABI_VERSION ||
        types->size != sizeof(PGW_DDSControlTypeI) || !types->take_command ||
        !types->register_state || !types->write_state || !types->write_result ||
        (telemetry_selected && (!types->register_telemetry || !types->write_telemetry)) ||
        !resource_count || !state_handles || state_handle_capacity < resource_count)
        return PGW_INVALID;
    PGW_DDSControlTransport configured = {0};
    for (size_t i = 0; i < endpoint_count; ++i) {
        if (!endpoints[i].name) return PGW_INVALID;
        if (!strcmp(endpoints[i].name, "ControlCommand") && endpoints[i].reader &&
            endpoints[i].datareader && !configured.command_reader)
            configured.command_reader = endpoints[i].datareader;
        else if (!strcmp(endpoints[i].name, "ControlState") && !endpoints[i].reader &&
                 endpoints[i].datawriter && !configured.state_writer)
            configured.state_writer = endpoints[i].datawriter;
        else if (!strcmp(endpoints[i].name, "ControlResult") && !endpoints[i].reader &&
                 endpoints[i].datawriter && !configured.result_writer)
            configured.result_writer = endpoints[i].datawriter;
        else if (!strcmp(endpoints[i].name, "ControlTelemetry") && !endpoints[i].reader &&
                 endpoints[i].datawriter && !configured.telemetry_writer)
            configured.telemetry_writer = endpoints[i].datawriter;
        else return PGW_INVALID;
    }
    if (!configured.command_reader || !configured.state_writer || !configured.result_writer)
        return PGW_INVALID;
    PGW_Status status = control_validate_reader(configured.command_reader);
    if (status != PGW_OK) return status;
    status = control_validate_writer(configured.state_writer, resource_count, 1,
        DDS_TRANSIENT_LOCAL_DURABILITY_QOS, 1);
    if (status != PGW_OK) return status;
    status = control_validate_writer(configured.result_writer, 1, 4,
        DDS_VOLATILE_DURABILITY_QOS, 4);
    if (status != PGW_OK) return status;
    if (telemetry_selected) {
        if (!configured.telemetry_writer) return PGW_INVALID;
        status = control_validate_telemetry_writer(configured.telemetry_writer, metric_count);
        if (status != PGW_OK) return status;
    } else if (configured.telemetry_writer) return PGW_INVALID;
    for (size_t i = 0; i < resource_count; ++i) {
        status = types->register_state(configured.state_writer, (uint32_t)i,
                                       &state_handles[i]);
        if (status != PGW_OK) return status;
        if (DDS_InstanceHandle_equals(&state_handles[i], &DDS_HANDLE_NIL))
            return PGW_FATAL;
    }
    if (telemetry_selected)
        for (size_t i = 0; i < metric_count; ++i) {
            status = types->register_telemetry(configured.telemetry_writer,
                &metrics[i], &telemetry_handles[i]);
            if (status != PGW_OK) return status;
            if (DDS_InstanceHandle_equals(&telemetry_handles[i], &DDS_HANDLE_NIL))
                return PGW_FATAL;
        }
    configured.types = types;
    configured.state_handles = state_handles;
    configured.state_handle_count = resource_count;
    configured.telemetry_handles = telemetry_handles;
    configured.telemetry_handle_count = telemetry_selected ? metric_count : 0;
    configured.initialized = true;
    *transport = configured;
    return PGW_OK;
}

PGW_ControlEndpoint PGW_DDS_control_endpoint(PGW_DDSControlTransport *transport)
{
    if (!transport || !transport->initialized) return (PGW_ControlEndpoint){0};
    return (PGW_ControlEndpoint){transport, &control_endpoint_iface};
}

PGW_Status PGW_DDS_control_transport_finalize(PGW_DDSControlTransport *transport)
{
    if (!transport || !transport->initialized) return PGW_INVALID;
    *transport = (PGW_DDSControlTransport){0};
    return PGW_OK;
}
static bool writer_qos_matches(DDS_DataWriter *writer,
                               const struct DDS_DataWriterQos *expected)
{
    struct DDS_DataWriterQos actual = DDS_DataWriterQos_INITIALIZER;
    if (DDS_DataWriter_get_qos(writer, &actual) != DDS_RETCODE_OK) return false;
    bool matches = actual.history.kind == expected->history.kind &&
        actual.history.depth == expected->history.depth &&
        actual.resource_limits.max_instances == expected->resource_limits.max_instances &&
        actual.resource_limits.max_samples == expected->resource_limits.max_samples &&
        actual.resource_limits.max_samples_per_instance ==
            expected->resource_limits.max_samples_per_instance &&
        actual.reliability.kind == expected->reliability.kind &&
        actual.reliability.max_blocking_time.sec == expected->reliability.max_blocking_time.sec &&
        actual.reliability.max_blocking_time.nanosec ==
            expected->reliability.max_blocking_time.nanosec;
    DDS_DataWriterQos_finalize(&actual);
    return matches;
}

static bool reader_qos_matches(DDS_DataReader *reader,
                               const struct DDS_DataReaderQos *expected)
{
    struct DDS_DataReaderQos actual = DDS_DataReaderQos_INITIALIZER;
    if (DDS_DataReader_get_qos(reader, &actual) != DDS_RETCODE_OK) return false;
    bool matches = actual.history.kind == expected->history.kind &&
        actual.history.depth == expected->history.depth &&
        actual.resource_limits.max_instances == expected->resource_limits.max_instances &&
        actual.resource_limits.max_samples == expected->resource_limits.max_samples &&
        actual.resource_limits.max_samples_per_instance ==
            expected->resource_limits.max_samples_per_instance &&
        actual.reliability.kind == expected->reliability.kind;
    DDS_DataReaderQos_finalize(&actual);
    return matches;
}

static PGW_Status create_static_entities(DDS_DomainParticipant *participant,
                                         const struct APPGEN_DomainParticipantModel *model,
                                         PGW_DDSStaticEndpoint *endpoints,
                                         size_t capacity, size_t *endpoint_count)
{
    for (DDS_UnsignedLong i = 0; i < model->type_registration_count; ++i) {
        const struct APPGEN_TypeRegistrationModel *type = &model->type_registrations[i];
        struct DDS_TypePluginI *plugin = type->get_type_plugin();
        if (!plugin || DDS_DomainParticipant_register_type(
                participant, type->type_name, plugin) != DDS_RETCODE_OK) {
            fprintf(stderr, "DDS dynamic type registration failed: %s\n", type->type_name);
            return PGW_FATAL;
        }
    }
    for (DDS_UnsignedLong i = 0; i < model->topic_count; ++i) {
        const struct APPGEN_TopicModel *topic = &model->topics[i];
        if (!DDS_DomainParticipant_create_topic(participant, topic->name,
                topic->type_name, &topic->topic_qos, NULL, DDS_STATUS_MASK_NONE)) {
            fprintf(stderr, "DDS dynamic topic creation failed: %s\n", topic->name);
            return PGW_FATAL;
        }
    }
    for (DDS_UnsignedLong i = 0; i < model->publisher_count; ++i) {
        const struct APPGEN_PublisherModel *publisher_model = &model->publishers[i];
        for (DDS_UnsignedLong instance = 0; instance < publisher_model->multiplicity; ++instance) {
            DDS_Publisher *publisher = DDS_DomainParticipant_create_publisher(
                participant, &publisher_model->publisher_qos, NULL, DDS_STATUS_MASK_NONE);
            if (!publisher) {
                fprintf(stderr, "DDS dynamic publisher creation failed: %s\n",
                        publisher_model->name);
                return PGW_FATAL;
            }
            for (DDS_UnsignedLong j = 0; j < publisher_model->writer_count; ++j) {
                const struct APPGEN_DataWriterModel *writer_model =
                    &publisher_model->data_writers[j];
                DDS_Topic *topic = DDS_Topic_narrow(
                    DDS_DomainParticipant_lookup_topicdescription(
                        participant, writer_model->topic_name));
                if (!topic) return PGW_INVALID;
                for (DDS_UnsignedLong writer = 0; writer < writer_model->multiplicity; ++writer) {
                    struct DDS_DataWriterQos writer_qos = DDS_DataWriterQos_INITIALIZER;
                    if (DDS_DataWriterQos_copy(&writer_qos, &writer_model->writer_qos) !=
                            DDS_RETCODE_OK ||
                        !DDS_EntityNameQosPolicy_set_name(&writer_qos.publication_name,
                                                        writer_model->name)) {
                        DDS_DataWriterQos_finalize(&writer_qos);
                        return PGW_INVALID;
                    }
                    DDS_DataWriter *created = DDS_Publisher_create_datawriter(publisher, topic,
                        &writer_qos, NULL, DDS_STATUS_MASK_NONE);
                    DDS_DataWriterQos_finalize(&writer_qos);
                    if (!created || *endpoint_count == capacity) {
                        fprintf(stderr, "DDS dynamic writer creation failed: %s\n",
                                writer_model->name);
                        return PGW_FATAL;
                    }
                    if (!writer_qos_matches(created, &writer_model->writer_qos))
                        return PGW_INVALID;
                    endpoints[*endpoint_count] = (PGW_DDSStaticEndpoint){
                        writer_model->name, false, NULL, created};
                    ++*endpoint_count;
                }
            }
        }
    }
    for (DDS_UnsignedLong i = 0; i < model->subscriber_count; ++i) {
        const struct APPGEN_SubscriberModel *subscriber_model = &model->subscribers[i];
        for (DDS_UnsignedLong instance = 0; instance < subscriber_model->multiplicity; ++instance) {
            DDS_Subscriber *subscriber = DDS_DomainParticipant_create_subscriber(
                participant, &subscriber_model->subscriber_qos, NULL, DDS_STATUS_MASK_NONE);
            if (!subscriber) {
                fprintf(stderr, "DDS dynamic subscriber creation failed: %s\n",
                        subscriber_model->name);
                return PGW_FATAL;
            }
            for (DDS_UnsignedLong j = 0; j < subscriber_model->reader_count; ++j) {
                const struct APPGEN_DataReaderModel *reader_model =
                    &subscriber_model->data_readers[j];
                DDS_TopicDescription *topic = DDS_DomainParticipant_lookup_topicdescription(
                    participant, reader_model->topic_name);
                if (!topic) return PGW_INVALID;
                for (DDS_UnsignedLong reader = 0; reader < reader_model->multiplicity; ++reader) {
                    struct DDS_DataReaderQos reader_qos = DDS_DataReaderQos_INITIALIZER;
                    if (DDS_DataReaderQos_copy(&reader_qos, &reader_model->reader_qos) !=
                            DDS_RETCODE_OK ||
                        !DDS_EntityNameQosPolicy_set_name(&reader_qos.subscription_name,
                                                        reader_model->name)) {
                        DDS_DataReaderQos_finalize(&reader_qos);
                        return PGW_INVALID;
                    }
                    DDS_DataReader *created = DDS_Subscriber_create_datareader(subscriber, topic,
                        &reader_qos, NULL, DDS_STATUS_MASK_NONE);
                    DDS_DataReaderQos_finalize(&reader_qos);
                    if (!created || *endpoint_count == capacity) {
                        fprintf(stderr, "DDS dynamic reader creation failed: %s\n",
                                reader_model->name);
                        return PGW_FATAL;
                    }
                    if (!reader_qos_matches(created, &reader_model->reader_qos))
                        return PGW_INVALID;
                    endpoints[*endpoint_count] = (PGW_DDSStaticEndpoint){
                        reader_model->name, true, created, NULL};
                    ++*endpoint_count;
                }
            }
        }
    }
    return PGW_OK;
}

static bool static_endpoint_count(const struct APPGEN_DomainParticipantModel *model,
                                  size_t *out)
{
    size_t count = 0;
    for (DDS_UnsignedLong i = 0; i < model->publisher_count; ++i)
        for (DDS_UnsignedLong j = 0; j < model->publishers[i].writer_count; ++j)
            if (!PGW_size_add(count, model->publishers[i].data_writers[j].multiplicity,
                              &count)) return false;
    for (DDS_UnsignedLong i = 0; i < model->subscriber_count; ++i)
        for (DDS_UnsignedLong j = 0; j < model->subscribers[i].reader_count; ++j)
            if (!PGW_size_add(count, model->subscribers[i].data_readers[j].multiplicity,
                              &count)) return false;
    *out = count;
    return true;
}

static PGW_Status register_participant_components(
    const struct APPGEN_DomainParticipantModel *model)
{
    RT_Registry_T *registry = DDS_DomainParticipantFactory_get_registry(
        DDS_DomainParticipantFactory_get_instance());
    if (!registry || model->custom_flow_controller_count ||
        model->content_filter_registration_count)
        return PGW_UNSUPPORTED;
    for (DDS_UnsignedLong i = 0;
         i < model->domain_participant_factory.register_count; ++i) {
        const struct ComponentFactoryRegisterModel *component =
            &model->domain_participant_factory.register_components[i];
        if (!component->register_name || !component->register_intf)
            return PGW_INVALID;
        if (RT_Registry_lookup(registry, component->register_name)) continue;
        if (!RT_Registry_register(registry, component->register_name,
                component->register_intf(),
                (struct RT_ComponentFactoryProperty *)component->register_property,
                (struct RT_ComponentFactoryListener *)component->register_listener)) {
            fprintf(stderr, "DDS component registration failed: %s\n",
                    component->register_name);
            return PGW_FATAL;
        }
    }
    return PGW_OK;
}

static PGW_Status create_appgen_participant_at_domain(const char *name,
    DDS_DomainId_t domain_id, const struct DDS_DomainParticipantQos *qos,
    PGW_DDSStaticEndpoint *endpoints, size_t endpoint_capacity,
    size_t *endpoint_count, DDS_DomainParticipant **out)
{
    if (!registered_model || !name || !*name || !endpoints || !endpoint_count || !out)
        return PGW_INVALID;
    *out = NULL;
    *endpoint_count = 0;
    const struct APPGEN_DomainParticipantModel *model = find_participant_model(name);
    if (!model) return PGW_INVALID;
    PGW_Status status = register_participant_components(model);
    if (status != PGW_OK) {
        fprintf(stderr, "DDS participant factory setup failed for %s: %s\n",
                name, PGW_status_name(status));
        return status;
    }
    size_t required;
    if (!static_endpoint_count(model, &required) || endpoint_capacity < required)
        return PGW_CAPACITY;
    DDS_DomainParticipantFactory *factory = DDS_DomainParticipantFactory_get_instance();
    if (!factory) return PGW_FATAL;
    DDS_DomainParticipant *participant = DDS_DomainParticipantFactory_create_participant(
        factory, domain_id, qos ? qos : &model->participant_qos,
        NULL, DDS_STATUS_MASK_NONE);
    if (!participant) {
        fprintf(stderr, "DDS participant creation failed for %s at domain %d\n",
                name, (int)domain_id);
        return PGW_FATAL;
    }
    status = create_static_entities(participant, model, endpoints,
                                    endpoint_capacity, endpoint_count);
    if (status != PGW_OK) {
        fprintf(stderr, "DDS static entities failed for %s: %s\n",
                name, PGW_status_name(status));
        if (DDS_DomainParticipant_delete_contained_entities(participant) != DDS_RETCODE_OK ||
            DDS_DomainParticipantFactory_delete_participant(factory, participant) != DDS_RETCODE_OK)
            return PGW_FATAL;
        *endpoint_count = 0;
        return status;
    }
    *out = participant;
    return PGW_OK;
}

PGW_Status PGW_DDS_create_participant_at_domain(const char *name, DDS_DomainId_t domain_id,
    PGW_DDSStaticEndpoint *endpoints, size_t endpoint_capacity, size_t *endpoint_count,
    DDS_DomainParticipant **out)
{
    return create_appgen_participant_at_domain(name, domain_id, NULL, endpoints,
        endpoint_capacity, endpoint_count, out);
}

static bool participant_has_entity(const struct APPGEN_DomainParticipantModel *model,
                                   const char *name, bool writer)
{
    if (writer) {
        for (DDS_UnsignedLong i = 0; i < model->publisher_count; ++i)
            for (DDS_UnsignedLong j = 0; j < model->publishers[i].writer_count; ++j)
                if (!strcmp(model->publishers[i].data_writers[j].name, name)) return true;
    } else {
        for (DDS_UnsignedLong i = 0; i < model->subscriber_count; ++i)
            for (DDS_UnsignedLong j = 0; j < model->subscribers[i].reader_count; ++j)
                if (!strcmp(model->subscribers[i].data_readers[j].name, name)) return true;
    }
    return false;
}

static PGW_Status set_control_participant_limits(
    const struct APPGEN_DomainParticipantModel *model, uint32_t peers,
    bool controller_side, bool telemetry, struct DDS_DomainParticipantQos *qos)
{
    if (!peers || peers > 32 || !qos) return PGW_INVALID;
    size_t local_writers = 0, local_readers = 0;
    size_t local_publishers = 0, local_subscribers = 0;
    for (DDS_UnsignedLong i = 0; i < model->publisher_count; ++i) {
        if (!PGW_size_add(local_publishers, model->publishers[i].multiplicity,
                          &local_publishers)) return PGW_CAPACITY;
        for (DDS_UnsignedLong j = 0; j < model->publishers[i].writer_count; ++j)
            if (!PGW_size_add(local_writers,
                    model->publishers[i].data_writers[j].multiplicity, &local_writers))
                return PGW_CAPACITY;
    }
    for (DDS_UnsignedLong i = 0; i < model->subscriber_count; ++i) {
        if (!PGW_size_add(local_subscribers, model->subscribers[i].multiplicity,
                          &local_subscribers)) return PGW_CAPACITY;
        for (DDS_UnsignedLong j = 0; j < model->subscribers[i].reader_count; ++j)
            if (!PGW_size_add(local_readers,
                    model->subscribers[i].data_readers[j].multiplicity, &local_readers))
                return PGW_CAPACITY;
    }
    size_t control_pair_count = 2 + (telemetry ? 1u : 0u);
    size_t remote_readers_per_peer = controller_side ? 1u : control_pair_count;
    size_t remote_writers_per_peer = controller_side ? control_pair_count : 1u;
    size_t remote_readers, remote_writers, match_writer_reader, match_reader_writer;
    if (!PGW_size_multiply(peers, remote_readers_per_peer, &remote_readers) ||
        !PGW_size_multiply(peers, remote_writers_per_peer, &remote_writers) ||
        !PGW_size_multiply(local_writers < remote_readers_per_peer ?
                              local_writers : remote_readers_per_peer,
                           peers, &match_writer_reader) ||
        !PGW_size_multiply(local_readers < remote_writers_per_peer ?
                              local_readers : remote_writers_per_peer,
                           peers, &match_reader_writer) ||
        local_writers > INT_MAX || local_readers > INT_MAX ||
        local_publishers > INT_MAX || local_subscribers > INT_MAX ||
        model->topic_count > INT_MAX || model->type_registration_count > INT_MAX ||
        remote_readers > INT_MAX || remote_writers > INT_MAX ||
        match_writer_reader > INT_MAX || match_reader_writer > INT_MAX)
        return PGW_CAPACITY;
    struct DDS_DomainParticipantQos configured = DDS_DomainParticipantQos_INITIALIZER;
    if (DDS_DomainParticipantQos_copy(&configured, &model->participant_qos) !=
        DDS_RETCODE_OK) return PGW_FATAL;
    configured.resource_limits.local_writer_allocation = (DDS_Long)local_writers;
    configured.resource_limits.local_reader_allocation = (DDS_Long)local_readers;
    configured.resource_limits.local_publisher_allocation = (DDS_Long)local_publishers;
    configured.resource_limits.local_subscriber_allocation = (DDS_Long)local_subscribers;
    configured.resource_limits.local_topic_allocation = (DDS_Long)model->topic_count;
    configured.resource_limits.local_type_allocation = (DDS_Long)model->type_registration_count;
    configured.resource_limits.remote_participant_allocation = (DDS_Long)peers;
    configured.resource_limits.remote_writer_allocation = (DDS_Long)remote_writers;
    configured.resource_limits.remote_reader_allocation = (DDS_Long)remote_readers;
    configured.resource_limits.matching_writer_reader_pair_allocation =
        (DDS_Long)match_writer_reader;
    configured.resource_limits.matching_reader_writer_pair_allocation =
        (DDS_Long)match_reader_writer;
    DDS_ReturnCode_t rc = DDS_DomainParticipantQos_copy(qos, &configured);
    DDS_DomainParticipantQos_finalize(&configured);
    return rc == DDS_RETCODE_OK ? PGW_OK : PGW_FATAL;
}

static PGW_Status create_bounded_control_participant(
    const PGW_DDSRemoteControlOptions *options, const char *participant_name,
    bool controller_side, PGW_DDSStaticEndpoint *endpoints,
    size_t endpoint_capacity, size_t *endpoint_count, DDS_DomainParticipant **out)
{
    if (!options || !options->enabled || !registered_model ||
        !endpoint_count || !out || !endpoints) return PGW_INVALID;
    const struct APPGEN_DomainParticipantModel *model =
        find_participant_model(participant_name);
    if (!model) return PGW_INVALID;
    bool telemetry_selected = participant_has_entity(model,
        "ControlTelemetry", !controller_side);
    PGW_Status status = PGW_DDS_remote_control_options_validate(options,
        telemetry_selected, options->minimum_telemetry_period_ms);
    if (status != PGW_OK) return status;
    struct DDS_DomainParticipantQos qos = DDS_DomainParticipantQos_INITIALIZER;
    status = set_control_participant_limits(model, options->max_controller_peers,
                                            controller_side, telemetry_selected, &qos);
    if (status != PGW_OK) return status;
    status = create_appgen_participant_at_domain(participant_name, options->domain_id,
        &qos, endpoints, endpoint_capacity, endpoint_count, out);
    if (DDS_DomainParticipantQos_finalize(&qos) != DDS_RETCODE_OK &&
        status == PGW_OK) {
        if (*out && PGW_DDS_delete_dynamic_participant(*out) != PGW_OK)
            return PGW_FATAL;
        *out = NULL;
        *endpoint_count = 0;
        return PGW_FATAL;
    }
    return status;
}

PGW_Status PGW_DDS_create_control_participant(
    const PGW_DDSRemoteControlOptions *options, PGW_DDSStaticEndpoint *endpoints,
    size_t endpoint_capacity, size_t *endpoint_count, DDS_DomainParticipant **out)
{
    return create_bounded_control_participant(options, "RemoteControl", false,
        endpoints, endpoint_capacity, endpoint_count, out);
}

PGW_Status PGW_DDS_create_controller_participant(
    const PGW_DDSRemoteControlOptions *options, PGW_DDSStaticEndpoint *endpoints,
    size_t endpoint_capacity, size_t *endpoint_count, DDS_DomainParticipant **out)
{
    return create_bounded_control_participant(options, "RemoteController", true,
        endpoints, endpoint_capacity, endpoint_count, out);
}

PGW_Status PGW_DDS_delete_dynamic_participant(DDS_DomainParticipant *participant)
{
    if (!participant) return PGW_INVALID;
    if (DDS_DomainParticipant_delete_contained_entities(participant) != DDS_RETCODE_OK)
        return PGW_FATAL;
    if (DDS_DomainParticipantFactory_delete_participant(
            DDS_DomainParticipantFactory_get_instance(), participant) != DDS_RETCODE_OK)
        return PGW_FATAL;
    return PGW_OK;
}
#endif
static PGW_Status copy_value(const PGW_Sample *opaque, void *out, size_t size)
{
    const PGW_DDSSample *sample = (const PGW_DDSSample *)opaque;
    if (!sample || !sample->info.valid_data) return PGW_INVALID;
    return sample->binding->copy_native(sample->data, out, size);
}
static PGW_Status timestamp(const PGW_Sample *opaque, PGW_Timestamp *out)
{
    const PGW_DDSSample *sample = (const PGW_DDSSample *)opaque;
    if (!sample || !out) return PGW_INVALID;
    out->seconds = sample->info.source_timestamp.sec;
    out->nanoseconds = sample->info.source_timestamp.nanosec;
    out->valid = sample->info.valid_data && out->seconds >= 0 &&
                 out->nanoseconds < 1000000000u;
    out->portable = out->valid;
    return PGW_OK;
}
static const PGW_SampleAccessI access_i = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), copy_value, timestamp
};
PGW_Status PGW_DDS_metadata(const PGW_Representation *representation,
                          const PGW_Sample *opaque, PGW_DDSMetadata *out)
{
    const PGW_DDSSample *sample = (const PGW_DDSSample *)opaque;
    if (!representation || representation->access != &access_i || !sample || !out)
        return PGW_INVALID;
    *out = sample->info;
    return PGW_OK;
}
static PGW_Status release_failed_read(PGW_DDSEndpoint *ep, PGW_SampleSeq *seq,
                                     PGW_Status status)
{
    if (ep->config->binding->return_loan(ep->typed, ep->reader) == DDS_RETCODE_OK)
        return status;
    ep->loaned = true;
    ep->loan = seq;
    ++ep->statistics.loan_errors;
    return PGW_LOAN_ERROR;
}
static PGW_Status read_samples(void *state, PGW_SampleSeq *seq, size_t budget)
{
    PGW_DDSEndpoint *ep = state;
    DDS_ReturnCode_t rc;
    size_t count, valid = 0, limit;
    if (!seq || ep->loaned || !budget || PGW_SampleSeq_get_length(seq)) {
        ++ep->statistics.loan_errors;
        return PGW_LOAN_ERROR;
    }
    if (!ep->connection->control_enabled || !ep->input_enabled)
        return PGW_NO_DATA;
    limit = budget < ep->config->capacity ? budget : ep->config->capacity;
    RTI_INT32 maximum = PGW_SampleSeq_get_maximum(seq);
    if (maximum < 0 || (size_t)maximum < limit) return PGW_CAPACITY;
    rc = ep->config->binding->take(ep->typed, ep->reader, limit);
    if (rc == DDS_RETCODE_NO_DATA) return PGW_NO_DATA;
    if (rc != DDS_RETCODE_OK) return PGW_IO_ERROR;
    count = ep->config->binding->length(ep->typed);
    if (count > limit) return release_failed_read(ep, seq, PGW_CAPACITY);
    for (size_t i = 0; i < count; ++i) {
        const struct DDS_SampleInfo *info = ep->config->binding->info(ep->typed, i);
        if (!info) return release_failed_read(ep, seq, PGW_FATAL);
        /* Lifecycle-only samples are not application commands. */
        if (!info->valid_data) {++ep->statistics.lifecycle_samples; continue;}
        ep->samples[valid].binding = ep->config->binding;
        ep->samples[valid].data = ep->config->binding->data(ep->typed, i);
        if (!ep->samples[valid].data) return release_failed_read(ep, seq, PGW_FATAL);
        ep->samples[valid].info = (PGW_DDSMetadata){info->source_timestamp,
            info->reception_timestamp, info->publication_sequence_number,
            info->publication_handle, info->instance_state, info->valid_data,
            info->sample_state, info->view_state, info->instance_handle};
        ++valid;
    }
    if (!valid) {
        return release_failed_read(ep, seq, PGW_NO_DATA);
    }
    if (!PGW_SampleSeq_set_length(seq, (RTI_INT32)valid)) {
        return release_failed_read(ep, seq, PGW_LOAN_ERROR);
    }
    for (size_t i = 0; i < valid; ++i)
        *PGW_SampleSeq_get_reference(seq, (RTI_INT32)i) = (const PGW_Sample *)&ep->samples[i];
    ep->loaned = true;
    ep->loan = seq;
    ep->statistics.valid_samples += valid;
    ++ep->statistics.loans;
    return PGW_OK;
}
static PGW_Status return_samples(void *state, PGW_SampleSeq *seq)
{
    PGW_DDSEndpoint *ep = state;
    DDS_ReturnCode_t rc;
    if (!ep->loaned || ep->loan != seq) {
        ++ep->statistics.loan_errors;
        return PGW_LOAN_ERROR;
    }
    rc = ep->config->binding->return_loan(ep->typed, ep->reader);
    if (rc != DDS_RETCODE_OK) {++ep->statistics.loan_errors; return PGW_LOAN_ERROR;}
    ep->loaned = false;
    ep->loan = NULL;
    if (!PGW_SampleSeq_set_length(seq, 0)) {
        ++ep->statistics.loan_errors;
        return PGW_LOAN_ERROR;
    }
    return PGW_OK;
}
static PGW_Status bind_writer(void *state, const PGW_Representation *source)
{
    PGW_DDSEndpoint *ep = state;
    if (!source || !PGW_schema_equal(source->schema, ep->representation.schema) ||
        !source->access || source->access->version != PGW_ABI_VERSION ||
        source->access->size != sizeof(PGW_SampleAccessI) ||
        !source->access->copy_value ||
        (ep->config->preserve_source_timestamp && !source->access->source_timestamp))
        return PGW_UNSUPPORTED;
    ep->source = source;
    return PGW_OK;
}
static PGW_Status write_samples(void *state, const PGW_SampleSeq *seq,
                               PGW_WriteResultSeq *results)
{
    PGW_DDSEndpoint *ep = state;
    if (!ep->source || !seq || !results) return PGW_INVALID;
    RTI_INT32 length = PGW_SampleSeq_get_length(seq);
    RTI_INT32 maximum = PGW_WriteResultSeq_get_maximum(results);
    if (length < 0 || maximum < length) return PGW_INVALID;
    size_t count = (size_t)length;
    if (count > ep->config->capacity) return PGW_CAPACITY;
    if (!PGW_WriteResultSeq_set_length(results, length)) return PGW_INVALID;
    if (!ep->connection->control_enabled || !ep->output_enabled) {
        for (size_t i = 0; i < count; ++i) {
            *PGW_WriteResultSeq_get_reference(results, (RTI_INT32)i) =
                PGW_WRITE_BACKPRESSURE;
            ++ep->statistics.backpressure;
        }
        return PGW_OK;
    }
    for (size_t i = 0; i < count; ++i) {
        PGW_SampleRef sample = *PGW_SampleSeq_get_reference(seq, (RTI_INT32)i);
        PGW_WriteResult *result = PGW_WriteResultSeq_get_reference(results, (RTI_INT32)i);
        struct DDS_Time_t time;
        const struct DDS_Time_t *time_ptr = NULL;
        DDS_ReturnCode_t rc;
        if (!result) return PGW_INVALID;
        *result = PGW_WRITE_INVALID;
        if (ep->source->access->copy_value(sample, ep->scratch,
                ep->config->binding->native_size) != PGW_OK) {
            ++ep->statistics.invalid;
            continue;
        }
        if (ep->config->preserve_source_timestamp) {
            PGW_Timestamp ts;
            if (ep->source->access->source_timestamp(sample, &ts) != PGW_OK ||
                !ts.valid || !ts.portable || ts.seconds < 0 ||
                ts.seconds > INT_MAX || ts.nanoseconds >= 1000000000u) {
                ++ep->statistics.invalid;
                continue;
            }
            time.sec = (DDS_Long)ts.seconds;
            time.nanosec = ts.nanoseconds;
            time_ptr = &time;
        }
        rc = ep->config->binding->write(ep->typed, ep->writer, ep->scratch, time_ptr);
        if (rc == DDS_RETCODE_OK) *result = PGW_WRITE_ACCEPTED;
        else if (rc == DDS_RETCODE_OUT_OF_RESOURCES || rc == DDS_RETCODE_TIMEOUT)
            *result = PGW_WRITE_BACKPRESSURE;
        else if (rc != DDS_RETCODE_BAD_PARAMETER &&
                 rc != DDS_RETCODE_PRECONDITION_NOT_MET)
            *result = PGW_WRITE_FATAL;
        if (*result == PGW_WRITE_ACCEPTED) ++ep->statistics.accepted;
        else if (*result == PGW_WRITE_BACKPRESSURE) ++ep->statistics.backpressure;
        else if (*result == PGW_WRITE_INVALID) ++ep->statistics.invalid;
        else ++ep->statistics.fatal;
    }
    return PGW_OK;
}
static const PGW_StreamReaderI reader_i = {
    PGW_ABI_VERSION, sizeof(PGW_StreamReaderI), read_samples, return_samples
};
static const PGW_StreamWriterI writer_i = {
    PGW_ABI_VERSION, sizeof(PGW_StreamWriterI), bind_writer, write_samples
};
static PGW_Status get_reader(PGW_Connection *opaque, const char *name,
                            PGW_StreamReader *out)
{
    PGW_DDSConnection *connection = (PGW_DDSConnection *)opaque;
    for (RTI_INT32 i = 0; i < PGW_DDSEndpointSeq_get_length(&connection->endpoints); ++i) {
        PGW_DDSEndpoint *ep = PGW_DDSEndpointSeq_get_reference(&connection->endpoints, i);
        if (ep->reader && !strcmp(ep->config->name, name)) {
            *out = (PGW_StreamReader){ep, &reader_i, &ep->representation};
            return PGW_OK;
        }
    }
    return PGW_INVALID;
}
static PGW_Status get_writer(PGW_Connection *opaque, const char *name,
                            PGW_StreamWriter *out)
{
    PGW_DDSConnection *connection = (PGW_DDSConnection *)opaque;
    for (RTI_INT32 i = 0; i < PGW_DDSEndpointSeq_get_length(&connection->endpoints); ++i) {
        PGW_DDSEndpoint *ep = PGW_DDSEndpointSeq_get_reference(&connection->endpoints, i);
        if (ep->writer && !strcmp(ep->config->name, name)) {
            *out = (PGW_StreamWriter){ep, &writer_i, &ep->representation};
            return PGW_OK;
        }
    }
    return PGW_INVALID;
}
#if defined(PGW_ENABLE_REMOTE_CONTROL)
PGW_Status PGW_DDS_control_target(PGW_Connection *opaque,
                                  PGW_ControlResourceKind kind,
                                  const char *name,
                                  const PGW_ControlAdapterI **iface,
                                  void **state)
{
    PGW_DDSConnection *connection = (PGW_DDSConnection *)opaque;
    if (!connection || !connection->participant || !name || !*name ||
        !iface || !state) return PGW_INVALID;
    if (kind == PGW_CONTROL_RESOURCE_CONNECTION) {
        *iface = &dds_control;
        *state = connection;
        return PGW_OK;
    }
    if (kind != PGW_CONTROL_RESOURCE_INPUT && kind != PGW_CONTROL_RESOURCE_OUTPUT)
        return PGW_UNSUPPORTED;
    for (RTI_INT32 i = 0; i < PGW_DDSEndpointSeq_get_length(&connection->endpoints); ++i) {
        PGW_DDSEndpoint *endpoint = PGW_DDSEndpointSeq_get_reference(
            &connection->endpoints, i);
        if (!strcmp(endpoint->config->name, name) &&
            ((kind == PGW_CONTROL_RESOURCE_INPUT && endpoint->reader) ||
             (kind == PGW_CONTROL_RESOURCE_OUTPUT && endpoint->writer))) {
            *iface = &dds_control;
            *state = endpoint;
            return PGW_OK;
        }
    }
    return PGW_INVALID;
}
#endif
static PGW_Status close_connection(PGW_Connection *opaque)
{
    PGW_DDSConnection *connection = (PGW_DDSConnection *)opaque;
    for (RTI_INT32 i = 0; i < PGW_DDSEndpointSeq_get_length(&connection->endpoints); ++i)
        if (PGW_DDSEndpointSeq_get_reference(&connection->endpoints, i)->loaned)
            return PGW_LOAN_ERROR;
    if (connection->participant) {
        if (DDS_DomainParticipant_delete_contained_entities(connection->participant) !=
            DDS_RETCODE_OK) return PGW_FATAL;
        if (DDS_DomainParticipantFactory_delete_participant(
                DDS_DomainParticipantFactory_get_instance(), connection->participant) !=
            DDS_RETCODE_OK) return PGW_FATAL;
        connection->participant = NULL;
    }
    if (connection->endpoints_initialized) {
        if (connection->endpoints_borrowed &&
            !PGW_DDSEndpointSeq_unloan(&connection->endpoints)) return PGW_LOAN_ERROR;
        if (!PGW_DDSEndpointSeq_finalize(&connection->endpoints)) return PGW_LOAN_ERROR;
        connection->endpoints_initialized = false;
        connection->endpoints_borrowed = false;
    }
    return PGW_OK;
}

#if defined(PGW_ENABLE_REMOTE_CONTROL)
static PGW_Status control_apply(void *state, PGW_ControlAction action)
{
    if (!state) return PGW_INVALID;
    if (action == PGW_CONTROL_CONNECTION_UP ||
        action == PGW_CONTROL_CONNECTION_DOWN) {
        PGW_DDSConnection *connection = state;
        if (!connection->participant) return PGW_INVALID;
        bool desired = action == PGW_CONTROL_CONNECTION_UP;
        if (connection->control_enabled == desired) return PGW_NO_CHANGE;
        connection->control_enabled = desired;
        return PGW_OK;
    }
    if (action == PGW_CONTROL_INPUT_ENABLE ||
        action == PGW_CONTROL_INPUT_DISABLE ||
        action == PGW_CONTROL_OUTPUT_ENABLE ||
        action == PGW_CONTROL_OUTPUT_DISABLE) {
        PGW_DDSEndpoint *endpoint = state;
        bool input = action == PGW_CONTROL_INPUT_ENABLE ||
                     action == PGW_CONTROL_INPUT_DISABLE;
        if ((input && !endpoint->reader) || (!input && !endpoint->writer))
            return PGW_INVALID;
        bool desired = action == PGW_CONTROL_INPUT_ENABLE ||
                       action == PGW_CONTROL_OUTPUT_ENABLE;
        bool *enabled = input ? &endpoint->input_enabled : &endpoint->output_enabled;
        if (*enabled == desired) return PGW_NO_CHANGE;
        *enabled = desired;
        return PGW_OK;
    }
    return PGW_UNSUPPORTED;
}

static const PGW_ControlAdapterI dds_control = {
    PGW_CONTROL_ABI_VERSION,
    sizeof(PGW_ControlAdapterI),
    PGW_CONNEXT_MICRO_CONTROL_MANIFEST_VERSION,
    PGW_CONNEXT_MICRO_CONTROL_RESOURCE_KIND_MASK,
    PGW_CONNEXT_MICRO_CONTROL_ACTION_MASK,
    control_apply,
    PGW_CONNEXT_MICRO_CONTROL_TELEMETRY_METRIC_MASK,
    NULL
};
#endif
static PGW_Status allocate(PGW_Arena *arena, size_t count, size_t size, void **out)
{
    size_t bytes;
    if (!PGW_size_multiply(count, size, &bytes)) return PGW_CAPACITY;
    return PGW_Arena_allocate(arena, bytes, _Alignof(max_align_t), out);
}
static PGW_Status create_connection(const void *configuration, PGW_Arena *arena,
                                   PGW_Connection **out)
{
    const PGW_DDSConfig *config = configuration;
    PGW_DDSConnection *connection;
    PGW_Status status;
    size_t initial_used;
    if (!registered_model || !config || !config->participant_name ||
        !config->endpoints_initialized || !arena || !out)
        return PGW_INVALID;
    RTI_INT32 endpoint_count = PGW_DDSEndpointConfigSeq_get_length(&config->endpoints);
    if (endpoint_count <= 0) return PGW_INVALID;
    for (RTI_INT32 i = 0; i < endpoint_count; ++i) {
        const PGW_DDSEndpointConfig *ec =
            PGW_DDSEndpointConfigSeq_get_reference(&config->endpoints, i);
        const PGW_DDSBinding *binding = ec->binding;
        if (!ec->name || !*ec->name || !ec->entity_name || !*ec->entity_name ||
            !binding || !binding->dds_type_name || !binding->representation ||
            !binding->representation->schema || !binding->representation->schema->name ||
            !binding->representation->schema->fingerprint ||
            !binding->initialize || !binding->copy_native || !binding->write ||
            !binding->take || !binding->length || !binding->data || !binding->info ||
            !binding->return_loan || !binding->native_size ||
            !ec->capacity || ec->capacity > INT_MAX ||
            (ec->reader && ec->preserve_source_timestamp)) return PGW_INVALID;
        for (RTI_INT32 j = 0; j < i; ++j) {
            const PGW_DDSEndpointConfig *prior =
                PGW_DDSEndpointConfigSeq_get_reference(&config->endpoints, j);
            if (!strcmp(ec->name, prior->name) ||
                !strcmp(ec->entity_name, prior->entity_name))
                return PGW_INVALID;
        }
    }
    initial_used = arena->used;
    status = allocate(arena, 1, sizeof(*connection), (void **)&connection);
    if (status != PGW_OK) return status;
    memset(connection, 0, sizeof(*connection));
    connection->control_enabled = true;
    PGW_DDSEndpoint *endpoint_storage;
    status = allocate(arena, (size_t)endpoint_count, sizeof(PGW_DDSEndpoint),
                      (void **)&endpoint_storage);
    if (status != PGW_OK) return status;
    memset(endpoint_storage, 0, (size_t)endpoint_count * sizeof(PGW_DDSEndpoint));
    if (!PGW_DDSEndpointSeq_initialize(&connection->endpoints)) return PGW_FATAL;
    connection->endpoints_initialized = true;
    if (!PGW_DDSEndpointSeq_loan_contiguous(&connection->endpoints,
            endpoint_storage, endpoint_count, endpoint_count)) {
        (void)PGW_DDSEndpointSeq_finalize(&connection->endpoints);
        connection->endpoints_initialized = false;
        return PGW_CAPACITY;
    }
    connection->endpoints_borrowed = true;
    if (!PGW_DDSEndpointSeq_set_length(&connection->endpoints, endpoint_count)) {
        (void)PGW_DDSEndpointSeq_unloan(&connection->endpoints);
        (void)PGW_DDSEndpointSeq_finalize(&connection->endpoints);
        connection->endpoints_initialized = false;
        connection->endpoints_borrowed = false;
        return PGW_CAPACITY;
    }
    connection->participant = DDS_DomainParticipantFactory_create_participant_from_config(
        DDS_DomainParticipantFactory_get_instance(), config->participant_name);
    if (!connection->participant) return PGW_FATAL;
    for (RTI_INT32 i = 0; i < endpoint_count; ++i) {
        PGW_DDSEndpoint *ep = PGW_DDSEndpointSeq_get_reference(&connection->endpoints, i);
        const PGW_DDSEndpointConfig *ec =
            PGW_DDSEndpointConfigSeq_get_reference(&config->endpoints, i);
        const PGW_DDSBinding *binding = ec->binding;
        ep->config = ec;
        ep->connection = connection;
        ep->input_enabled = true;
        ep->output_enabled = true;
        ep->representation = *binding->representation;
        ep->representation.access = &access_i;
        ep->representation.sample_size = sizeof(PGW_DDSSample);
        ep->representation.sample_alignment = _Alignof(PGW_DDSSample);
        status = binding->initialize(arena, ec->capacity, &ep->typed);
        if (status != PGW_OK) goto fail;
        if (ec->reader) {
            struct DDS_DataReaderQos qos = DDS_DataReaderQos_INITIALIZER;
            ep->reader = DDS_DomainParticipant_lookup_datareader_by_name(
                connection->participant, ec->entity_name);
            if (!ep->reader) {status = PGW_INVALID; goto fail;}
            if (strcmp(DDS_TopicDescription_get_type_name(
                    DDS_DataReader_get_topicdescription(ep->reader)),
                    binding->dds_type_name)) {status = PGW_INVALID; goto fail;}
            if (DDS_DataReader_get_qos(ep->reader, &qos) != DDS_RETCODE_OK)
                {status = PGW_FATAL; goto fail;}
            status = qos.history.kind == DDS_KEEP_LAST_HISTORY_QOS &&
                qos.history.depth == 1 && qos.resource_limits.max_instances > 0 &&
                qos.resource_limits.max_samples > 0 ? PGW_OK : PGW_INVALID;
            DDS_DataReaderQos_finalize(&qos);
            if (status != PGW_OK) goto fail;
            status = allocate(arena, ec->capacity, sizeof(PGW_DDSSample), (void **)&ep->samples);
            if (status != PGW_OK) goto fail;
        } else {
            struct DDS_DataWriterQos qos = DDS_DataWriterQos_INITIALIZER;
            ep->writer = DDS_DomainParticipant_lookup_datawriter_by_name(
                connection->participant, ec->entity_name);
            if (!ep->writer) {status = PGW_INVALID; goto fail;}
            if (strcmp(DDS_TopicDescription_get_type_name(
                    DDS_Topic_as_topicdescription(DDS_DataWriter_get_topic(ep->writer))),
                    binding->dds_type_name)) {status = PGW_INVALID; goto fail;}
            if (DDS_DataWriter_get_qos(ep->writer, &qos) != DDS_RETCODE_OK)
                {status = PGW_FATAL; goto fail;}
            if (qos.reliability.max_blocking_time.sec != 0 ||
                qos.reliability.max_blocking_time.nanosec != 0 ||
                qos.history.kind != DDS_KEEP_LAST_HISTORY_QOS ||
                qos.history.depth != 1 || qos.resource_limits.max_instances <= 0 ||
                qos.resource_limits.max_samples <= 0) status = PGW_INVALID;
            else status = PGW_OK;
            DDS_DataWriterQos_finalize(&qos);
            if (status != PGW_OK) goto fail;
            status = allocate(arena, 1, binding->native_size, &ep->scratch);
            if (status != PGW_OK) goto fail;
            if (binding->register_keys &&
                (status = binding->register_keys(ep->typed, ep->writer)) != PGW_OK)
                goto fail;
        }
    }
    connection->storage_bytes = arena->used - initial_used;
    *out = (PGW_Connection *)connection;
    return PGW_OK;
fail:
    if (close_connection((PGW_Connection *)connection) != PGW_OK) return PGW_FATAL;
    arena->used = initial_used;
    return status;
}
const PGW_ConnectionI PGW_DDSConnextMicroConnection = {
    PGW_ABI_VERSION, sizeof(PGW_ConnectionI), get_reader, get_writer, close_connection
};
const PGW_AdapterI PGW_DDSConnextMicroAdapter = {
    .version = PGW_ABI_VERSION,
    .size = sizeof(PGW_AdapterI),
    .name = "connext_micro",
    .create = create_connection,
    .connection = &PGW_DDSConnextMicroConnection,
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    .control = &dds_control
#endif
};
PGW_Status PGW_DDS_register_adapter(PGW_Registry *registry)
{
    return PGW_Registry_register_adapter(registry, &PGW_DDSConnextMicroAdapter);
}
PGW_Status PGW_DDS_create(const PGW_DDSConfig *config, PGW_Arena *arena,
                        PGW_Connection **out)
{
    return create_connection(config, arena, out);
}
DDS_DomainParticipant *PGW_DDS_participant(PGW_Connection *opaque)
{
    PGW_DDSConnection *connection = (PGW_DDSConnection *)opaque;
    return connection ? connection->participant : NULL;
}
PGW_Status PGW_DDS_effective_resources(PGW_Connection *opaque, PGW_DDSResources *out)
{
    PGW_DDSConnection *connection = (PGW_DDSConnection *)opaque;
    struct DDS_DomainParticipantQos qos = DDS_DomainParticipantQos_INITIALIZER;
    struct DDS_DomainParticipantFactoryQos factory_qos =
        DDS_DomainParticipantFactoryQos_INITIALIZER;
    if (!connection || !out || DDS_DomainParticipant_get_qos(connection->participant,
        &qos) != DDS_RETCODE_OK) return PGW_INVALID;
    out->local_readers = qos.resource_limits.local_reader_allocation;
    out->local_writers = qos.resource_limits.local_writer_allocation;
    out->local_topics = qos.resource_limits.local_topic_allocation;
    out->remote_participants = qos.resource_limits.remote_participant_allocation;
    out->remote_readers = qos.resource_limits.remote_reader_allocation;
    out->remote_writers = qos.resource_limits.remote_writer_allocation;
    if (DDS_DomainParticipantFactory_get_qos(DDS_DomainParticipantFactory_get_instance(),
            &factory_qos) != DDS_RETCODE_OK) {
        DDS_DomainParticipantQos_finalize(&qos);
        return PGW_IO_ERROR;
    }
    out->factory_participants = factory_qos.resource_limits.max_participants;
    out->factory_components = factory_qos.resource_limits.max_components;
    out->gateway_storage_bytes = connection->storage_bytes;
    DDS_DomainParticipantFactoryQos_finalize(&factory_qos);
    DDS_DomainParticipantQos_finalize(&qos);
    return PGW_OK;
}
PGW_Status PGW_DDS_statistics(PGW_Connection *opaque, const char *name,
                            PGW_DDSStatistics *out)
{
    PGW_DDSConnection *connection = (PGW_DDSConnection *)opaque;
    if (!connection || !name || !out) return PGW_INVALID;
    for (RTI_INT32 i = 0; i < PGW_DDSEndpointSeq_get_length(&connection->endpoints); ++i) {
        PGW_DDSEndpoint *ep = PGW_DDSEndpointSeq_get_reference(&connection->endpoints, i);
        if (strcmp(ep->config->name, name)) continue;
        *out = ep->statistics;
        if (ep->reader) {
            struct DDS_SampleLostStatus lost;
            struct DDS_SampleRejectedStatus rejected;
            struct DDS_SubscriptionMatchedStatus matched;
            struct DDS_RequestedIncompatibleQosStatus incompatible;
            if (DDS_DataReader_get_sample_lost_status(ep->reader, &lost) != DDS_RETCODE_OK ||
                DDS_DataReader_get_sample_rejected_status(ep->reader, &rejected) != DDS_RETCODE_OK ||
                DDS_DataReader_get_subscription_matched_status(ep->reader, &matched) != DDS_RETCODE_OK ||
                DDS_DataReader_get_requested_incompatible_qos_status(ep->reader, &incompatible) != DDS_RETCODE_OK)
                return PGW_IO_ERROR;
            out->lost = lost.total_count;
            out->rejected = rejected.total_count;
            out->matched = matched.current_count;
            out->incompatible_qos = incompatible.total_count;
        } else {
            struct DDS_PublicationMatchedStatus matched;
            struct DDS_OfferedIncompatibleQosStatus incompatible;
            if (DDS_DataWriter_get_publication_matched_status(ep->writer, &matched) != DDS_RETCODE_OK ||
                DDS_DataWriter_get_offered_incompatible_qos_status(ep->writer, &incompatible) != DDS_RETCODE_OK)
                return PGW_IO_ERROR;
            out->matched = matched.current_count;
            out->incompatible_qos = incompatible.total_count;
        }
        return PGW_OK;
    }
    return PGW_INVALID;
}
PGW_Status PGW_DDS_effective_history(PGW_Connection *opaque, const char *name,
                                   PGW_DDSHistoryResources *out)
{
    PGW_DDSConnection *connection = (PGW_DDSConnection *)opaque;
    if (!connection || !name || !out) return PGW_INVALID;
    for (RTI_INT32 i = 0; i < PGW_DDSEndpointSeq_get_length(&connection->endpoints); ++i) {
        PGW_DDSEndpoint *ep = PGW_DDSEndpointSeq_get_reference(&connection->endpoints, i);
        if (strcmp(ep->config->name, name)) continue;
        if (ep->writer) {
            struct DDS_DataWriterQos qos = DDS_DataWriterQos_INITIALIZER;
            if (DDS_DataWriter_get_qos(ep->writer, &qos) != DDS_RETCODE_OK)
                return PGW_IO_ERROR;
            *out = (PGW_DDSHistoryResources){qos.resource_limits.max_instances,
                qos.resource_limits.max_samples, qos.resource_limits.max_samples_per_instance,
                qos.history.depth, qos.reliability.max_blocking_time.sec,
                qos.reliability.max_blocking_time.nanosec};
            DDS_DataWriterQos_finalize(&qos);
        } else {
            struct DDS_DataReaderQos qos = DDS_DataReaderQos_INITIALIZER;
            if (DDS_DataReader_get_qos(ep->reader, &qos) != DDS_RETCODE_OK)
                return PGW_IO_ERROR;
            *out = (PGW_DDSHistoryResources){qos.resource_limits.max_instances,
                qos.resource_limits.max_samples, qos.resource_limits.max_samples_per_instance,
                qos.history.depth, 0, 0};
            DDS_DataReaderQos_finalize(&qos);
        }
        return PGW_OK;
    }
    return PGW_INVALID;
}

#include "pgw/dds_micro.h"
#include "app_gen/app_gen_plugin.h"
#include <limits.h>
#include <string.h>

typedef struct PGW_DDSSample {
    const PGW_DDSBinding *binding;
    const void *data;
    PGW_DDSMetadata info;
} PGW_DDSSample;
typedef struct PGW_DDSEndpoint {
    const PGW_DDSEndpointConfig *config;
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
} PGW_DDSConnection;

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
    model_property._model = model;
    if (!APPGEN_Factory_register(DDS_DomainParticipantFactory_get_registry(factory),
                                &model_property)) return PGW_FATAL;
    registered_model = model;
    return PGW_OK;
}
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
const PGW_ConnectionI PGW_DDSMicroConnection = {
    PGW_ABI_VERSION, sizeof(PGW_ConnectionI), get_reader, get_writer, close_connection
};
const PGW_AdapterI PGW_DDSMicroAdapter = {
    PGW_ABI_VERSION, sizeof(PGW_AdapterI), "dds_micro", create_connection, &PGW_DDSMicroConnection
};
PGW_Status PGW_DDS_register_adapter(PGW_Registry *registry)
{
    return PGW_Registry_register_adapter(registry, &PGW_DDSMicroAdapter);
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

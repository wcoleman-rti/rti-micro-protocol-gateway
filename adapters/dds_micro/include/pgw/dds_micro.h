#ifndef PGW_DDS_MICRO_H
#define PGW_DDS_MICRO_H
#include "pgw/core.h"
#include "rti_me_c.h"
#include "app_gen/app_gen.h"

/* Typed implementations own native DDS sequence descriptors and scratch storage. */
typedef struct {
    const PGW_Representation *representation;
    const char *dds_type_name;
    size_t native_size;
    PGW_Status (*initialize)(PGW_Arena *, size_t, void **);
    DDS_ReturnCode_t (*take)(void *, DDS_DataReader *, size_t);
    size_t (*length)(void *);
    const void *(*data)(void *, size_t);
    const struct DDS_SampleInfo *(*info)(void *, size_t);
    DDS_ReturnCode_t (*return_loan)(void *, DDS_DataReader *);
    PGW_Status (*copy_native)(const void *, void *, size_t);
    DDS_ReturnCode_t (*write)(void *, DDS_DataWriter *, const void *,
                             const struct DDS_Time_t *);
    PGW_Status (*register_keys)(void *, DDS_DataWriter *);
} PGW_DDSBinding;

typedef struct {
    const char *name;
    const char *entity_name;
    const PGW_DDSBinding *binding;
    size_t capacity;
    bool reader;
    bool preserve_source_timestamp;
} PGW_DDSEndpointConfig;

typedef const PGW_DDSEndpointConfig PGW_DDSEndpointConfigElement;
#define REDA_SEQUENCE_USER_API
#define T PGW_DDSEndpointConfigElement
#define TSeq PGW_DDSEndpointConfigSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_DDSEndpointConfigSeq PGW_DDSEndpointConfigSeq;

typedef struct {
    const char *participant_name;
    PGW_DDSEndpointConfigSeq endpoints;
    bool endpoints_initialized;
} PGW_DDSConfig;

typedef struct {
    DDS_Long local_readers, local_writers, local_topics;
    DDS_Long remote_participants, remote_readers, remote_writers;
    DDS_Long factory_participants, factory_components;
    size_t gateway_storage_bytes;
} PGW_DDSResources;
typedef struct {
    DDS_Long instances, samples, samples_per_instance, history_depth;
    DDS_Long blocking_seconds;
    DDS_UnsignedLong blocking_nanoseconds;
} PGW_DDSHistoryResources;
typedef struct {
    uint64_t valid_samples, lifecycle_samples, loans, loan_errors;
    uint64_t accepted, backpressure, invalid, fatal;
    DDS_Long lost, rejected, matched, incompatible_qos;
} PGW_DDSStatistics;
typedef struct {
    struct DDS_Time_t source_timestamp, reception_timestamp;
    struct DDS_SequenceNumber_t publication_sequence_number;
    DDS_InstanceHandle_t publication_handle;
    DDS_InstanceStateKind instance_state;
    DDS_Boolean valid_data;
    DDS_SampleStateKind sample_state;
    DDS_ViewStateKind view_state;
    DDS_InstanceHandle_t instance_handle;
} PGW_DDSMetadata;

PGW_Status PGW_DDS_register_model(const struct APPGEN_LibraryModelSeq *);
PGW_Status PGW_DDS_register_adapter(PGW_Registry *);
PGW_Status PGW_DDS_create(const PGW_DDSConfig *, PGW_Arena *, PGW_Connection **);
PGW_Status PGW_DDS_effective_resources(PGW_Connection *, PGW_DDSResources *);
PGW_Status PGW_DDS_effective_history(PGW_Connection *, const char *,
                                   PGW_DDSHistoryResources *);
PGW_Status PGW_DDS_statistics(PGW_Connection *, const char *, PGW_DDSStatistics *);
PGW_Status PGW_DDS_metadata(const PGW_Representation *, const PGW_Sample *,
                          PGW_DDSMetadata *);
DDS_DomainParticipant *PGW_DDS_participant(PGW_Connection *);
extern const PGW_AdapterI PGW_DDSMicroAdapter;
extern const PGW_ConnectionI PGW_DDSMicroConnection;
#endif

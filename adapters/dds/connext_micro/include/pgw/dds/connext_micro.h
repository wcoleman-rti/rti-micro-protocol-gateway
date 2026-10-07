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

#ifndef PGW_DDS_CONNEXT_MICRO_H
#define PGW_DDS_CONNEXT_MICRO_H
/** @addtogroup pgw_dds_api
 * @{
 */

#include "pgw/core.h"
#include "rti_me_c.h"
#include "app_gen/app_gen.h"

/** @brief Type-specific bridge between Connext Micro DDS data and the gateway.
 *
 * A binding supplies operations for its native sample type. Its implementation
 * owns the native sequence descriptors and scratch storage provisioned from
 * the caller's arena. The binding table, representation, and callback code are
 * borrowed and must outlive all connections using them. Reader samples/metadata
 * are valid only while the DDS loan remains outstanding; return every
 * successful take with @c return_loan before reusing reader storage.
 */
typedef struct {
    const PGW_Representation *representation; /**< Gateway view of the native type. */
    const char *dds_type_name;                 /**< Generated DDS type name. */
    size_t native_size;                        /**< Native sample size in bytes. */
    /** Initialize type-specific sequences/scratch storage from the arena.
     * The arena is caller-owned; capacity is the maximum endpoint sample
     * count; the output state pointer receives the initialized binding state.
     * @return PGW_OK or a gateway status describing allocation/setup failure.
     */
    PGW_Status (*initialize)(PGW_Arena *, size_t, void **);
    /** Take up to @p maximum native samples from a reader.
     * Returns a native DDS result code; a successful take's loan must be
     * returned before the storage is reused. Use DDS_RETCODE_NO_DATA when no
     * samples are available.
     */
    DDS_ReturnCode_t (*take)(void *, DDS_DataReader *, size_t);
    /** Return current native sample count after take. */
    size_t (*length)(void *);
    /** Return a borrowed native sample by index while its DDS loan is active. */
    const void *(*data)(void *, size_t);
    /** Return sample metadata corresponding to a loaned sample index. */
    const struct DDS_SampleInfo *(*info)(void *, size_t);
    /** Return the DDS loan associated with the most recent take. */
    DDS_ReturnCode_t (*return_loan)(void *, DDS_DataReader *);
    /** Copy one native sample into caller storage of the advertised representation. */
    PGW_Status (*copy_native)(const void *, void *, size_t);
    /** Negotiate a borrowed source view before READY.
     * Cross-schema routes require this operation together with write_view.
     * The source representation and its view_contract remain borrowed for the
     * connection lifetime.
     */
    PGW_Status (*bind_view)(void *, const PGW_Representation *);
    /** Translate and write a borrowed source sample view.
     * The callback may inspect payload and metadata but must not retain either
     * borrowed pointer. On PGW_OK it stores the DDS write result in write_result;
     * PGW_UNSUPPORTED permits canonical fallback only for schema-compatible
     * representations; otherwise the individual sample is reported INVALID.
     */
    PGW_Status (*write_view)(void *, DDS_DataWriter *, const PGW_SampleView *,
                             const struct DDS_Time_t *, DDS_ReturnCode_t *);
    /** Write one native sample, optionally with a source timestamp. */
    DDS_ReturnCode_t (*write)(void *, DDS_DataWriter *, const void *,
                             const struct DDS_Time_t *);
    /** Register instance keys with a writer when the type requires it. */
    PGW_Status (*register_keys)(void *, DDS_DataWriter *);
    /** Return the generated type-plugin interface as an opaque identity token.
     * The adapter compares tokens; it does not invoke plugin operations through
     * this callback.
     */
    const void *(*type_identity)(void);
    /** Whether same-type borrowed samples may bypass canonical conversion.
     * Callback-mode bindings require validate_native to be non-null.
     */
    bool direct_write_safe;
    /** Validate a borrowed native DDS sample before direct forwarding, if needed. */
    bool (*validate_native)(const void *);
    /** Write a borrowed native DDS sample without canonical conversion. */
    DDS_ReturnCode_t (*write_native)(void *, DDS_DataWriter *, const void *,
                                    const struct DDS_Time_t *);
} PGW_DDSBinding;

/** @brief Select one named DDS reader or writer endpoint for a connection.
 * @c name is the gateway-facing endpoint name; @c entity_name is the generated
 * participant entity name used for DDS lookup. Strings and binding are borrowed
 * for the connection lifetime. Capacity must be nonzero and fit in INT_MAX.
 * A reader cannot request preserve_source_timestamp.
 */
typedef struct {
    const char *name;                           /**< Unique gateway endpoint name. */
    const char *entity_name;                    /**< DDS participant entity lookup name. */
    const PGW_DDSBinding *binding;              /**< Borrowed generated type binding. */
    size_t capacity;                            /**< Nonzero endpoint sample capacity. */
    bool reader;                                /**< True for reader; false for writer. */
    bool preserve_source_timestamp;             /**< Preserve valid portable source time on writes. */
} PGW_DDSEndpointConfig;

/** @brief Read-only endpoint configuration element used by endpoint sequences. */
typedef const PGW_DDSEndpointConfig PGW_DDSEndpointConfigElement;
#define REDA_SEQUENCE_USER_API
#define T PGW_DDSEndpointConfigElement
#define TSeq PGW_DDSEndpointConfigSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
/** @brief Sequence of borrowed endpoint configuration elements. */
typedef struct PGW_DDSEndpointConfigSeq PGW_DDSEndpointConfigSeq;

/** @brief DDS participant and endpoint configuration for one connection.
 * Endpoint sequence storage, endpoint strings, and binding descriptors are
 * borrowed; keep them alive until the connection is closed. The participant
 * name must identify a participant in the model registered with
 * PGW_DDS_register_model().
 */
typedef struct {
    const char *participant_name;        /**< Registered generated participant name. */
    PGW_DDSEndpointConfigSeq endpoints;  /**< Non-empty endpoint configuration sequence. */
    bool endpoints_initialized;           /**< True when the sequence is ready for use. */
} PGW_DDSConfig;

/** @brief Effective Connext Micro resource allocations for a connection. */
typedef struct {
    DDS_Long local_readers, local_writers, local_topics; /**< Local endpoint/topic limits. */
    DDS_Long remote_participants, remote_readers, remote_writers; /**< Remote discovery limits. */
    DDS_Long factory_participants, factory_components;    /**< Factory-wide limits. */
    size_t gateway_storage_bytes;                          /**< Arena bytes consumed by gateway. */
} PGW_DDSResources;
/** @brief Effective per-endpoint history and blocking QoS resources. */
typedef struct {
    DDS_Long instances, samples, samples_per_instance, history_depth;
    /**< Maximum instance/sample limits and configured history depth. */
    DDS_Long blocking_seconds;          /**< Writer maximum-blocking seconds (zero for readers). */
    DDS_UnsignedLong blocking_nanoseconds; /**< Writer maximum-blocking nanoseconds. */
} PGW_DDSHistoryResources;
/** @brief Gateway and DDS status counters for one endpoint. */
typedef struct {
    uint64_t valid_samples, lifecycle_samples, loans, loan_errors;
    /**< Valid/lifecycle samples taken, DDS loans, and loan-return failures. */
    uint64_t accepted, backpressure, invalid, fatal; /**< Gateway stream-write outcomes. */
    DDS_Long lost, rejected, matched, incompatible_qos;
    /**< DDS lost/rejected totals, current matches, and incompatible-QoS total. */
    uint64_t direct_write_attempts, converted_write_attempts;
    /**< Samples written by native-type fast path or canonical conversion path. */
} PGW_DDSStatistics;
/** @brief Metadata copied from Connext Micro sample information.
 * The sample state, view state, and instance state values are native DDS enums.
 */
typedef struct {
    struct DDS_Time_t source_timestamp, reception_timestamp;
    /**< Writer source and reader reception timestamps. */
    struct DDS_SequenceNumber_t publication_sequence_number; /**< Writer sequence number. */
    DDS_InstanceHandle_t publication_handle;                /**< Publishing data-writer handle. */
    DDS_InstanceStateKind instance_state;                   /**< DDS instance lifecycle state. */
    DDS_Boolean valid_data;                                 /**< Whether sample payload is valid. */
    DDS_SampleStateKind sample_state;                       /**< DDS sample state. */
    DDS_ViewStateKind view_state;                           /**< DDS view state. */
    DDS_InstanceHandle_t instance_handle;                   /**< DDS instance handle. */
} PGW_DDSMetadata;
extern const unsigned char PGW_DDS_METADATA_IDENTITY;

/** @brief Register a generated AppGen library model with the process-global DDS factory.
 * The factory retains the model pointer; the model storage must remain valid
 * for the process lifetime. Re-registering the same pointer succeeds;
 * registering a different model after the first succeeds is invalid.
 * @return PGW_OK on success, PGW_INVALID for malformed/conflicting input,
 *         PGW_UNSUPPORTED for incompatible factory QoS across libraries, or
 *         PGW_FATAL for factory/model registration failure.
 */
PGW_Status PGW_DDS_register_model(const struct APPGEN_LibraryModelSeq *);
#if defined(PGW_ENABLE_REMOTE_CONTROL)
#define PGW_DDS_REMOTE_CONTROL_OPTIONS_VERSION 1u
typedef struct {
    uint32_t version;
    size_t size;
    bool enabled;
    DDS_DomainId_t domain_id;
    uint32_t max_controller_peers;
    uint32_t telemetry_period_ms;
    uint32_t minimum_telemetry_period_ms;
} PGW_DDSRemoteControlOptions;

typedef struct {
    uint32_t version;
    size_t size;
    PGW_Status (*take_command)(DDS_DataReader *, PGW_ControlCommand *,
                               PGW_ControlCorrelation *);
    PGW_Status (*register_state)(DDS_DataWriter *, uint32_t,
                                DDS_InstanceHandle_t *);
    PGW_Status (*register_telemetry)(DDS_DataWriter *,
        const PGW_ControlTelemetryMetric *, DDS_InstanceHandle_t *);
    PGW_Status (*write_state)(DDS_DataWriter *, const PGW_ControlState *,
                              const DDS_InstanceHandle_t *);
    PGW_Status (*write_telemetry)(DDS_DataWriter *, const PGW_ControlTelemetry *,
                                  const DDS_InstanceHandle_t *);
    PGW_Status (*write_result)(DDS_DataWriter *, const PGW_ControlResult *);
    DDS_ReturnCode_t (*write_command)(DDS_DataWriter *, const PGW_ControlCommand *);
    PGW_Status (*take_state)(DDS_DataReader *, PGW_ControlState *);
    PGW_Status (*take_result)(DDS_DataReader *, PGW_ControlResult *);
    PGW_Status (*take_telemetry)(DDS_DataReader *, PGW_ControlTelemetry *);
} PGW_DDSControlTypeI;

typedef struct {
    DDS_DataReader *command_reader;
    DDS_DataWriter *state_writer;
    DDS_DataWriter *result_writer;
    DDS_DataWriter *telemetry_writer;
    const PGW_DDSControlTypeI *types;
    DDS_InstanceHandle_t *state_handles;
    size_t state_handle_count;
    DDS_InstanceHandle_t *telemetry_handles;
    size_t telemetry_handle_count;
    struct DDS_DataReaderListener command_listener;
    _Atomic(const PGW_ReaderListener *) command_listener_target;
    atomic_uint command_callbacks_inflight;
    bool initialized;
} PGW_DDSControlTransport;

typedef struct {
    const char *name;
    bool reader;
    DDS_DataReader *datareader;
    DDS_DataWriter *datawriter;
} PGW_DDSStaticEndpoint;
/** @brief Create one participant and its static AppGen model at a startup-selected domain.
 * The model must already be registered. Missing model factory components are
 * registered from its static descriptors. Static type registrations, topics,
 * endpoint QoS, and entities are reused; only the DDS domain ID is selected at
 * this call. The caller supplies fixed storage for the model's endpoint
 * handles and owns the returned participant. With valid output pointers,
 * endpoint_count is zero on failure and endpoint handles must not be used. If
 * rollback fails, this returns PGW_FATAL with *out set to the participant; the
 * caller must retain it and retry cleanup with
 * PGW_DDS_delete_dynamic_participant().
 */
PGW_Status PGW_DDS_create_participant_at_domain(const char *, DDS_DomainId_t,
    PGW_DDSStaticEndpoint *, size_t, size_t *, DDS_DomainParticipant **);
/** @brief Create the gateway's dedicated, resource-bounded control participant.
 * On rollback failure, PGW_FATAL may leave *out non-NULL; retry cleanup with
 * PGW_DDS_delete_dynamic_participant(). With valid output pointers,
 * endpoint_count is zero on failure; ignore endpoint handles unless PGW_OK.
 */
PGW_Status PGW_DDS_create_control_participant(
    const PGW_DDSRemoteControlOptions *, PGW_DDSStaticEndpoint *, size_t,
    size_t *, DDS_DomainParticipant **);
/** @brief Create the controller-side participant with the inverse fixed bounds.
 * On rollback failure, PGW_FATAL may leave *out non-NULL; retry cleanup with
 * PGW_DDS_delete_dynamic_participant(). With valid output pointers,
 * endpoint_count is zero on failure; ignore endpoint handles unless PGW_OK.
 */
PGW_Status PGW_DDS_create_controller_participant(
    const PGW_DDSRemoteControlOptions *, PGW_DDSStaticEndpoint *, size_t,
    size_t *, DDS_DomainParticipant **);
/** @brief Create the gateway's dedicated, resource-bounded control participant. */
PGW_Status PGW_DDS_create_control_participant(const PGW_DDSRemoteControlOptions *,
    PGW_DDSStaticEndpoint *, size_t, size_t *, DDS_DomainParticipant **);
/** @brief Create the controller-side participant with the inverse fixed bounds. */
PGW_Status PGW_DDS_create_controller_participant(const PGW_DDSRemoteControlOptions *,
    PGW_DDSStaticEndpoint *, size_t, size_t *, DDS_DomainParticipant **);
/** @brief Retry deletion of an owned dynamic participant returned by a creator above. */
PGW_Status PGW_DDS_delete_dynamic_participant(DDS_DomainParticipant *);
/** @brief Validate typed activation/domain/telemetry options before initialization. */
PGW_Status PGW_DDS_remote_control_options_validate(
    const PGW_DDSRemoteControlOptions *, bool, uint32_t);
/** @brief Bind pre-created static control endpoints and pre-register state keys. */
PGW_Status PGW_DDS_control_transport_initialize(
    PGW_DDSControlTransport *, const PGW_DDSStaticEndpoint *, size_t, bool,
    const PGW_DDSControlTypeI *, size_t, DDS_InstanceHandle_t *, size_t,
    const PGW_ControlTelemetryMetric *, size_t, DDS_InstanceHandle_t *, size_t);
/** @brief Obtain the optional core endpoint operations for an initialized transport. */
PGW_ControlEndpoint PGW_DDS_control_endpoint(PGW_DDSControlTransport *);
/** @brief Clear borrowed endpoint and handle references before participant deletion. */
PGW_Status PGW_DDS_control_transport_finalize(PGW_DDSControlTransport *);
#endif
/** @brief Register the Connext Micro adapter in a gateway registry. */
PGW_Status PGW_DDS_register_adapter(PGW_Registry *);
/** @brief Create a DDS connection using registered model entities and an arena.
 * The connection owns the DDS participant/endpoints it creates; the arena,
 * config, endpoint definitions, and bindings are borrowed. The caller must
 * close the connection before releasing the arena or borrowed configuration.
 * @return PGW_OK on success or an applicable PGW_Status for invalid config,
 *         unsupported representation, allocation, discovery entity, or DDS
 *         creation failures.
 */
PGW_Status PGW_DDS_create(const PGW_DDSConfig *, PGW_Arena *, PGW_Connection **);
#if defined(PGW_ENABLE_REMOTE_CONTROL)
/** @brief Resolve a configured DDS connection/endpoint to its control context. */
PGW_Status PGW_DDS_control_target(PGW_Connection *, PGW_ControlResourceKind,
                                  const char *, const PGW_ControlAdapterI **,
                                  void **);
#endif
/** @brief Query effective participant/factory resource limits.
 * @return PGW_OK on success, PGW_INVALID for invalid input/participant query,
 *         or PGW_IO_ERROR if factory QoS cannot be read.
 */
PGW_Status PGW_DDS_effective_resources(PGW_Connection *, PGW_DDSResources *);
/** @brief Query effective history resources for a named endpoint.
 * @return PGW_OK on success, PGW_INVALID for invalid/unknown endpoint, or
 *         PGW_IO_ERROR if DDS QoS cannot be read.
 */
PGW_Status PGW_DDS_effective_history(PGW_Connection *, const char *,
                                   PGW_DDSHistoryResources *);
/** @brief Copy gateway and DDS status counters for a named endpoint.
 * @return PGW_OK on success, PGW_INVALID for invalid/unknown endpoint, or
 *         PGW_IO_ERROR if a DDS status query fails.
 */
PGW_Status PGW_DDS_statistics(PGW_Connection *, const char *, PGW_DDSStatistics *);
/** @brief Copy DDS metadata from a sample belonging to this adapter.
 * The sample must still be valid, typically within its outstanding reader
 * loan, and the representation must be the Connext Micro representation.
 * @return PGW_OK on success or PGW_INVALID for unrelated/invalid inputs.
 */
PGW_Status PGW_DDS_metadata(const PGW_Representation *, const PGW_Sample *,
                          PGW_DDSMetadata *);
/** @brief Get the connection-owned DDS participant.
 * The returned participant is borrowed; do not delete it. It becomes invalid
 * when the connection is closed. Returns null for a null connection.
 */
DDS_DomainParticipant *PGW_DDS_participant(PGW_Connection *);
/** @brief Connext Micro adapter descriptor. */
extern const PGW_AdapterI PGW_DDSConnextMicroAdapter;
/** @brief Connext Micro connection interface descriptor. */
extern const PGW_ConnectionI PGW_DDSConnextMicroConnection;
/** @} */
#endif

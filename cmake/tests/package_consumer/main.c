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

#include <pgw/core.h>
#include <pgw/runtime.h>
#include <pgw/local_sink.h>
#include <pgw/can_memory.h>
#include <pgw/can_socketcan.h>
#include <pgw/dds/connext_micro.h>
#include <pgw/signal.h>
#include <osapi/osapi_system.h>

typedef struct {
    uint32_t identifier;
    int64_t value;
} ConsumerRecord;
#define T ConsumerRecord
#define TSeq ConsumerRecordSeq
#define REDA_SEQUENCE_API REDA_SEQUENCE_API_UNTYPED
#define TSeq_initialize
#define TSeq_finalize
#define TSeq_get_reference
#define TSeq_loan_contiguous
#define TSeq_unloan
#include "reda/reda_sequence_decl.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate
typedef struct ConsumerRecordSeq ConsumerRecordSeq;

#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"
#define T ConsumerRecord
#define TSeq ConsumerRecordSeq
#define REDA_SEQUENCE_API REDA_SEQUENCE_API_UNTYPED
#define TSeq_initialize
#define TSeq_finalize
#define TSeq_get_reference
#define TSeq_loan_contiguous
#define TSeq_unloan
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate

int main(void)
{
    PGW_SampleSeq samples;
    ConsumerRecord records[1] = {{7, 42}};
    ConsumerRecordSeq records_seq;
    PGW_AdapterRef adapter_refs[2];
    PGW_RepresentationRef representation_refs[1];
    PGW_AdapterSeq adapter_sequence;
    PGW_RepresentationSeq representation_sequence;
    PGW_Registry registry = {0};
    PGW_CANMemory memory;
    PGW_CANFrame rx[1], tx[1];
    PGW_CANSocket socket = {.fd = -1};
    PGW_Route uninitialized_route = {0};
    PGW_SampleView sample_view = {
        .kind = PGW_SAMPLE_VIEW_CANONICAL,
        .value = &records[0],
        .value_size = sizeof(records[0])
    };
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    PGW_DDSRemoteControlOptions control_options = {
        PGW_DDS_REMOTE_CONTROL_OPTIONS_VERSION,
        sizeof(PGW_DDSRemoteControlOptions), false, 0, 0
    };
#endif
    PGW_Status (*volatile sink_initialize)(PGW_LocalSink *, int, char *, size_t)
        = PGW_LocalSink_initialize;
    (void)sink_initialize;
    if (PGW_Route_pause(&uninitialized_route) != PGW_INVALID ||
        PGW_Route_resume(&uninitialized_route) != PGW_INVALID ||
        PGW_status_name(PGW_NO_CHANGE) == NULL) {
        return 5;
    }
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    if (PGW_DDS_remote_control_options_validate(&control_options, false, 0) != PGW_OK)
        return 6;
#endif
    if (!PGW_Runtime_initialize() ||
        !PGW_SampleSeq_initialize(&samples) ||
        !PGW_SampleSeq_finalize(&samples)) {
        return 1;
    }
    if (!ConsumerRecordSeq_initialize(&records_seq) ||
        !ConsumerRecordSeq_loan_contiguous(&records_seq, records, 1, 1) ||
        ConsumerRecordSeq_get_reference(&records_seq, 0) == NULL ||
        ConsumerRecordSeq_get_reference(&records_seq, 0)->value != 42 ||
        !ConsumerRecordSeq_unloan(&records_seq) ||
        !ConsumerRecordSeq_finalize(&records_seq) ||
        !PGW_AdapterSeq_initialize(&adapter_sequence) ||
        !PGW_AdapterSeq_loan_contiguous(&adapter_sequence, adapter_refs, 0, 2) ||
        !PGW_RepresentationSeq_initialize(&representation_sequence) ||
        !PGW_RepresentationSeq_loan_contiguous(&representation_sequence,
                                               representation_refs, 0, 1) ||
        PGW_Registry_initialize(&registry, &adapter_sequence, &representation_sequence) != PGW_OK ||
        PGW_Registry_register_adapter(&registry, &PGW_CANAdapter) != PGW_OK ||
        PGW_Registry_register_adapter(&registry, &PGW_DDSConnextMicroAdapter) != PGW_OK ||
        PGW_AdapterSeq_get_length(&registry.adapters) != 2 ||
        PGW_Registry_finalize(&registry) != PGW_OK ||
        !PGW_AdapterSeq_unloan(&adapter_sequence) ||
        !PGW_AdapterSeq_finalize(&adapter_sequence) ||
        !PGW_RepresentationSeq_unloan(&representation_sequence) ||
        !PGW_RepresentationSeq_finalize(&representation_sequence)) {
        return 4;
    }
    if (PGW_CANMemory_initialize(&memory, rx, 1, tx, 1) != PGW_OK ||
        PGW_CANAdapter.version != PGW_ABI_VERSION ||
        PGW_DDSConnextMicroAdapter.version != PGW_ABI_VERSION ||
        PGW_DDS_participant(NULL) != NULL) {
        return 2;
    }
    PGW_CANTransport memory_transport = PGW_CANMemory_transport(&memory);
    PGW_CANTransport socket_transport = PGW_CANSocket_transport(&socket);
    (void)memory_transport;
    (void)socket_transport;
    if (PGW_CANMemory_finalize(&memory) != PGW_OK) return 3;
    return OSAPI_System_finalize() ? 0 : 3;
}

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

#include "pgw/can.h"
#if defined(PGW_ENABLE_REMOTE_CONTROL)
#include "control_manifest.h"
#endif
#include <math.h>
#include <string.h>

typedef struct PGW_CANSample {
    PGW_Signal signal;
    PGW_CANMetadata metadata;
} PGW_CANSample;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANSample
#define TSeq PGW_CANSampleSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANSampleSeq PGW_CANSampleSeq;

#define REDA_SEQUENCE_USER_API
#define T PGW_CANSample
#define TSeq PGW_CANLoanSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANLoanSeq PGW_CANLoanSeq;

typedef struct PGW_CANMessage {
    PGW_CANFrame received, shadow, stage;
    bool baseline, staged;
} PGW_CANMessage;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANMessage
#define TSeq PGW_CANMessageSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANMessageSeq PGW_CANMessageSeq;

typedef struct PGW_CANConnection PGW_CANConnection;
typedef struct PGW_CANCategoryState {
    PGW_CANConnection *connection;
    const char *name;
    PGW_Representation representation;
    PGW_SampleViewDescriptor view_contract;
    PGW_CANSampleSeq queue;
    PGW_CANLoanSeq loan;
    size_t head, count;
    PGW_SampleSeq *borrowed;
    const PGW_Representation *source;
    bool queue_initialized;
    bool queue_borrowed;
    bool loan_initialized;
    bool loan_borrowed;
    bool input_enabled;
    bool output_enabled;
} PGW_CANCategoryState;
#define REDA_SEQUENCE_USER_API
#define T PGW_CANCategoryState
#define TSeq PGW_CANCategoryStateSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANCategoryStateSeq PGW_CANCategoryStateSeq;
#define REDA_SEQUENCE_USER_API
#define T PGW_Signal
#define TSeq PGW_CANDecodedSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANDecodedSeq PGW_CANDecodedSeq;
#define REDA_SEQUENCE_USER_API
#define T size_t
#define TSeq PGW_CANIndexSeq
#include <reda/reda_sequence_decl.h>
#undef T
#undef TSeq
typedef struct PGW_CANIndexSeq PGW_CANIndexSeq;

struct PGW_CANConnection {
    PGW_CANConfig config;
    PGW_CANCategoryStateSeq categories;
    PGW_CANMessageSeq messages;
    PGW_CANDecodedSeq decode;
    PGW_CANIndexSeq write_messages;
    PGW_CANStats stats;
    PGW_Counters counters;
    bool closed;
    bool control_enabled;
    uint32_t sequence_initialization;
};
#if defined(PGW_ENABLE_REMOTE_CONTROL)
static const PGW_ControlAdapterI can_control;
#endif

#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"

#define REDA_SEQUENCE_USER_API
#define T PGW_CANSample
#define TSeq PGW_CANSampleSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T PGW_CANMessageDefinition
#define TSeq PGW_CANMessageDefinitionSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T PGW_CANSignalDefinition
#define TSeq PGW_CANSignalDefinitionSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T PGW_CANCategoryDefinition
#define TSeq PGW_CANCategorySeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T PGW_CANSample
#define TSeq PGW_CANLoanSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T PGW_CANMessage
#define TSeq PGW_CANMessageSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T PGW_CANCategoryState
#define TSeq PGW_CANCategoryStateSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T PGW_Signal
#define TSeq PGW_CANDecodedSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

#define REDA_SEQUENCE_USER_API
#define T size_t
#define TSeq PGW_CANIndexSeq
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq

PGW_Status PGW_CANMapping_initialize(PGW_CANMapping *mapping,
    const PGW_MessageDescriptor *messages, size_t message_count,
    const PGW_SignalDescriptor *signals, size_t signal_count,
    void *context, PGW_CANDecode decode, PGW_CANPatch patch)
{
    if (!mapping || !messages || !message_count || !signals || !signal_count ||
        !decode || !patch || message_count > INT32_MAX || signal_count > INT32_MAX ||
        mapping->initialized)
        return PGW_INVALID;
    if (!PGW_CANMessageDefinitionSeq_initialize(&mapping->messages)) return PGW_FATAL;
    if (!PGW_CANSignalDefinitionSeq_initialize(&mapping->signals)) {
        PGW_CANMessageDefinitionSeq_finalize(&mapping->messages);
        return PGW_FATAL;
    }
    mapping->initialized = true;
    /* REDA accepts void * even when T is const; this view is never written. */
    if (!PGW_CANMessageDefinitionSeq_loan_contiguous(&mapping->messages,
            (void *)messages, (RTI_INT32)message_count, (RTI_INT32)message_count)) {
        PGW_CANMapping_finalize(mapping);
        return PGW_CAPACITY;
    }
    mapping->messages_borrowed = true;
    if (!PGW_CANSignalDefinitionSeq_loan_contiguous(&mapping->signals,
            (void *)signals, (RTI_INT32)signal_count, (RTI_INT32)signal_count)) {
        PGW_CANMapping_finalize(mapping);
        return PGW_CAPACITY;
    }
    mapping->signals_borrowed = true;
    mapping->context = context;
    mapping->decode = decode;
    mapping->patch = patch;
    return PGW_OK;
}

PGW_Status PGW_CANMapping_finalize(PGW_CANMapping *mapping)
{
    bool ok = true;
    if (!mapping) return PGW_INVALID;
    if (mapping->initialized) {
        if (mapping->messages_borrowed)
            ok = PGW_CANMessageDefinitionSeq_unloan(&mapping->messages);
        ok = PGW_CANMessageDefinitionSeq_finalize(&mapping->messages) && ok;
        if (mapping->signals_borrowed)
            ok = PGW_CANSignalDefinitionSeq_unloan(&mapping->signals) && ok;
        ok = PGW_CANSignalDefinitionSeq_finalize(&mapping->signals) && ok;
    }
    if (ok) {
        mapping->initialized = false;
        mapping->messages_borrowed = false;
        mapping->signals_borrowed = false;
    }
    return ok ? PGW_OK : PGW_LOAN_ERROR;
}

PGW_Status PGW_CANConfig_set_categories(PGW_CANConfig *cfg,
                                        const PGW_CANCategorySeq *categories)
{
    if (!cfg || !categories ||
        cfg->categories_initialized) return PGW_INVALID;
    RTI_INT32 length = PGW_CANCategorySeq_get_length(categories);
    RTI_INT32 capacity = PGW_CANCategorySeq_get_maximum(categories);
    PGW_CANCategoryDefinition *buffer = PGW_CANCategorySeq_get_contiguous_buffer(categories);
    if (length <= 0 || capacity < length || !buffer) return PGW_INVALID;
    if (!PGW_CANCategorySeq_initialize(&cfg->categories)) return PGW_FATAL;
    if (!PGW_CANCategorySeq_loan_contiguous(&cfg->categories, buffer, length, capacity)) {
        PGW_CANCategorySeq_finalize(&cfg->categories);
        return PGW_CAPACITY;
    }
    cfg->categories_initialized = true;
    cfg->categories_borrowed = true;
    return PGW_OK;
}

PGW_Status PGW_CANConfig_finalize(PGW_CANConfig *cfg)
{
    bool ok = true;
    if (!cfg) return PGW_INVALID;
    if (cfg->categories_initialized) {
        if (cfg->categories_borrowed)
            ok = PGW_CANCategorySeq_unloan(&cfg->categories);
        ok = PGW_CANCategorySeq_finalize(&cfg->categories) && ok;
    }
    if (ok) {
        cfg->categories_initialized = false;
        cfg->categories_borrowed = false;
    }
    return PGW_CANMapping_finalize(&cfg->mapping) == PGW_OK && ok
        ? PGW_OK : PGW_LOAN_ERROR;
}

bool PGW_CANFrame_valid(const PGW_CANFrame *f)
{
    const uint32_t known = PGW_CAN_FLAG_EXTENDED | PGW_CAN_FLAG_FD |
        PGW_CAN_FLAG_RTR | PGW_CAN_FLAG_ERROR | PGW_CAN_FLAG_ECHO |
        PGW_CAN_FLAG_BRS | PGW_CAN_FLAG_ESI;
    if (!f || (f->flags & ~known) || f->length > 64) return false;
    if (f->id > ((f->flags & (PGW_CAN_FLAG_EXTENDED | PGW_CAN_FLAG_ERROR))
                     ? UINT32_C(0x1fffffff) : UINT32_C(0x7ff))) return false;
    if (!(f->flags & PGW_CAN_FLAG_FD) &&
        (f->length > 8 || (f->flags & (PGW_CAN_FLAG_BRS | PGW_CAN_FLAG_ESI))))
        return false;
    if ((f->flags & PGW_CAN_FLAG_FD) &&
        (f->flags & (PGW_CAN_FLAG_RTR | PGW_CAN_FLAG_ERROR))) return false;
    return !f->timestamp.valid || f->timestamp.nanoseconds < 1000000000u;
}

static void event(PGW_CANConnection *c, uint32_t code, uint64_t count)
{
    PGW_Event e = {.entity_id = c->config.entity_id, .code = code,
                   .severity = 2, .count = count};
    if (c->config.diagnostics) PGW_Diagnostics_emit(c->config.diagnostics, &e);
}

static PGW_Status copy_value(const PGW_Sample *s, void *v, size_t size)
{
    if (!s || !v || size != sizeof(PGW_Signal)) return PGW_INVALID;
    memcpy(v, &((const PGW_CANSample *)s)->signal, size);
    return PGW_OK;
}

static PGW_Status timestamp(const PGW_Sample *s, PGW_Timestamp *t)
{
    if (!s || !t) return PGW_INVALID;
    *t = ((const PGW_CANSample *)s)->metadata.timestamp;
    return t->valid ? PGW_OK : PGW_NO_DATA;
}

const unsigned char PGW_CAN_METADATA_IDENTITY = 0;
const unsigned char PGW_CAN_SIGNAL_VALUE_IDENTITY = 0;

static PGW_Status sample_view(const PGW_Sample *opaque, PGW_SampleView *view)
{
    const PGW_CANSample *sample = (const PGW_CANSample *)opaque;
    if (!sample || !view) return PGW_INVALID;
    *view = (PGW_SampleView){
        .kind = PGW_SAMPLE_VIEW_CANONICAL,
        .value = &sample->signal,
        .value_size = sizeof(sample->signal),
        .type_identity = &PGW_CAN_SIGNAL_VALUE_IDENTITY,
        .context = &sample->metadata,
        .context_identity = &PGW_CAN_METADATA_IDENTITY
    };
    return PGW_OK;
}

static const PGW_SampleAccessI sample_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), copy_value, timestamp, sample_view
};

static const PGW_SignalDescriptor *signal_by_id(PGW_CANConnection *c, uint32_t id)
{
    RTI_INT32 i;
    for (i = 0; i < PGW_CANSignalDefinitionSeq_get_length(&c->config.mapping.signals); ++i) {
        const PGW_SignalDescriptor *signal =
            PGW_CANSignalDefinitionSeq_get_reference(&c->config.mapping.signals, i);
        if (signal->id == id) return signal;
    }
    return NULL;
}

PGW_Status PGW_CAN_poll(PGW_Connection *connection, size_t budget)
{
    PGW_CANConnection *c = (PGW_CANConnection *)connection;
    size_t n;
    uint64_t before;
    PGW_Status result = PGW_OK;
    if (!c || c->closed) return PGW_INVALID;
    if (!c->control_enabled) return PGW_OK;
    before = c->stats.receive_drops;
    if (budget > c->config.receive_budget) budget = c->config.receive_budget;
    for (n = 0; n < budget; ++n) {
        PGW_CANFrame frame;
        RTI_INT32 m, i, k;
        size_t count = 0;
        PGW_Status status = c->config.transport.iface->receive(
            c->config.transport.state, &frame);
        if (status == PGW_NO_DATA) break;
        if (status == PGW_INVALID) {
            ++c->stats.received_frames;
            ++c->stats.malformed_frames;
            PGW_Counters_add(&c->counters, PGW_COUNT_FRAMES, 1);
            PGW_Counters_add(&c->counters, PGW_COUNT_INVALID, 1);
            continue;
        }
        if (status != PGW_OK) {
            ++c->stats.io_errors;
            PGW_Counters_add(&c->counters, PGW_COUNT_FATAL, 1);
            event(c, 1001, 1);
            result = status;
            break;
        }
        ++c->stats.received_frames;
        PGW_Counters_add(&c->counters, PGW_COUNT_FRAMES, 1);
        if (!PGW_CANFrame_valid(&frame) ||
            (frame.flags & (PGW_CAN_FLAG_RTR | PGW_CAN_FLAG_ERROR))) {
            ++c->stats.malformed_frames;
            PGW_Counters_add(&c->counters, PGW_COUNT_INVALID, 1);
            continue;
        }
        if (frame.flags & PGW_CAN_FLAG_ECHO) {
            ++c->stats.echoes;
            continue;
        }
        for (m = 0; m < PGW_CANMessageDefinitionSeq_get_length(&c->config.mapping.messages); ++m) {
            const PGW_MessageDescriptor *d =
                PGW_CANMessageDefinitionSeq_get_reference(&c->config.mapping.messages, m);
            if (d->frame_id == frame.id &&
                d->extended == !!(frame.flags & PGW_CAN_FLAG_EXTENDED) &&
                d->fd == !!(frame.flags & PGW_CAN_FLAG_FD)) break;
        }
        if (m == PGW_CANMessageDefinitionSeq_get_length(&c->config.mapping.messages)) {
            ++c->stats.unknown_frames;
            continue;
        }
        if (frame.length != PGW_CANMessageDefinitionSeq_get_reference(
                &c->config.mapping.messages, m)->length ||
            c->config.mapping.decode(c->config.mapping.context, (size_t)m, frame.data,
                frame.length, PGW_CANDecodedSeq_get_contiguous_buffer(&c->decode),
                (size_t)PGW_CANDecodedSeq_get_maximum(&c->decode),
                &count) != PGW_CODEC_OK ||
            count > (size_t)INT32_MAX ||
            !PGW_CANDecodedSeq_set_length(&c->decode, (RTI_INT32)count)) {
            ++c->stats.malformed_frames;
            PGW_Counters_add(&c->counters, PGW_COUNT_INVALID, 1);
            continue;
        }
        PGW_CANMessage *message = PGW_CANMessageSeq_get_reference(&c->messages, m);
        message->received = frame;
        message->shadow = frame;
        message->baseline = true;
        for (i = 0; i < PGW_CANDecodedSeq_get_length(&c->decode); ++i) {
            const PGW_Signal *decoded = PGW_CANDecodedSeq_get_reference(&c->decode, i);
            const PGW_SignalDescriptor *d = signal_by_id(c, decoded->id);
            if (!d || d->message_index != (size_t)m) {
                ++c->stats.receive_drops;
                PGW_Counters_add(&c->counters, PGW_COUNT_INVALID, 1);
                continue;
            }
            ++c->stats.decoded_samples;
            PGW_Counters_add(&c->counters, PGW_COUNT_RECEIVED, 1);
            for (k = 0; k < PGW_CANCategoryStateSeq_get_length(&c->categories); ++k) {
                PGW_CANCategoryState *cat =
                    PGW_CANCategoryStateSeq_get_reference(&c->categories, k);
                PGW_CANSample *sample;
                if (strcmp(cat->name, d->category)) continue;
                if (!cat->input_enabled) break;
                size_t capacity = (size_t)PGW_CANSampleSeq_get_maximum(&cat->queue);
                if (cat->count == capacity) {
                    ++c->stats.receive_drops;
                    PGW_Counters_add(&c->counters, PGW_COUNT_OVERFLOW, 1);
                    break;
                }
                sample = PGW_CANSampleSeq_get_reference(
                    &cat->queue, (RTI_INT32)((cat->head + cat->count) % capacity));
                sample->signal = *decoded;
                if (!c->config.disable_metadata_capture) {
                    sample->metadata = (PGW_CANMetadata){
                        frame.timestamp, frame.id, frame.flags, frame.interface_index
                    };
                }
                ++cat->count;
                if (cat->count > c->stats.queue_high_water) {
                    PGW_Counters_add(&c->counters, PGW_COUNT_HIGH_WATER,
                                     cat->count - c->stats.queue_high_water);
                    c->stats.queue_high_water = cat->count;
                }
                break;
            }
        }
    }
    if (c->stats.receive_drops != before)
        event(c, 1003, c->stats.receive_drops - before);
    return result;
}

static PGW_Status read_samples(void *state, PGW_SampleSeq *seq, size_t budget)
{
    PGW_CANCategoryState *cat = state;
    size_t n;
    RTI_INT32 i;
    PGW_Status status;
    if (!seq || !PGW_SampleSeq_get_contiguous_buffer(seq) ||
        cat->connection->closed) return PGW_INVALID;
    if (!cat->connection->control_enabled || !cat->input_enabled)
        return PGW_NO_DATA;
    if (cat->borrowed) {
        PGW_Counters_add(&cat->connection->counters, PGW_COUNT_LOAN_ERRORS, 1);
        return PGW_LOAN_ERROR;
    }
    status = PGW_CAN_poll((PGW_Connection *)cat->connection,
                          cat->connection->config.receive_budget);
    if (status != PGW_OK) return status;
    n = cat->count;
    if (n > budget) n = budget;
    if (n > (size_t)PGW_SampleSeq_get_maximum(seq))
        n = (size_t)PGW_SampleSeq_get_maximum(seq);
    if (!n) return PGW_NO_DATA;
    if (n > (size_t)INT32_MAX ||
        !PGW_SampleSeq_set_length(seq, (RTI_INT32)n) ||
        !PGW_CANLoanSeq_set_length(&cat->loan, (RTI_INT32)n)) {
        PGW_SampleSeq_set_length(seq, 0);
        PGW_Counters_add(&cat->connection->counters, PGW_COUNT_LOAN_ERRORS, 1);
        return PGW_LOAN_ERROR;
    }
    cat->loan_borrowed = true;
    for (i = 0; i < (RTI_INT32)n; ++i) {
        PGW_CANSample *sample = PGW_CANLoanSeq_get_reference(&cat->loan, i);
        *sample = *PGW_CANSampleSeq_get_reference(&cat->queue, (RTI_INT32)cat->head);
        cat->head = (cat->head + 1) % (size_t)PGW_CANSampleSeq_get_maximum(&cat->queue);
        --cat->count;
        *PGW_SampleSeq_get_reference(seq, i) = (PGW_SampleRef)sample;
    }
    cat->borrowed = seq;
    PGW_Counters_add(&cat->connection->counters, PGW_COUNT_LOANS, 1);
    return PGW_OK;
}

static PGW_Status return_loan(void *state, PGW_SampleSeq *seq)
{
    PGW_CANCategoryState *cat = state;
    if (!seq || cat->borrowed != seq) {
        PGW_Counters_add(&cat->connection->counters, PGW_COUNT_LOAN_ERRORS, 1);
        return PGW_LOAN_ERROR;
    }
    if (!PGW_SampleSeq_set_length(seq, 0) || !PGW_CANLoanSeq_set_length(&cat->loan, 0)) {
        PGW_Counters_add(&cat->connection->counters, PGW_COUNT_LOAN_ERRORS, 1);
        return PGW_LOAN_ERROR;
    }
    cat->borrowed = NULL;
    PGW_Counters_add(&cat->connection->counters, PGW_COUNT_LOANS, UINT64_MAX);
    return PGW_OK;
}

static PGW_Status bind_source(void *state, const PGW_Representation *source)
{
    PGW_CANCategoryState *cat = state;
    if (!source || !PGW_schema_equal(source->schema, cat->representation.schema) ||
        !source->access || source->access->version != PGW_ABI_VERSION ||
        source->access->size != sizeof(PGW_SampleAccessI) ||
        (!source->access->copy_value && !source->access->view))
        return PGW_UNSUPPORTED;
    if (source->access->view && !source->view_contract)
        return PGW_UNSUPPORTED;
    if (!source->access->copy_value &&
        (source->view_contract->kind != PGW_SAMPLE_VIEW_CANONICAL ||
         source->view_contract->value_size != sizeof(PGW_Signal) ||
         source->view_contract->type_identity != &PGW_CAN_SIGNAL_VALUE_IDENTITY))
        return PGW_UNSUPPORTED;
    if (cat->source && cat->source != source) return PGW_UNSUPPORTED;
    cat->source = source;
    return PGW_OK;
}

static PGW_Status sample_signal(const PGW_Representation *source,
                               const PGW_Sample *sample,
                               const PGW_Signal **value,
                               PGW_Signal *copy)
{
    const PGW_SampleAccessI *access = source->access;
    if (access->view) {
        PGW_SampleView view;
        PGW_Status status = access->view(sample, &view);
        if (status == PGW_OK) {
            if (!view.value || ((view.context == NULL) !=
                                (view.context_identity == NULL)) ||
                !source->view_contract ||
                view.kind != source->view_contract->kind ||
                view.value_size != source->view_contract->value_size ||
                view.type_identity != source->view_contract->type_identity ||
                view.context_identity != source->view_contract->context_identity)
                return PGW_INVALID;
            if (view.kind == PGW_SAMPLE_VIEW_CANONICAL &&
                view.value_size == sizeof(PGW_Signal)) {
                *value = view.value;
                return PGW_OK;
            }
            if (view.kind != PGW_SAMPLE_VIEW_NATIVE)
                return PGW_INVALID;
        } else if (status != PGW_UNSUPPORTED) {
            return status;
        }
    }
    if (!access->copy_value) return PGW_UNSUPPORTED;
    PGW_Status status = access->copy_value(sample, copy, sizeof(*copy));
    if (status == PGW_OK) *value = copy;
    return status;
}

static PGW_Status write_samples(void *state, const PGW_SampleSeq *seq,
                               PGW_WriteResultSeq *results)
{
    PGW_CANCategoryState *cat = state;
    PGW_CANConnection *c = cat->connection;
    size_t n;
    RTI_INT32 i, m;
    uint64_t invalid_before = c->stats.invalid_commands;
    uint64_t backpressure_before = c->stats.backpressure_commands;
    PGW_Status overall = PGW_OK;
    if (!seq || !results || !PGW_WriteResultSeq_get_contiguous_buffer(results) ||
        !cat->source || c->closed) return PGW_INVALID;
    n = (size_t)PGW_SampleSeq_get_length(seq);
    if ((size_t)PGW_WriteResultSeq_get_length(results) != n || n > (size_t)INT32_MAX)
        return PGW_INVALID;
    if (!c->control_enabled || !cat->output_enabled) {
        for (i = 0; i < (RTI_INT32)n; ++i) {
            *PGW_WriteResultSeq_get_reference(results, i) = PGW_WRITE_BACKPRESSURE;
            ++c->stats.backpressure_commands;
            PGW_Counters_add(&c->counters, PGW_COUNT_BACKPRESSURE, 1);
        }
        return PGW_OK;
    }
    if (!PGW_CANIndexSeq_set_length(&c->write_messages, (RTI_INT32)n))
        return PGW_CAPACITY;
    for (m = 0; m < PGW_CANMessageSeq_get_length(&c->messages); ++m)
        PGW_CANMessageSeq_get_reference(&c->messages, m)->staged = false;
    for (i = 0; i < (RTI_INT32)n; ++i) {
        PGW_Signal copied_value;
        const PGW_Signal *value;
        const PGW_SignalDescriptor *d;
        PGW_CANMessage *msg;
        PGW_CANFrame candidate;
        *PGW_CANIndexSeq_get_reference(&c->write_messages, i) = SIZE_MAX;
        *PGW_WriteResultSeq_get_reference(results, i) = PGW_WRITE_INVALID;
        if (sample_signal(cat->source,
                *PGW_SampleSeq_get_reference(seq, i), &value,
                &copied_value) != PGW_OK)
            goto invalid;
        d = signal_by_id(c, value->id);
        if (!d || strcmp(d->category, cat->name) || d->kind != value->value.kind ||
            (value->value.kind == PGW_VALUE_DOUBLE && !isfinite(value->value.data.real)))
            goto invalid;
        msg = PGW_CANMessageSeq_get_reference(&c->messages, d->message_index);
        if (!msg->baseline) {
            ++c->stats.missing_baseline;
            goto invalid;
        }
        candidate = msg->staged ? msg->stage : msg->shadow;
        if (c->config.mapping.patch(c->config.mapping.context, value->id,
                &value->value, candidate.data, candidate.length) != PGW_CODEC_OK)
            goto invalid;
        msg->stage = candidate;
        msg->staged = true;
        *PGW_CANIndexSeq_get_reference(&c->write_messages, i) = d->message_index;
        continue;
invalid:
        ++c->stats.invalid_commands;
        PGW_Counters_add(&c->counters, PGW_COUNT_INVALID, 1);
    }
    for (m = 0; m < PGW_CANMessageSeq_get_length(&c->messages); ++m) {
        PGW_CANMessage *msg = PGW_CANMessageSeq_get_reference(&c->messages, m);
        PGW_Status status;
        if (!msg->staged) continue;
        msg->stage.flags &= ~PGW_CAN_FLAG_ECHO;
        msg->stage.timestamp.valid = false;
        status = c->config.transport.iface->send(c->config.transport.state,
                                                &msg->stage);
        if (status == PGW_OK) msg->shadow = msg->stage;
        else if (status != PGW_BACKPRESSURE) {
            ++c->stats.io_errors;
            overall = PGW_IO_ERROR;
            event(c, 1002, 1);
        }
        for (i = 0; i < PGW_CANIndexSeq_get_length(&c->write_messages); ++i) {
            if (*PGW_CANIndexSeq_get_reference(&c->write_messages, i) != (size_t)m) continue;
            if (status == PGW_OK) {
                *PGW_WriteResultSeq_get_reference(results, i) = PGW_WRITE_ACCEPTED;
                ++c->stats.accepted_commands;
                PGW_Counters_add(&c->counters, PGW_COUNT_ACCEPTED, 1);
            } else if (status == PGW_BACKPRESSURE) {
                *PGW_WriteResultSeq_get_reference(results, i) = PGW_WRITE_BACKPRESSURE;
                ++c->stats.backpressure_commands;
                PGW_Counters_add(&c->counters, PGW_COUNT_BACKPRESSURE, 1);
            } else {
                *PGW_WriteResultSeq_get_reference(results, i) = PGW_WRITE_FATAL;
                PGW_Counters_add(&c->counters, PGW_COUNT_FATAL, 1);
            }
        }
        msg->staged = false;
    }
    if (c->stats.invalid_commands != invalid_before)
        event(c, 1004, c->stats.invalid_commands - invalid_before);
    if (c->stats.backpressure_commands != backpressure_before)
        event(c, 1005, c->stats.backpressure_commands - backpressure_before);
    return overall;
}

static const PGW_StreamReaderI reader_iface = {
    PGW_ABI_VERSION, sizeof(PGW_StreamReaderI), read_samples, return_loan
};
static const PGW_StreamWriterI writer_iface = {
    PGW_ABI_VERSION, sizeof(PGW_StreamWriterI), bind_source, write_samples
};

static PGW_Status get_reader(PGW_Connection *connection, const char *name,
                             PGW_StreamReader *out)
{
    PGW_CANConnection *c = (PGW_CANConnection *)connection;
    RTI_INT32 i;
    if (!c || !name || !out || c->closed) return PGW_INVALID;
    for (i = 0; i < PGW_CANCategoryStateSeq_get_length(&c->categories); ++i) {
        PGW_CANCategoryState *cat = PGW_CANCategoryStateSeq_get_reference(&c->categories, i);
        if (!strcmp(name, cat->name)) {
            *out = (PGW_StreamReader){cat, &reader_iface, &cat->representation};
            return PGW_OK;
        }
    }
    return PGW_INVALID;
}

static PGW_Status get_writer(PGW_Connection *connection, const char *name,
                             PGW_StreamWriter *out)
{
    PGW_StreamReader r;
    PGW_Status status = get_reader(connection, name, &r);
    if (!out) return PGW_INVALID;
    if (status == PGW_OK)
        *out = (PGW_StreamWriter){r.state, &writer_iface, r.representation};
    return status;
}

#if defined(PGW_ENABLE_REMOTE_CONTROL)
PGW_Status PGW_CAN_control_target(PGW_Connection *opaque,
                                  PGW_ControlResourceKind kind,
                                  const char *name,
                                  const PGW_ControlAdapterI **iface,
                                  void **state)
{
    PGW_CANConnection *connection = (PGW_CANConnection *)opaque;
    if (!connection || connection->closed || !name || !*name || !iface || !state)
        return PGW_INVALID;
    if (kind == PGW_CONTROL_RESOURCE_CONNECTION) {
        *iface = &can_control;
        *state = connection;
        return PGW_OK;
    }
    if (kind != PGW_CONTROL_RESOURCE_INPUT && kind != PGW_CONTROL_RESOURCE_OUTPUT)
        return PGW_UNSUPPORTED;
    for (RTI_INT32 i = 0; i < PGW_CANCategoryStateSeq_get_length(&connection->categories); ++i) {
        PGW_CANCategoryState *category =
            PGW_CANCategoryStateSeq_get_reference(&connection->categories, i);
        if (!strcmp(category->name, name)) {
            *iface = &can_control;
            *state = category;
            return PGW_OK;
        }
    }
    return PGW_INVALID;
}
#endif

static PGW_Status release_sequences(PGW_CANConnection *c)
{
    bool ok = true;
    RTI_INT32 i;
#define RELEASE(Name, member, init_bit, borrowed_bit) do { \
    if (c->sequence_initialization & (UINT32_C(1) << (init_bit))) { \
        if ((c->sequence_initialization & (UINT32_C(1) << (borrowed_bit))) && \
            !Name##_unloan(&(member))) { fprintf(stderr, #Name " unloan fail\n"); ok = false; } \
        if (!Name##_finalize(&(member))) { fprintf(stderr, #Name " finalize fail\n"); ok = false; } \
        c->sequence_initialization &= ~((UINT32_C(1) << (init_bit)) | \
                                         (UINT32_C(1) << (borrowed_bit))); \
    } \
} while (0)
    for (i = 0; (c->sequence_initialization & (UINT32_C(1) << 0)) &&
                i < PGW_CANCategoryStateSeq_get_length(&c->categories); ++i) {
        PGW_CANCategoryState *cat = PGW_CANCategoryStateSeq_get_reference(&c->categories, i);
        if (cat->queue_initialized) {
            if (cat->queue_borrowed && !PGW_CANSampleSeq_unloan(&cat->queue)) ok = false;
            if (!PGW_CANSampleSeq_finalize(&cat->queue)) ok = false;
            cat->queue_initialized = false;
            cat->queue_borrowed = false;
        }
        if (cat->loan_initialized) {
            if (cat->loan_borrowed && !PGW_CANLoanSeq_unloan(&cat->loan)) ok = false;
            if (!PGW_CANLoanSeq_finalize(&cat->loan)) ok = false;
            cat->loan_initialized = false;
            cat->loan_borrowed = false;
        }
    }
    RELEASE(PGW_CANCategoryStateSeq, c->categories, 0, 4);
    RELEASE(PGW_CANMessageSeq, c->messages, 1, 5);
    RELEASE(PGW_CANDecodedSeq, c->decode, 2, 6);
    RELEASE(PGW_CANIndexSeq, c->write_messages, 3, 7);
#undef RELEASE
    PGW_Status config_status = PGW_CANConfig_finalize(&c->config);
    return config_status == PGW_OK && ok ? PGW_OK : PGW_LOAN_ERROR;
}

static PGW_Status close_connection(PGW_Connection *connection)
{
    PGW_CANConnection *c = (PGW_CANConnection *)connection;
    RTI_INT32 i;
    if (!c) return PGW_INVALID;
    if (c->closed) return PGW_OK;
    for (i = 0; i < PGW_CANCategoryStateSeq_get_length(&c->categories); ++i)
        if (PGW_CANCategoryStateSeq_get_reference(&c->categories, i)->borrowed) {
            PGW_Counters_add(&c->counters, PGW_COUNT_LOAN_ERRORS, 1);
            return PGW_LOAN_ERROR;
        }
    c->closed = true;
    PGW_Status status = c->config.transport.iface->close ?
        c->config.transport.iface->close(c->config.transport.state) : PGW_OK;
    PGW_Status released = release_sequences(c);
    return status == PGW_OK ? released : status;
}

#if defined(PGW_ENABLE_REMOTE_CONTROL)
static PGW_Status control_apply(void *state, PGW_ControlAction action)
{
    if (!state) return PGW_INVALID;
    if (action == PGW_CONTROL_CONNECTION_UP ||
        action == PGW_CONTROL_CONNECTION_DOWN) {
        PGW_CANConnection *connection = state;
        if (connection->closed) return PGW_INVALID;
        bool desired = action == PGW_CONTROL_CONNECTION_UP;
        if (connection->control_enabled == desired) return PGW_NO_CHANGE;
        connection->control_enabled = desired;
        return PGW_OK;
    }
    if (action == PGW_CONTROL_INPUT_ENABLE ||
        action == PGW_CONTROL_INPUT_DISABLE ||
        action == PGW_CONTROL_OUTPUT_ENABLE ||
        action == PGW_CONTROL_OUTPUT_DISABLE) {
        PGW_CANCategoryState *category = state;
        if (!category->connection || category->connection->closed) return PGW_INVALID;
        bool input = action == PGW_CONTROL_INPUT_ENABLE ||
                     action == PGW_CONTROL_INPUT_DISABLE;
        bool desired = action == PGW_CONTROL_INPUT_ENABLE ||
                       action == PGW_CONTROL_OUTPUT_ENABLE;
        bool *enabled = input ? &category->input_enabled : &category->output_enabled;
        if (*enabled == desired) return PGW_NO_CHANGE;
        *enabled = desired;
        if (input && !desired) {
            category->head = 0;
            category->count = 0;
        }
        return PGW_OK;
    }
    return PGW_UNSUPPORTED;
}

static PGW_Status control_read_telemetry(void *state, const char *name,
                                         PGW_ControlScalar *out)
{
    if (!state || !name || !out || strcmp(name, "received_frames"))
        return PGW_UNSUPPORTED;
    PGW_CANStats stats;
    PGW_Status status = PGW_CAN_stats((PGW_Connection *)state, &stats);
    if (status != PGW_OK) return status;
    out->type = PGW_CONTROL_SCALAR_UINT64;
    out->value.uint64_value = stats.received_frames;
    return PGW_OK;
}

static const PGW_ControlAdapterI can_control = {
    PGW_CONTROL_ABI_VERSION,
    sizeof(PGW_ControlAdapterI),
    PGW_CAN_CONTROL_MANIFEST_VERSION,
    PGW_CAN_CONTROL_RESOURCE_KIND_MASK,
    PGW_CAN_CONTROL_ACTION_MASK,
    control_apply,
    PGW_CAN_CONTROL_TELEMETRY_METRIC_MASK,
    control_read_telemetry
};
#endif

static PGW_Status array(PGW_Arena *arena, size_t count, size_t size,
                        size_t alignment, void **out)
{
    size_t bytes;
    PGW_Status status;
    if (!PGW_size_multiply(count, size, &bytes)) return PGW_CAPACITY;
    status = PGW_Arena_allocate(arena, bytes, alignment, out);
    if (status == PGW_OK) memset(*out, 0, bytes);
    return status;
}

static bool storage_add(size_t *total, size_t count, size_t size, size_t alignment)
{
    size_t bytes;
    return PGW_size_multiply(count, size, &bytes) &&
           PGW_size_add(bytes, alignment - 1, &bytes) &&
           PGW_size_add(*total, bytes, total);
}

PGW_Status PGW_CAN_storage_size(const PGW_CANConfig *cfg, size_t *bytes)
{
    size_t total = 0;
    RTI_INT32 i;
    if (!cfg || !bytes || !cfg->categories_initialized || !cfg->categories_borrowed ||
        !cfg->mapping.initialized || !cfg->mapping.messages_borrowed ||
        !cfg->mapping.signals_borrowed || !PGW_CANCategorySeq_get_length(&cfg->categories) ||
        !PGW_CANMessageDefinitionSeq_get_length(&cfg->mapping.messages) ||
        !PGW_CANSignalDefinitionSeq_get_length(&cfg->mapping.signals) ||
        !cfg->write_capacity || cfg->write_capacity > (size_t)INT32_MAX) return PGW_INVALID;
#define ADD(count, type) do { \
    if (!storage_add(&total, count, sizeof(type), _Alignof(type))) \
        return PGW_CAPACITY; \
} while (0)
    ADD(1, PGW_CANConnection);
    ADD(PGW_CANCategorySeq_get_length(&cfg->categories), PGW_CANCategoryState);
    ADD(PGW_CANMessageDefinitionSeq_get_length(&cfg->mapping.messages), PGW_CANMessage);
    ADD(PGW_CANSignalDefinitionSeq_get_length(&cfg->mapping.signals), PGW_Signal);
    ADD(cfg->write_capacity, size_t);
    for (i = 0; i < PGW_CANCategorySeq_get_length(&cfg->categories); ++i) {
        size_t capacity = PGW_CANCategorySeq_get_reference(&cfg->categories, i)->capacity;
        if (!capacity || capacity > (size_t)INT32_MAX) return PGW_INVALID;
        ADD(capacity, PGW_CANSample);
        ADD(capacity, PGW_CANSample);
    }
#undef ADD
    *bytes = total;
    return PGW_OK;
}

static PGW_Status create(const void *configuration, PGW_Arena *arena,
                         PGW_Connection **out)
{
    const PGW_CANConfig *cfg = configuration;
    PGW_CANConnection *c = NULL;
    size_t start;
    RTI_INT32 category_count, message_count, signal_count;
    RTI_INT32 i, j;
    PGW_Status status = PGW_INVALID;
    if (!cfg || !arena || !out || !cfg->transport.state ||
        !cfg->transport.iface || !cfg->transport.iface->receive ||
        !cfg->transport.iface->send ||
        !cfg->mapping.initialized || !cfg->mapping.messages_borrowed ||
        !cfg->mapping.signals_borrowed ||
        !cfg->mapping.decode || !cfg->mapping.patch ||
        !cfg->categories_initialized || !cfg->categories_borrowed ||
        !cfg->receive_budget || !cfg->write_capacity) return PGW_INVALID;
    category_count = PGW_CANCategorySeq_get_length(&cfg->categories);
    message_count = PGW_CANMessageDefinitionSeq_get_length(&cfg->mapping.messages);
    signal_count = PGW_CANSignalDefinitionSeq_get_length(&cfg->mapping.signals);
    if (!category_count || !message_count || !signal_count) return PGW_INVALID;
    if (cfg->write_capacity > (size_t)INT32_MAX) return PGW_INVALID;
    for (i = 0; i < category_count; ++i) {
        const PGW_CANCategory *category =
            PGW_CANCategorySeq_get_reference(&cfg->categories, i);
        if (!category->name || !category->capacity ||
            !category->schema || !category->schema->name ||
            !category->schema->fingerprint) return PGW_INVALID;
        for (j = 0; j < i; ++j)
            if (!strcmp(category->name,
                PGW_CANCategorySeq_get_reference(&cfg->categories, j)->name))
                return PGW_INVALID;
    }
    for (i = 0; i < message_count; ++i) {
        const PGW_MessageDescriptor *d =
            PGW_CANMessageDefinitionSeq_get_reference(&cfg->mapping.messages, i);
        PGW_CANFrame f = {0};
        f.id = d->frame_id; f.length = d->length;
        f.flags = (d->fd ? PGW_CAN_FLAG_FD : 0) |
                  (d->extended ? PGW_CAN_FLAG_EXTENDED : 0);
        if (!PGW_CANFrame_valid(&f)) return PGW_INVALID;
        for (j = 0; j < i; ++j) {
            const PGW_MessageDescriptor *p =
                PGW_CANMessageDefinitionSeq_get_reference(&cfg->mapping.messages, j);
            if (p->frame_id == d->frame_id && p->extended == d->extended &&
                p->fd == d->fd) return PGW_INVALID;
        }
    }
    for (i = 0; i < signal_count; ++i) {
        const PGW_SignalDescriptor *d =
            PGW_CANSignalDefinitionSeq_get_reference(&cfg->mapping.signals, i);
        if (d->message_index >= (size_t)message_count || !d->category ||
            d->kind < PGW_VALUE_BOOLEAN || d->kind > PGW_VALUE_DOUBLE)
            return PGW_INVALID;
        for (j = 0; j < category_count; ++j)
            if (!strcmp(d->category,
                PGW_CANCategorySeq_get_reference(&cfg->categories, j)->name)) break;
        if (j == category_count) return PGW_INVALID;
        for (j = 0; j < i; ++j)
            if (PGW_CANSignalDefinitionSeq_get_reference(
                    &cfg->mapping.signals, j)->id == d->id) return PGW_INVALID;
    }
    start = arena->used;
#define ALLOC(member, count, type) do { \
    void *allocated; \
    status = array(arena, count, sizeof(type), _Alignof(type), &allocated); \
    if (status != PGW_OK) goto fail; \
    member = allocated; \
} while (0)
#define ALLOC_SEQ(Name, member, count, length, type, id) do { \
    type *buffer; \
    ALLOC(buffer, count, type); \
    if (!Name##_initialize(&(member))) { status = PGW_FATAL; goto fail; } \
    c->sequence_initialization |= UINT32_C(1) << (id); \
    if (!Name##_loan_contiguous(&(member), buffer, length, count)) { status = PGW_CAPACITY; goto fail; } \
    c->sequence_initialization |= UINT32_C(1) << ((id) + 4); \
} while (0)
    ALLOC(c, 1, PGW_CANConnection);
    c->control_enabled = true;
    c->config.entity_id = cfg->entity_id;
    c->config.transport = cfg->transport;
    c->config.receive_budget = cfg->receive_budget;
    c->config.write_capacity = cfg->write_capacity;
    c->config.diagnostics = cfg->diagnostics;
    c->config.disable_metadata_capture = cfg->disable_metadata_capture;
    status = PGW_CANMapping_initialize(&c->config.mapping,
        PGW_CANMessageDefinitionSeq_get_contiguous_buffer(&cfg->mapping.messages), message_count,
        PGW_CANSignalDefinitionSeq_get_contiguous_buffer(&cfg->mapping.signals), signal_count,
        cfg->mapping.context, cfg->mapping.decode, cfg->mapping.patch);
    if (status != PGW_OK) goto fail;
    status = PGW_CANConfig_set_categories(&c->config, &cfg->categories);
    if (status != PGW_OK) goto fail;
    ALLOC_SEQ(PGW_CANCategoryStateSeq, c->categories,
        (size_t)category_count, category_count, PGW_CANCategoryState, 0);
    ALLOC_SEQ(PGW_CANMessageSeq, c->messages, (size_t)message_count, message_count, PGW_CANMessage, 1);
    ALLOC_SEQ(PGW_CANDecodedSeq, c->decode, (size_t)signal_count, 0, PGW_Signal, 2);
    ALLOC_SEQ(PGW_CANIndexSeq, c->write_messages, cfg->write_capacity, 0, size_t, 3);
    for (i = 0; i < (RTI_INT32)category_count; ++i) {
        PGW_CANCategoryState *cat = PGW_CANCategoryStateSeq_get_reference(&c->categories, i);
        const PGW_CANCategory *category = PGW_CANCategorySeq_get_reference(&cfg->categories, i);
        cat->connection = c; cat->name = category->name;
        cat->input_enabled = true;
        cat->output_enabled = true;
        cat->view_contract = (PGW_SampleViewDescriptor){
            PGW_SAMPLE_VIEW_CANONICAL, sizeof(PGW_Signal),
            &PGW_CAN_SIGNAL_VALUE_IDENTITY, &PGW_CAN_METADATA_IDENTITY
        };
        cat->representation = (PGW_Representation){
            category->schema, "can.signal", sizeof(PGW_CANSample),
            _Alignof(PGW_CANSample), &sample_access, &cat->view_contract};
        PGW_CANSample *queue_buffer;
        if (!PGW_CANSampleSeq_initialize(&cat->queue)) { status = PGW_FATAL; goto fail; }
        cat->queue_initialized = true;
        ALLOC(queue_buffer, category->capacity, PGW_CANSample);
        if (!PGW_CANSampleSeq_loan_contiguous(&cat->queue, queue_buffer, 0,
                                               (RTI_INT32)category->capacity)) {
            status = PGW_CAPACITY; goto fail;
        }
        cat->queue_borrowed = true;
        if (!PGW_CANSampleSeq_set_length(&cat->queue, (RTI_INT32)category->capacity)) {
            status = PGW_CAPACITY; goto fail;
        }
        if (!PGW_CANLoanSeq_initialize(&cat->loan)) { status = PGW_FATAL; goto fail; }
        cat->loan_initialized = true;
        PGW_CANSample *loan_buffer;
        ALLOC(loan_buffer, category->capacity, PGW_CANSample);
        if (!PGW_CANLoanSeq_loan_contiguous(&cat->loan, loan_buffer, 0,
                                            (RTI_INT32)category->capacity)) {
            status = PGW_CAPACITY; goto fail;
        }
        cat->loan_borrowed = true;
    }
#undef ALLOC_SEQ
#undef ALLOC
    if (!PGW_Counters_initialize(&c->counters)) {
        status = PGW_UNSUPPORTED;
        goto fail;
    }
    c->stats.mutable_bytes = arena->used - start;
    *out = (PGW_Connection *)c;
    return PGW_OK;
fail:
    if (c) release_sequences(c);
    arena->used = start;
    return status;
}

PGW_Status PGW_CAN_stats(const PGW_Connection *c, PGW_CANStats *stats)
{
    if (!c || !stats) return PGW_INVALID;
    *stats = ((const PGW_CANConnection *)c)->stats;
    return PGW_OK;
}

PGW_Status PGW_CAN_baseline_timestamp(const PGW_Connection *connection,
                                     size_t message, PGW_Timestamp *out)
{
    const PGW_CANConnection *c = (const PGW_CANConnection *)connection;
    if (!c || !out || c->closed || message > (size_t)INT32_MAX ||
        (RTI_INT32)message >= PGW_CANMessageSeq_get_length(&c->messages))
        return PGW_INVALID;
    const PGW_CANMessage *baseline = PGW_CANMessageSeq_get_reference(
        &c->messages, (RTI_INT32)message);
    if (!baseline->baseline) return PGW_NO_DATA;
    *out = baseline->received.timestamp;
    return out->valid ? PGW_OK : PGW_NO_DATA;
}

const PGW_Counters *PGW_CAN_counters(const PGW_Connection *c)
{
    return c ? &((const PGW_CANConnection *)c)->counters : NULL;
}

static const PGW_ConnectionI connection_iface = {
    PGW_ABI_VERSION, sizeof(PGW_ConnectionI),
    get_reader, get_writer, close_connection
};
const PGW_AdapterI PGW_CANAdapter = {
    .version = PGW_ABI_VERSION,
    .size = sizeof(PGW_AdapterI),
    .name = "can",
    .create = create,
    .connection = &connection_iface,
#if defined(PGW_ENABLE_REMOTE_CONTROL)
    .control = &can_control
#endif
};

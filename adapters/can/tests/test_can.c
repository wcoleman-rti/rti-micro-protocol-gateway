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

#include "pgw/atomic.h"
#include "pgw/can_memory.h"
#include "pgw/runtime.h"
#include "pgw_codec.h"
#include "osapi/osapi_system.h"
#include "osapi/osapi_thread.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef PGW_CAN_WRAP_ALLOCATIONS
static bool frozen;
static size_t allocations;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__real_aligned_alloc(size_t, size_t);
void *__wrap_malloc(size_t n)
{ if (frozen) ++allocations; return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t s)
{ if (frozen) ++allocations; return __real_calloc(n, s); }
void *__wrap_realloc(void *p, size_t n)
{ if (frozen) ++allocations; return __real_realloc(p, n); }
void *__wrap_aligned_alloc(size_t a, size_t n)
{ if (frozen) ++allocations; return __real_aligned_alloc(a, n); }
#endif

static PGW_CodecStatus decode(void *context, size_t index, const uint8_t *data,
                             size_t size, PGW_Signal *out, size_t capacity,
                             size_t *count)
{
    const PGW_MessageDescriptor *m = &PGW_codec_messages[index];
    (void)context;
    return PGW_codec_decode(m->frame_id, m->extended, m->fd, data, size,
                            out, capacity, count);
}

static PGW_CodecStatus patch(void *context, uint32_t id, const PGW_Value *value,
                            uint8_t *data, size_t size)
{
    PGW_Signal s = {id, *value};
    (void)context;
    return PGW_codec_patch(&s, data, size);
}

static PGW_Status source_copy(const PGW_Sample *s, void *out, size_t size)
{
    if (!s || size != sizeof(PGW_Signal)) return PGW_INVALID;
    memcpy(out, s, size);
    return PGW_OK;
}

static PGW_Status canonical_source_view(const PGW_Sample *s, PGW_SampleView *view)
{
    if (!s || !view) return PGW_INVALID;
    *view = (PGW_SampleView){
        PGW_SAMPLE_VIEW_CANONICAL, s, sizeof(PGW_Signal),
        &PGW_CAN_SIGNAL_VALUE_IDENTITY, NULL, NULL
    };
    return PGW_OK;
}

static const unsigned char native_source_type_identity = 0;

static PGW_Status native_source_view(const PGW_Sample *s, PGW_SampleView *view)
{
    if (!s || !view) return PGW_INVALID;
    *view = (PGW_SampleView){
        PGW_SAMPLE_VIEW_NATIVE, s, sizeof(uint32_t),
        &native_source_type_identity, NULL, NULL
    };
    return PGW_OK;
}

static const PGW_SampleAccessI native_view_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), NULL, NULL, native_source_view
};
static const PGW_SampleViewDescriptor native_view_contract = {
    PGW_SAMPLE_VIEW_NATIVE, sizeof(uint32_t), &native_source_type_identity, NULL
};
static const PGW_SampleAccessI canonical_view_access = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), NULL, NULL, canonical_source_view
};
static const PGW_SampleViewDescriptor canonical_view_contract = {
    PGW_SAMPLE_VIEW_CANONICAL, sizeof(PGW_Signal),
    &PGW_CAN_SIGNAL_VALUE_IDENTITY, NULL
};

static void commands(PGW_SampleSeq *seq, PGW_WriteResultSeq *outcomes,
                     PGW_Signal *values, size_t n)
{
    assert(PGW_SampleSeq_set_length(seq, n));
    assert(PGW_WriteResultSeq_set_length(outcomes, n));
    for (size_t i = 0; i < n; ++i)
        *PGW_SampleSeq_get_reference(seq, (RTI_INT32)i) = (PGW_SampleRef)&values[i];
}

static PGW_Signal get_signal(PGW_StreamReader *reader, PGW_SampleSeq *seq,
                             uint32_t id)
{
    PGW_Signal s = {0};
    for (RTI_INT32 i = 0; i < PGW_SampleSeq_get_length(seq); ++i) {
        assert(reader->representation->access->copy_value(
            *PGW_SampleSeq_get_reference(seq, i), &s, sizeof(s)) == PGW_OK);
        if (s.id == id) return s;
    }
    assert(!"signal absent");
    return s;
}

typedef struct {
    PGW_ATOMIC(size_t) calls;
} NotificationCounter;

static void reader_available(void *context)
{
    NotificationCounter *counter = context;
    PGW_ATOMIC_ADD(&counter->calls, 1,
                   OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE_RELEASE);
}

static void wait_for_notifications(const NotificationCounter *counter,
                                   size_t expected)
{
    for (size_t i = 0; i < 2000; ++i) {
        if (PGW_ATOMIC_LOAD(&counter->calls, OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) >=
            expected) return;
        OSAPI_Thread_sleep(1);
    }
    assert(!"timed out waiting for reader notification");
}

static void wait_for_received(PGW_Connection *connection, uint64_t expected)
{
    for (size_t i = 0; i < 2000; ++i) {
        PGW_CANStats stats;
        assert(PGW_CAN_stats(connection, &stats) == PGW_OK);
        if (stats.received_frames >= expected) return;
        OSAPI_Thread_sleep(1);
    }
    assert(!"timed out waiting for CAN receiver");
}

static void wait_for_io_errors(PGW_Connection *connection, uint64_t expected)
{
    for (size_t i = 0; i < 2000; ++i) {
        PGW_CANStats stats;
        assert(PGW_CAN_stats(connection, &stats) == PGW_OK);
        if (stats.io_errors >= expected) return;
        OSAPI_Thread_sleep(1);
    }
    assert(!"timed out waiting for transport error");
}

static void partial_messages(const PGW_TypeInfo *schema,
                             const PGW_SampleRepresentation *source)
{
    void *storage = malloc(16384);
    assert(storage);
    PGW_Arena arena = {storage, 16384, 0};
    PGW_CANMemory memory;
    PGW_CANFrame rx[2], tx[1], engine = {0}, aux = {0}, sent;
    PGW_SignalDescriptor descriptors[8];
    PGW_CANCategory category = {"all", 7, schema};
    PGW_CANCategorySeq category_sequence;
    PGW_CANConfig cfg = {0};
    PGW_Connection *connection;
    PGW_StreamWriter writer;
    PGW_StreamReader reader;
    NotificationCounter notifications;
    PGW_ReaderListener listener = {reader_available, &notifications};
    PGW_SampleSeq input, loan;
    PGW_SampleRef refs[2], loan_refs[7];
    PGW_WriteResult results[2];
    PGW_WriteResultSeq outcomes;
    PGW_Signal values[2] = {
        {1001, {PGW_VALUE_DOUBLE, {.real = 12.3}}},
        {2004, {PGW_VALUE_INT64, {.integer = 42}}}
    };
    assert(PGW_codec_signal_count == 8);
    memcpy(descriptors, PGW_codec_signals, sizeof(descriptors));
    for (size_t i = 0; i < 8; ++i) descriptors[i].category = "all";
    assert(PGW_CANMemory_initialize(&memory, rx, 2, tx, 1) == PGW_OK);
    assert(PGW_CANCategorySeq_initialize(&category_sequence));
    assert(PGW_CANCategorySeq_loan_contiguous(&category_sequence, &category, 1, 1));
    cfg.transport = PGW_CANMemory_transport(&memory);
    assert(PGW_CANMapping_initialize(&cfg.mapping,
        PGW_codec_messages, PGW_codec_message_count, descriptors, 8,
        NULL, decode, patch) == PGW_OK);
    assert(PGW_CANConfig_set_categories(&cfg, &category_sequence) == PGW_OK);
    cfg.receive_budget = 2; cfg.write_capacity = 2;
    cfg.disable_metadata_capture = true;
    assert(PGW_CANAdapter.create(&cfg, &arena, &connection) == PGW_OK);
    assert(PGW_CANAdapter.connection->lookup_stream_writer(connection, "all", &writer) == PGW_OK);
    PGW_SampleRepresentation native_source = {
        source->schema, "test.native_signal", sizeof(uint32_t),
        _Alignof(uint32_t), &native_view_access, &native_view_contract
    };
    assert(writer.iface->bind(writer.state, &native_source) == PGW_UNSUPPORTED);
    assert(writer.iface->bind(writer.state, source) == PGW_OK);
    assert(PGW_CANAdapter.connection->lookup_stream_reader(connection, "all", &reader) == PGW_OK);
    PGW_ATOMIC_INIT(&notifications.calls, 0);
    assert(reader.iface->register_listener(reader.state, &listener) == PGW_OK);
    assert(PGW_SampleSeq_initialize(&input));
    assert(PGW_SampleSeq_initialize(&loan));
    assert(PGW_SampleSeq_loan_contiguous(&input, refs, 0, 2));
    assert(PGW_SampleSeq_loan_contiguous(&loan, loan_refs, 0, 7));
    assert(PGW_WriteResultSeq_initialize(&outcomes));
    assert(PGW_WriteResultSeq_loan_contiguous(&outcomes, results, 0, 2));
    engine.id = 0x100; engine.length = 8;
    engine.timestamp = (PGW_Timestamp){true, true, 42, 123};
    aux.id = 0x123; aux.length = 16;
    aux.flags = PGW_CAN_FLAG_EXTENDED | PGW_CAN_FLAG_FD; aux.data[0] = 1;
    aux.timestamp = engine.timestamp;
#ifdef PGW_CAN_WRAP_ALLOCATIONS
    allocations = 0;
    frozen = true;
#endif
    assert(PGW_CANMemory_inject(&memory, &engine) == PGW_OK);
    assert(PGW_CANMemory_inject(&memory, &aux) == PGW_OK);
    wait_for_received(connection, 2);
    wait_for_notifications(&notifications, 2);
    assert(reader.iface->read(reader.state, &loan, 7) == PGW_OK);
    assert(PGW_SampleSeq_get_length(&loan) == 7);
    for (size_t i = 0; i < 7; ++i) {
        PGW_Timestamp timestamp;
        PGW_Signal signal;
        assert(reader.representation->access->copy_value(
            *PGW_SampleSeq_get_reference(&loan, (RTI_INT32)i), &signal, sizeof(signal)) == PGW_OK);
        assert(reader.representation->access->source_timestamp(
            *PGW_SampleSeq_get_reference(&loan, (RTI_INT32)i), &timestamp) == PGW_NO_DATA);
        assert(!timestamp.valid);
    }
    assert(get_signal(&reader, &loan, 1001).value.data.real == 0.0);
    assert(get_signal(&reader, &loan, 2001).value.data.integer == 1);
    assert(reader.iface->return_loan(reader.state, &loan) == PGW_OK);
    commands(&input, &outcomes, values, 2);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(results[0] == PGW_WRITE_ACCEPTED &&
           results[1] == PGW_WRITE_BACKPRESSURE);
    assert(PGW_CANMemory_take_sent(&memory, &sent) == PGW_OK);
    assert(sent.id == 0x100 && sent.data[0] == 123 && sent.data[1] == 0);
    PGW_Timestamp baseline_timestamp;
    assert(PGW_CAN_baseline_timestamp(connection, 0, &baseline_timestamp) == PGW_OK);
    assert(baseline_timestamp.valid && baseline_timestamp.seconds == 42 &&
           baseline_timestamp.nanoseconds == 123);
    values[0] = (PGW_Signal){2002, {PGW_VALUE_DOUBLE, {.real = -39.0}}};
    commands(&input, &outcomes, values, 1);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(results[0] == PGW_WRITE_ACCEPTED);
    assert(PGW_CANMemory_take_sent(&memory, &sent) == PGW_OK);
    assert(sent.id == 0x123 && sent.data[1] == 4 &&
           sent.data[8] == 0 && sent.data[9] == 0);
    assert(reader.iface->unregister_listener(reader.state, &listener) == PGW_OK);
    assert(PGW_CANAdapter.connection->close(connection) == PGW_OK);
    assert(PGW_SampleSeq_unloan(&input));
    assert(PGW_SampleSeq_unloan(&loan));
    assert(PGW_SampleSeq_finalize(&input));
    assert(PGW_SampleSeq_finalize(&loan));
    assert(PGW_WriteResultSeq_unloan(&outcomes));
    assert(PGW_WriteResultSeq_finalize(&outcomes));
    assert(PGW_CANConfig_finalize(&cfg) == PGW_OK);
    assert(PGW_CANCategorySeq_unloan(&category_sequence));
    assert(PGW_CANCategorySeq_finalize(&category_sequence));
#ifdef PGW_CAN_WRAP_ALLOCATIONS
    assert(allocations == 0);
    frozen = false;
#endif
    free(storage);
}

int main(void)
{
    assert(PGW_Runtime_initialize());
    void *storage = malloc(32768);
    assert(storage);
    PGW_Arena arena = {storage, 32768, 0};
    PGW_CANMemory memory;
    PGW_CANFrame rx[16], tx[1], sent;
    PGW_TypeInfo schema = {PGW_codec_schema.name, PGW_codec_schema.version,
                         PGW_codec_schema.fingerprint};
    PGW_CANCategory categories[] = {
        {"powertrain", 4, &schema}, {"auxiliary", 3, &schema}};
    PGW_CANCategorySeq category_sequence;
    PGW_CANConfig cfg = {0};
    PGW_Connection *connection = NULL;
    PGW_StreamReader reader, auxiliary;
    NotificationCounter reader_notifications, auxiliary_notifications;
    PGW_ReaderListener reader_listener = {
        reader_available, &reader_notifications
    };
    PGW_ReaderListener auxiliary_listener = {
        reader_available, &auxiliary_notifications
    };
    PGW_StreamWriter writer, aux_writer;
    PGW_SampleSeq input, loan, external_loan;
    PGW_SampleRef refs[16], loan_refs[4], external_refs[2];
    PGW_WriteResult results[16];
    PGW_WriteResultSeq outcomes;
    PGW_Signal values[16];
    PGW_CANStats stats;
    static const PGW_SampleAccessI source_access = {
        PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), source_copy, NULL, NULL};
    PGW_SampleRepresentation source = {&schema, "test.signal", sizeof(PGW_Signal),
                                  _Alignof(PGW_Signal), &source_access, NULL};
    PGW_SampleRepresentation canonical_view_source = {
        &schema, "test.signal.view", sizeof(PGW_Signal), _Alignof(PGW_Signal),
        &canonical_view_access, &canonical_view_contract
    };
    PGW_CANFrame engine = {0}, aux = {0}, bad = {0};
    const uint8_t baseline[] = {0xe8, 0x03, 0xff, 0x0a, 0xa5, 0x02, 0xcc, 0xdd};
    const uint8_t patched[] = {0xd2, 0x04, 0xff, 0xea, 0xa4, 0x02, 0xcc, 0xdd};
    partial_messages(&schema, &canonical_view_source);
    assert(PGW_CANMemory_initialize(&memory, rx, 16, tx, 1) == PGW_OK);
    cfg.transport = PGW_CANMemory_transport(&memory);
    assert(PGW_CANMapping_initialize(&cfg.mapping,
        PGW_codec_messages, PGW_codec_message_count,
        PGW_codec_signals, PGW_codec_signal_count,
        NULL, decode, patch) == PGW_OK);
    assert(PGW_CANCategorySeq_initialize(&category_sequence));
    assert(PGW_CANCategorySeq_loan_contiguous(&category_sequence, categories, 2, 2));
    assert(PGW_CANConfig_set_categories(&cfg, &category_sequence) == PGW_OK);
    cfg.receive_budget = 1; cfg.write_capacity = 16;
    size_t required;
    assert(PGW_CAN_storage_size(&cfg, &required) == PGW_OK);
    assert(required < 32768);
    PGW_Arena short_arena = {storage, 1, 0};
    assert(PGW_CANAdapter.create(&cfg, &short_arena, &connection) == PGW_CAPACITY);
    assert(short_arena.used == 0 && connection == NULL);
    assert(PGW_CANAdapter.create(&cfg, &arena, &connection) == PGW_OK);
    assert(arena.used <= required);
    assert(PGW_CANAdapter.connection->lookup_stream_reader(connection, "powertrain", &reader) == PGW_OK);
    assert(PGW_CANAdapter.connection->lookup_stream_reader(connection, "auxiliary", &auxiliary) == PGW_OK);
    PGW_ATOMIC_INIT(&reader_notifications.calls, 0);
    PGW_ATOMIC_INIT(&auxiliary_notifications.calls, 0);
    assert(reader.iface->register_listener(reader.state, &reader_listener) == PGW_OK);
    assert(auxiliary.iface->register_listener(
        auxiliary.state, &auxiliary_listener) == PGW_OK);
    assert(PGW_CANAdapter.connection->lookup_stream_writer(connection, "powertrain", &writer) == PGW_OK);
    assert(PGW_CANAdapter.connection->lookup_stream_writer(connection, "auxiliary", &aux_writer) == PGW_OK);
    assert(writer.iface->bind(writer.state, &source) == PGW_OK);
    assert(aux_writer.iface->bind(aux_writer.state, &source) == PGW_OK);
    PGW_TypeInfo wrong_schema = {"pgw.signal", 1, "wrong-fingerprint"};
    PGW_SampleRepresentation wrong = source; wrong.schema = &wrong_schema;
    assert(writer.iface->bind(writer.state, &wrong) == PGW_UNSUPPORTED);
    assert(PGW_SampleSeq_initialize(&input));
    assert(PGW_SampleSeq_initialize(&loan));
    assert(PGW_SampleSeq_initialize(&external_loan));
    assert(PGW_SampleSeq_loan_contiguous(&input, refs, 0, 16));
    assert(PGW_SampleSeq_loan_contiguous(&loan, loan_refs, 0, 4));
    assert(PGW_SampleSeq_loan_contiguous(&external_loan, external_refs, 0, 2));
    assert(PGW_WriteResultSeq_initialize(&outcomes));
    assert(PGW_WriteResultSeq_loan_contiguous(&outcomes, results, 0, 16));
#ifdef PGW_CAN_WRAP_ALLOCATIONS
    /* Validate that link wrapping is active before asserting runtime coverage. */
    void *(*volatile malloc_fn)(size_t) = malloc;
    void *(*volatile calloc_fn)(size_t, size_t) = calloc;
    void *(*volatile realloc_fn)(void *, size_t) = realloc;
    void *(*volatile aligned_fn)(size_t, size_t) = aligned_alloc;
    frozen = true;
    void *probe = malloc_fn(1);
    assert(probe);
    probe = realloc_fn(probe, 2);
    assert(probe);
    free(probe);
    probe = calloc_fn(1, 1);
    assert(probe);
    free(probe);
    probe = aligned_fn(16, 16);
    assert(probe);
    free(probe);
    assert(allocations == 4);
    allocations = 0;
#endif
    assert((size_t)PGW_CANMessageDefinitionSeq_get_length(&cfg.mapping.messages) ==
           PGW_codec_message_count);
    assert(PGW_CANSignalDefinitionSeq_get_contiguous_buffer(&cfg.mapping.signals) ==
           PGW_codec_signals);
    assert(PGW_CANCategorySeq_get_length(&cfg.categories) == 2);
    assert(!PGW_CANMessageDefinitionSeq_set_length(&cfg.mapping.messages,
        PGW_CANMessageDefinitionSeq_get_maximum(&cfg.mapping.messages) + 1));
    assert(!PGW_CANSignalDefinitionSeq_set_length(&cfg.mapping.signals,
        PGW_CANSignalDefinitionSeq_get_maximum(&cfg.mapping.signals) + 1));
    assert(!PGW_CANCategorySeq_set_length(&cfg.categories, 3));
    assert(PGW_CANCategorySeq_get_reference(&cfg.categories, 2) == NULL);
    values[0] = (PGW_Signal){1001, {PGW_VALUE_DOUBLE, {.real = 123.4}}};
    commands(&input, &outcomes, values, 1);
    assert(PGW_WriteResultSeq_set_length(&outcomes, 0));
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_INVALID);
    assert(PGW_WriteResultSeq_get_length(&outcomes) == 0 && memory.tx_count == 0);
    assert(PGW_WriteResultSeq_set_length(&outcomes, 1));
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(results[0] == PGW_WRITE_INVALID); /* CAN-BASELINE */
    engine.id = 0x100; engine.length = 8;
    memcpy(engine.data, baseline, sizeof(baseline));
    engine.timestamp = (PGW_Timestamp){true, true, 42, 123};
    aux.id = 0x123; aux.flags = PGW_CAN_FLAG_EXTENDED | PGW_CAN_FLAG_FD;
    aux.length = 16; aux.data[0] = 1; aux.data[1] = 0x04;
    aux.data[8] = 0x34; aux.data[9] = 0x12;
    bad = engine; bad.id = 0x700;
    assert(PGW_CANMemory_inject(&memory, &bad) == PGW_OK);
    assert(PGW_CANMemory_inject(&memory, &engine) == PGW_OK);
    wait_for_received(connection, 2);
    wait_for_notifications(&reader_notifications, 1);
    assert(PGW_CAN_stats(connection, &stats) == PGW_OK);
    assert(stats.unknown_frames == 1); /* unknown frames consume receiver budget */
    PGW_Signal decoded_signals[4];
    assert(reader.iface->read(reader.state, &loan, 2) == PGW_OK);
    assert(PGW_SampleSeq_get_length(&loan) == 2);
    for (size_t i = 0; i < 2; ++i)
        assert(reader.representation->access->copy_value(
            *PGW_SampleSeq_get_reference(&loan, (RTI_INT32)i),
            &decoded_signals[i], sizeof(PGW_Signal)) == PGW_OK);
    PGW_Timestamp timestamp;
    assert(reader.representation->access->source_timestamp(
        *PGW_SampleSeq_get_reference(&loan, 0), &timestamp) == PGW_OK);
    assert(timestamp.seconds == 42 && timestamp.nanoseconds == 123);
    assert(reader.iface->read(reader.state, &external_loan, 2) == PGW_LOAN_ERROR);
    assert(PGW_CANAdapter.connection->close(connection) == PGW_LOAN_ERROR);
    assert(reader.iface->return_loan(reader.state, &loan) == PGW_OK);
    assert(PGW_SampleSeq_get_contiguous_buffer(&loan) != NULL &&
           PGW_SampleSeq_get_length(&loan) == 0);
    assert(reader.iface->read(reader.state, &loan, 2) == PGW_OK);
    assert(PGW_SampleSeq_get_length(&loan) == 2);
    for (size_t i = 0; i < 2; ++i)
        assert(reader.representation->access->copy_value(
            *PGW_SampleSeq_get_reference(&loan, (RTI_INT32)i),
            &decoded_signals[i + 2], sizeof(PGW_Signal)) == PGW_OK);
    bool seen[4] = {false};
    PGW_Signal by_id[4] = {0};
    for (size_t i = 0; i < 4; ++i) {
        assert(decoded_signals[i].id >= 1001 &&
               decoded_signals[i].id <= 1004);
        size_t index = decoded_signals[i].id - 1001;
        seen[index] = true;
        by_id[index] = decoded_signals[i];
    }
    assert(seen[0] && seen[1] && seen[2] && seen[3]);
    assert(by_id[0].value.data.real == 100.0);
    assert(by_id[1].value.data.integer == -16);
    assert(by_id[2].value.data.boolean);
    assert(by_id[3].value.data.integer == 2);
    assert(reader.iface->return_loan(reader.state, &loan) == PGW_OK);
    assert(PGW_ATOMIC_LOAD(&reader_notifications.calls,
                                OSAPI_ATOMIC_MEMORY_ORDER_ACQUIRE) >= 2);
    assert(reader.iface->return_loan(reader.state, &loan) == PGW_LOAN_ERROR);
    values[1] = (PGW_Signal){1002, {PGW_VALUE_INT64, {.integer = -2}}};
    values[2] = (PGW_Signal){1003, {PGW_VALUE_BOOLEAN, {.boolean = false}}};
    values[3] = (PGW_Signal){9999, {PGW_VALUE_INT64, {.integer = 1}}};
    commands(&input, &outcomes, values, 4);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(results[0] == PGW_WRITE_ACCEPTED && results[1] == PGW_WRITE_ACCEPTED &&
           results[2] == PGW_WRITE_ACCEPTED && results[3] == PGW_WRITE_INVALID);
    assert(memory.tx_count == 1); /* CAN-COALESCE */
    assert(PGW_CANMemory_take_sent(&memory, &sent) == PGW_OK);
    assert(!memcmp(sent.data, patched, sizeof(patched))); /* CAN-GOLDEN */
    assert(!sent.timestamp.valid);
    assert(PGW_CANMemory_inject(&memory, &aux) == PGW_OK);
    wait_for_received(connection, 3);
    wait_for_notifications(&auxiliary_notifications, 1);
    assert(auxiliary.iface->read(auxiliary.state, &loan, 4) == PGW_OK);
    assert(PGW_SampleSeq_get_length(&loan) == 3); /* inactive Pressure absent */
    assert(get_signal(&auxiliary, &loan, 2002).value.data.real == -39.0);
    assert(get_signal(&auxiliary, &loan, 2004).value.data.integer == 0x1234);
    assert(auxiliary.iface->return_loan(auxiliary.state, &loan) == PGW_OK);
    values[0] = (PGW_Signal){2003, {PGW_VALUE_INT64, {.integer = 12}}};
    values[1] = (PGW_Signal){2001, {PGW_VALUE_INT64, {.integer = 2}}};
    values[2] = (PGW_Signal){2002, {PGW_VALUE_DOUBLE, {.real = -38.0}}};
    commands(&input, &outcomes, values, 3);
    assert(aux_writer.iface->write(aux_writer.state, &input, &outcomes) == PGW_OK);
    assert(results[0] == PGW_WRITE_INVALID && results[1] == PGW_WRITE_INVALID &&
           results[2] == PGW_WRITE_ACCEPTED); /* CAN-MUX */
    assert(PGW_CANMemory_take_sent(&memory, &sent) == PGW_OK);
    assert(sent.id == 0x123 && sent.length == 16 &&
           sent.flags == (PGW_CAN_FLAG_EXTENDED | PGW_CAN_FLAG_FD));
    assert(sent.data[0] == 1 && sent.data[1] == 8 && sent.data[8] == 0x34);
    values[0] = (PGW_Signal){1004, {PGW_VALUE_INT64, {.integer = 3}}};
    commands(&input, &outcomes, values, 1);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(results[0] == PGW_WRITE_ACCEPTED);
    values[0] = (PGW_Signal){1001, {PGW_VALUE_DOUBLE, {.real = 999.0}}};
    commands(&input, &outcomes, values, 1);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(results[0] == PGW_WRITE_BACKPRESSURE);
    assert(PGW_CANMemory_take_sent(&memory, &sent) == PGW_OK);
    assert(sent.data[5] == 3);
    values[0] = (PGW_Signal){1003, {PGW_VALUE_BOOLEAN, {.boolean = true}}};
    commands(&input, &outcomes, values, 1);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(PGW_CANMemory_take_sent(&memory, &sent) == PGW_OK);
    assert(sent.data[0] == 0xd2 && sent.data[1] == 4 &&
           sent.data[4] == 0xa5 && sent.data[5] == 3); /* no dirty retry */
    values[0] = (PGW_Signal){1001, {PGW_VALUE_DOUBLE, {.real = NAN}}};
    values[1] = (PGW_Signal){1001, {PGW_VALUE_DOUBLE, {.real = INFINITY}}};
    values[2] = (PGW_Signal){1001, {PGW_VALUE_INT64, {.integer = 5}}};
    values[3] = (PGW_Signal){1004, {PGW_VALUE_INT64, {.integer = 4}}};
    values[4] = (PGW_Signal){1002, {PGW_VALUE_INT64, {.integer = 2048}}};
    commands(&input, &outcomes, values, 5);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    for (size_t i = 0; i < 5; ++i) assert(results[i] == PGW_WRITE_INVALID);
    assert(memory.tx_count == 0);
    /* RX overflow does not prevent baseline replacement. */
    engine.data[0] = 0x10; engine.data[1] = 0x27;
    for (size_t i = 0; i < 2; ++i) {
        assert(PGW_CANMemory_inject(&memory, &engine) == PGW_OK);
    }
    wait_for_received(connection, 5);
    assert(PGW_CAN_stats(connection, &stats) == PGW_OK);
    assert(stats.receive_drops == 4 && stats.backpressure_commands == 1 &&
           stats.missing_baseline == 1 && stats.unknown_frames == 1);
    assert(reader.iface->read(reader.state, &external_loan, 2) == PGW_OK);
    assert(PGW_SampleSeq_get_length(&external_loan) == 2);
    assert(reader.iface->return_loan(reader.state, &external_loan) == PGW_OK);
    assert(PGW_SampleSeq_get_contiguous_buffer(&external_loan) != NULL);
    values[0] = (PGW_Signal){1003, {PGW_VALUE_BOOLEAN, {.boolean = false}}};
    commands(&input, &outcomes, values, 1);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(PGW_CANMemory_take_sent(&memory, &sent) == PGW_OK);
    assert(sent.data[0] == 0x10 && sent.data[1] == 0x27 &&
           sent.data[3] == 0x0a && sent.data[5] == 2); /* received supersedes shadow */
    bad = engine; bad.length = 7;
    assert(PGW_CANMemory_inject(&memory, &bad) == PGW_OK);
    wait_for_received(connection, 6);
    bad = engine; bad.length = 65;
    assert(PGW_CANMemory_inject(&memory, &bad) == PGW_OK);
    wait_for_received(connection, 7);
    bad = engine; bad.flags = PGW_CAN_FLAG_RTR;
    assert(PGW_CANMemory_inject(&memory, &bad) == PGW_OK);
    wait_for_received(connection, 8);
    bad = engine; bad.flags = PGW_CAN_FLAG_ERROR;
    assert(PGW_CANMemory_inject(&memory, &bad) == PGW_OK);
    wait_for_received(connection, 9);
    bad = engine; bad.flags = PGW_CAN_FLAG_ECHO;
    assert(PGW_CANMemory_inject(&memory, &bad) == PGW_OK);
    wait_for_received(connection, 10);
    assert(PGW_CAN_stats(connection, &stats) == PGW_OK);
    assert(stats.malformed_frames == 4 && stats.echoes == 1);
    memory.send_failure = PGW_IO_ERROR;
    values[0] = (PGW_Signal){1004, {PGW_VALUE_INT64, {.integer = 3}}};
    commands(&input, &outcomes, values, 1);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_IO_ERROR);
    assert(results[0] == PGW_WRITE_FATAL);
    memory.send_failure = PGW_OK;
    values[0] = (PGW_Signal){1003, {PGW_VALUE_BOOLEAN, {.boolean = true}}};
    commands(&input, &outcomes, values, 1);
    assert(writer.iface->write(writer.state, &input, &outcomes) == PGW_OK);
    assert(PGW_CANMemory_take_sent(&memory, &sent) == PGW_OK);
    assert(sent.data[5] == 2); /* failed I/O has no dirty retry */
    assert(PGW_CAN_baseline_timestamp(connection, 0, &timestamp) == PGW_OK);
    assert(timestamp.seconds == 42 && timestamp.nanoseconds == 123);
    memory.receive_failure = PGW_IO_ERROR;
    wait_for_io_errors(connection, 1);
    for (size_t i = 0; i < 16; ++i)
        assert(PGW_CANMemory_inject(&memory, &engine) == PGW_OK);
    assert(PGW_CANMemory_inject(&memory, &engine) == PGW_BACKPRESSURE);
    assert(memory.rx_overflow == 1);
    assert(reader.iface->unregister_listener(
        reader.state, &reader_listener) == PGW_OK);
    assert(auxiliary.iface->unregister_listener(
        auxiliary.state, &auxiliary_listener) == PGW_OK);
#ifdef PGW_CAN_WRAP_ALLOCATIONS
    assert(allocations == 0); /* CAN-NOALLOC, including first traffic/failure */
    frozen = false;
#endif
    assert(PGW_CANAdapter.connection->close(connection) == PGW_OK);
    assert(PGW_SampleSeq_unloan(&input));
    assert(PGW_SampleSeq_unloan(&external_loan));
    assert(PGW_SampleSeq_unloan(&loan));
    assert(PGW_SampleSeq_finalize(&input));
    assert(PGW_SampleSeq_finalize(&external_loan));
    assert(PGW_SampleSeq_finalize(&loan));
    assert(PGW_WriteResultSeq_unloan(&outcomes));
    assert(PGW_WriteResultSeq_finalize(&outcomes));
    assert(PGW_CANConfig_finalize(&cfg) == PGW_OK);
    assert(PGW_CANCategorySeq_unloan(&category_sequence));
    assert(PGW_CANCategorySeq_finalize(&category_sequence));
    free(storage);
    assert(OSAPI_System_finalize());
    return 0;
}

#include "pgw/dds_micro.h"
#include "pgw/signal.h"
#include "ddsAppgen.h"
#include "osapi/osapi_thread.h"
#include <stdio.h>
#include <stdlib.h>
extern const PGW_DDSConfig pgw_config_companion;
typedef struct { PGW_Signal value; } Sample;
static PGW_Status copy(const PGW_Sample *sample, void *out, size_t size)
{
    if (size != sizeof(PGW_Signal)) return PGW_INVALID;
    *(PGW_Signal *)out = ((const Sample *)sample)->value;
    return PGW_OK;
}
static const PGW_SampleAccessI access_i = {
    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), copy, NULL};
int main(int argc, char **argv)
{
    const size_t storage_capacity = 131072;
    void *storage = NULL;
    PGW_Arena arena = {NULL, storage_capacity, 0};
    PGW_Connection *connection = NULL;
    PGW_StreamReader reader;
    PGW_StreamWriter writer;
    PGW_SampleSeq seq;
    PGW_SampleRef refs[8];
    PGW_WriteResult result;
    PGW_WriteResultSeq result_sequence;
    PGW_Representation source;
    Sample command = {{1001, {.kind = PGW_VALUE_DOUBLE, .data.real = 123.4}}};
    bool send = argc == 2;
    bool observed = false;
    unsigned long steps = 1000;
    int failed = 1;
    if (argc > 3) {fprintf(stderr, "usage: %s [speed-rpm-command] [steps]\n", argv[0]); return 2;}
    send = argc >= 2;
    if (send) {
        char *end;
        command.value.value.data.real = strtod(argv[1], &end);
        if (*end) return 2;
    }
    if (argc == 3) {
        char *end;
        steps = strtoul(argv[2], &end, 10);
        if (!steps || *end || steps > 1000000) return 2;
    }
    storage = malloc(storage_capacity);
    if (!storage) return 1;
    arena.storage = storage;
    if (PGW_DDS_register_model(APPGEN_get_library_seq()) != PGW_OK ||
        PGW_DDSMicroAdapter.create(&pgw_config_companion, &arena, &connection) != PGW_OK)
        goto done;
    if (PGW_DDSMicroAdapter.connection->reader(connection, "state_powertrain", &reader) != PGW_OK ||
        PGW_DDSMicroAdapter.connection->writer(connection, "command_powertrain", &writer) != PGW_OK ||
        !PGW_SampleSeq_initialize(&seq) ||
        !PGW_SampleSeq_loan_contiguous(&seq, refs, 0, 8) ||
        !PGW_WriteResultSeq_initialize(&result_sequence) ||
        !PGW_WriteResultSeq_loan_contiguous(&result_sequence, &result, 0, 1)) goto done;
    source = *writer.representation;
    source.access = &access_i;
    if (writer.iface->bind(writer.state, &source) != PGW_OK) goto done;
    failed = 0;
    for (unsigned long step = 0; step < steps; ++step) {
        PGW_Status status = reader.iface->read(reader.state, &seq, 8);
        if (status == PGW_OK) {
            observed = true;
            for (RTI_INT32 i = 0; i < PGW_SampleSeq_get_length(&seq); ++i) {
                PGW_Signal value;
                if (reader.representation->access->copy_value(*PGW_SampleSeq_get_reference(&seq, i),
                        &value, sizeof(value)) != PGW_OK) {failed = 1; break;}
                printf("state key=%u kind=%d value=", value.id, value.value.kind);
                if (value.value.kind == PGW_VALUE_DOUBLE) printf("%g", value.value.data.real);
                else if (value.value.kind == PGW_VALUE_INT64)
                    printf("%lld", (long long)value.value.data.integer);
                else printf("%s", value.value.data.boolean ? "true" : "false");
                putchar('\n');
            }
            if (reader.iface->return_loan(reader.state, &seq) != PGW_OK) {failed = 1; break;}
            if (send) {
                PGW_SampleSeq_set_length(&seq, 1);
                refs[0] = (const PGW_Sample *)&command;
                if (writer.iface->write(writer.state, &seq, &result_sequence) != PGW_OK ||
                    result != PGW_WRITE_ACCEPTED) failed = 1;
                PGW_SampleSeq_set_length(&seq, 0);
                send = false;
            }
        } else if (status != PGW_NO_DATA) {failed = 1; break;}
        OSAPI_Thread_sleep(10);
    }
    PGW_SampleSeq_unloan(&seq);
    PGW_SampleSeq_finalize(&seq);
    PGW_WriteResultSeq_unloan(&result_sequence);
    PGW_WriteResultSeq_finalize(&result_sequence);
    if (!observed || send) failed = 1;
done:
    if (connection && PGW_DDSMicroAdapter.connection->close(connection) != PGW_OK)
        return 1;
    free(storage);
    return failed;
}

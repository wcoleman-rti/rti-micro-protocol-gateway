#ifndef PGW_TEST_FAKE_H
#define PGW_TEST_FAKE_H
#include "pgw/core.h"

typedef struct {
    uint64_t value;
    uint32_t key;
} PGW_TestValue;

typedef struct {
    PGW_TestValue values[4];
    size_t available;
    size_t borrows;
    size_t returns;
    bool loaned;
    bool empty_ok;
    PGW_Status read_status;
    PGW_Status return_status;
} PGW_TestReader;

typedef struct {
    const PGW_Representation *source;
    uint64_t sum;
    size_t writes;
    PGW_WriteResult outcome;
    bool partial;
    PGW_Status status;
} PGW_TestWriter;

extern const PGW_Representation PGW_test_representation;
extern const PGW_StreamReaderI PGW_test_reader_iface;
extern const PGW_StreamWriterI PGW_test_writer_iface;
void PGW_test_route(PGW_Route *, uint32_t, PGW_TestReader *, PGW_TestWriter *,
                    PGW_SampleRef *, PGW_WriteResult *, size_t);
PGW_Status PGW_test_route_initialize_storage(PGW_Route *, PGW_SampleRef *,
                                             PGW_WriteResult *, size_t);
PGW_Status PGW_test_service_set_routes(PGW_Service *, PGW_Route *, size_t);
PGW_Status PGW_test_registry_initialize(PGW_Registry *, PGW_AdapterRef *, size_t,
                                        PGW_RepresentationRef *, size_t);
bool PGW_test_diagnostics_initialize(PGW_Diagnostics *, PGW_Event *, size_t);
#endif

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

#ifndef PGW_TEST_FAKE_H
#define PGW_TEST_FAKE_H
#include "pgw/atomic.h"
#include "pgw/core.h"

typedef struct {
    uint64_t value;
    uint32_t key;
} PGW_TestValue;

typedef struct {
    PGW_TestValue values[8];
    size_t available;
    size_t borrows;
    size_t returns;
    PGW_ATOMIC(RTI_UINT64) read_calls;
    PGW_ATOMIC(RTI_UINT64) notifications;
    const PGW_ReaderListener *listener;
    bool loaned;
    bool empty_ok;
    PGW_Status read_status;
    PGW_Status return_status;
} PGW_TestReader;

typedef struct {
    const PGW_SampleRepresentation *source;
    const PGW_TypeInfo *target_schema;
    uint64_t sum;
    size_t writes;
    PGW_WriteResult outcome;
    bool partial;
    PGW_Status status;
    PGW_ATOMIC(RTI_UINT32) *order_clock;
    unsigned order[8];
} PGW_TestWriter;

extern const PGW_SampleRepresentation PGW_test_representation;
extern const PGW_StreamReaderI PGW_test_reader_iface;
extern const PGW_StreamWriterI PGW_test_writer_iface;
void PGW_test_route(PGW_Route *, uint32_t, PGW_TestReader *, PGW_TestWriter *,
                    PGW_SampleRef *, PGW_WriteResult *, size_t);
PGW_Status PGW_test_route_initialize_storage(PGW_Route *, PGW_SampleRef *,
                                             PGW_WriteResult *, size_t);
PGW_Status PGW_test_service_set_routes(PGW_Service *, PGW_Session *,
                                      PGW_Route *, size_t);
PGW_Status PGW_test_session_set_routes(PGW_Session *, PGW_Route *, size_t);
PGW_Status PGW_test_service_set_sessions(PGW_Service *, PGW_Session *, size_t);
PGW_Status PGW_test_notify_routes(PGW_Service *);
void PGW_test_wait_wakeups(PGW_Session *, uint64_t);
void PGW_test_wait_dispatches(PGW_Session *, uint64_t);
PGW_Status PGW_test_registry_initialize(PGW_Registry *, PGW_AdapterRef *, size_t,
                                        PGW_SampleRepresentationRef *, size_t);
bool PGW_test_diagnostics_initialize(PGW_Diagnostics *, PGW_Event *, size_t);
#endif

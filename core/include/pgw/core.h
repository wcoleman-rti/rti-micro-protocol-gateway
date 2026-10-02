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

#ifndef PGW_CORE_H
#define PGW_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pgw/sequence.h"
#include "pgw/diagnostics.h"

#define PGW_ABI_VERSION 1u

typedef enum {
    PGW_OK, PGW_NO_DATA, PGW_BACKPRESSURE, PGW_INVALID,
    PGW_UNSUPPORTED, PGW_CAPACITY, PGW_IO_ERROR, PGW_LOAN_ERROR, PGW_FATAL
} PGW_Status;

typedef struct {
    PGW_Status status;
    uint32_t entity_id;
    const char *operation;
} PGW_Error;

typedef enum {
    PGW_UNINITIALIZED, PGW_INITIALIZING, PGW_READY,
    PGW_RUNNING, PGW_STOPPED, PGW_FAULTED
} PGW_Lifecycle;

typedef struct {
    const char *name;
    uint32_t version;
    const char *fingerprint;
} PGW_Schema;

typedef struct {
    bool valid;
    bool portable;
    int64_t seconds;
    uint32_t nanoseconds;
} PGW_Timestamp;

typedef struct {
    uint32_t version;
    size_t size;
    PGW_Status (*copy_value)(const PGW_Sample *, void *, size_t);
    PGW_Status (*source_timestamp)(const PGW_Sample *, PGW_Timestamp *);
} PGW_SampleAccessI;

typedef struct {
    const PGW_Schema *schema;
    const char *name;
    size_t sample_size;
    size_t sample_alignment;
    const PGW_SampleAccessI *access;
} PGW_Representation;

typedef enum {
    PGW_WRITE_ACCEPTED, PGW_WRITE_BACKPRESSURE,
    PGW_WRITE_INVALID, PGW_WRITE_FATAL
} PGW_WriteResult;

#define T PGW_WriteResult
#define TSeq PGW_WriteResultSeq
#define REDA_SEQUENCE_API REDA_SEQUENCE_API_UNTYPED
#define TSeq_initialize
#define TSeq_finalize
#define TSeq_get_length
#define TSeq_get_maximum
#define TSeq_set_length
#define TSeq_get_reference
#define TSeq_loan_contiguous
#define TSeq_unloan
#define TSeq_get_contiguous_buffer
#define TSeq_has_ownership
#include "reda/reda_sequence_decl.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate
typedef struct PGW_WriteResultSeq PGW_WriteResultSeq;

typedef struct {
    uint32_t version;
    size_t size;
    PGW_Status (*read)(void *, PGW_SampleSeq *, size_t);
    PGW_Status (*return_loan)(void *, PGW_SampleSeq *);
} PGW_StreamReaderI;

typedef struct {
    uint32_t version;
    size_t size;
    PGW_Status (*bind)(void *, const PGW_Representation *);
    PGW_Status (*write)(void *, const PGW_SampleSeq *, PGW_WriteResultSeq *);
} PGW_StreamWriterI;

typedef struct {
    void *state;
    const PGW_StreamReaderI *iface;
    const PGW_Representation *representation;
} PGW_StreamReader;

typedef struct {
    void *state;
    const PGW_StreamWriterI *iface;
    const PGW_Representation *representation;
} PGW_StreamWriter;

typedef struct PGW_Connection PGW_Connection;
typedef struct {
    uint32_t version;
    size_t size;
    PGW_Status (*reader)(PGW_Connection *, const char *, PGW_StreamReader *);
    PGW_Status (*writer)(PGW_Connection *, const char *, PGW_StreamWriter *);
    PGW_Status (*close)(PGW_Connection *);
} PGW_ConnectionI;

typedef struct {
    void *storage;
    size_t capacity;
    size_t used;
} PGW_Arena;

typedef struct {
    uint32_t version;
    size_t size;
    const char *name;
    PGW_Status (*create)(const void *, PGW_Arena *, PGW_Connection **);
    const PGW_ConnectionI *connection;
} PGW_AdapterI;

typedef const PGW_AdapterI *PGW_AdapterRef;
#define T PGW_AdapterRef
#define TSeq PGW_AdapterSeq
#define REDA_SEQUENCE_API REDA_SEQUENCE_API_UNTYPED
#define TSeq_initialize
#define TSeq_finalize
#define TSeq_get_length
#define TSeq_get_maximum
#define TSeq_set_length
#define TSeq_get_reference
#define TSeq_loan_contiguous
#define TSeq_unloan
#define TSeq_get_contiguous_buffer
#define TSeq_has_ownership
#include "reda/reda_sequence_decl.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate
typedef struct PGW_AdapterSeq PGW_AdapterSeq;

typedef const PGW_Representation *PGW_RepresentationRef;
#define T PGW_RepresentationRef
#define TSeq PGW_RepresentationSeq
#define REDA_SEQUENCE_API REDA_SEQUENCE_API_UNTYPED
#define TSeq_initialize
#define TSeq_finalize
#define TSeq_get_length
#define TSeq_get_maximum
#define TSeq_set_length
#define TSeq_get_reference
#define TSeq_loan_contiguous
#define TSeq_unloan
#define TSeq_get_contiguous_buffer
#define TSeq_has_ownership
#include "reda/reda_sequence_decl.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate
typedef struct PGW_RepresentationSeq PGW_RepresentationSeq;

typedef struct {
    PGW_AdapterSeq adapters;
    PGW_RepresentationSeq bindings;
    bool frozen;
    bool initialized;
    bool adapters_borrowed;
    bool bindings_borrowed;
} PGW_Registry;

typedef struct {
    uint32_t id;
    PGW_StreamReader reader;
    PGW_StreamWriter writer;
    PGW_SampleSeq samples;
    PGW_WriteResultSeq results;
    PGW_Counters counters;
    PGW_Lifecycle lifecycle;
    PGW_Error error;
    bool storage_initialized;
    bool samples_borrowed;
    bool results_borrowed;
} PGW_Route;

#define T PGW_Route
#define TSeq PGW_RouteSeq
#define REDA_SEQUENCE_API REDA_SEQUENCE_API_UNTYPED
#define TSeq_initialize
#define TSeq_finalize
#define TSeq_get_length
#define TSeq_get_maximum
#define TSeq_set_length
#define TSeq_get_reference
#define TSeq_loan_contiguous
#define TSeq_unloan
#define TSeq_get_contiguous_buffer
#define TSeq_has_ownership
#include "reda/reda_sequence_decl.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate
typedef struct PGW_RouteSeq PGW_RouteSeq;

typedef struct {
    PGW_RouteSeq routes;
    size_t cursor;
    size_t route_budget;
    size_t sample_budget;
    PGW_Diagnostics *diagnostics;
    PGW_Lifecycle lifecycle;
    bool (*clock_ns)(void *, uint64_t *);
    void *clock_state;
    bool routes_initialized;
} PGW_Service;

typedef struct {
    size_t objects_bytes;
    size_t sample_references_bytes;
    size_t write_results_bytes;
    size_t diagnostics_bytes;
    size_t total_bytes;
} PGW_CoreResourceReport;

#define T size_t
#define TSeq PGW_SizeSeq
#define REDA_SEQUENCE_API REDA_SEQUENCE_API_UNTYPED
#define TSeq_initialize
#define TSeq_finalize
#define TSeq_get_length
#define TSeq_get_maximum
#define TSeq_set_length
#define TSeq_get_reference
#define TSeq_loan_contiguous
#define TSeq_unloan
#define TSeq_get_contiguous_buffer
#define TSeq_has_ownership
#include "reda/reda_sequence_decl.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate
typedef struct PGW_SizeSeq PGW_SizeSeq;

bool PGW_size_add(size_t, size_t, size_t *);
bool PGW_size_multiply(size_t, size_t, size_t *);
PGW_Status PGW_core_resource_report(const PGW_SizeSeq *, bool, size_t,
                                   PGW_CoreResourceReport *);
PGW_Status PGW_Arena_allocate(PGW_Arena *, size_t, size_t, void **);
bool PGW_schema_equal(const PGW_Schema *, const PGW_Schema *);
PGW_Status PGW_Registry_initialize(PGW_Registry *, const PGW_AdapterSeq *,
                                  const PGW_RepresentationSeq *);
PGW_Status PGW_Registry_finalize(PGW_Registry *);
PGW_Status PGW_Registry_register_adapter(PGW_Registry *, const PGW_AdapterI *);
PGW_Status PGW_Registry_register_binding(PGW_Registry *, const PGW_Representation *);
const PGW_AdapterI *PGW_Registry_find_adapter(const PGW_Registry *, const char *);
const PGW_Representation *PGW_Registry_find_binding(const PGW_Registry *, const char *);
PGW_Status PGW_Route_initialize_storage(PGW_Route *, const PGW_SampleSeq *,
                                       const PGW_WriteResultSeq *);
PGW_Status PGW_Service_set_routes(PGW_Service *, const PGW_RouteSeq *);
PGW_Status PGW_Service_initialize(PGW_Service *);
PGW_Status PGW_Service_step(PGW_Service *);
PGW_Status PGW_Service_stop(PGW_Service *);
PGW_Status PGW_Service_finalize(PGW_Service *);
const char *PGW_status_name(PGW_Status);

#endif

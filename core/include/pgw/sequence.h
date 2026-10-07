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

#ifndef PGW_SEQUENCE_H
#define PGW_SEQUENCE_H

/** @addtogroup pgw_core_api
 * @{
 */

#include "reda/reda_sequence.h"

/** @brief Opaque sample delivered by a stream reader.
 * Its concrete representation is adapter-defined. Use the bound
 * PGW_SampleRepresentation access operations when the value must be inspected
 * without knowing its native type.
 */
typedef struct PGW_Sample PGW_Sample;
/** @brief Non-owning reference to an immutable sample.
 * The reference remains valid only while the reader's sample loan is active.
 */
typedef const PGW_Sample *PGW_SampleRef;

/** @brief Sequence of sample references used by stream readers and routes.
 *
 * The generated REDA sequence API provides initialization, finalization,
 * length/capacity access, and contiguous-storage loan operations. A caller
 * supplies backing storage where required; a borrowed buffer must remain valid
 * until the matching unloan/finalize operation. A reader loan is separately
 * returned through its PGW_StreamReaderI::return_loan callback.
 */
#define T PGW_SampleRef
#define TSeq PGW_SampleSeq
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
/** @brief Opaque generated sequence of PGW_SampleRef values. */
typedef struct PGW_SampleSeq PGW_SampleSeq;

/** @} */
#endif

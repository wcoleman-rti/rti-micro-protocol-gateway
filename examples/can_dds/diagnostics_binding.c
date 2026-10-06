/*
 * (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software. Licensee has the right to distribute object form only
 * for use with RTI products. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose. RTI is under no
 * obligation to maintain or support the software. RTI shall not be liable for any
 * incidental or consequential damages arising out of the use or inability to use
 * the software.
 */

#include "diagnostics_binding.h"
#include <pgw/diagnostics.h>

PGW_Status PGW_DDS_export_snapshot(PGW_StreamWriter *writer,
                                   const PGW_CounterSnapshot *snapshot)
{
    PGW_SampleSeq seq;
    PGW_SampleRef reference = (const PGW_Sample *)snapshot;
    PGW_WriteResult result = PGW_WRITE_FATAL;
    PGW_WriteResultSeq result_sequence;
    PGW_Status status;
    if (!writer || !snapshot || !PGW_SampleSeq_initialize(&seq)) return PGW_INVALID;
    if (!PGW_SampleSeq_loan_contiguous(&seq, &reference, 1, 1)) {
        PGW_SampleSeq_finalize(&seq);
        return PGW_LOAN_ERROR;
    }
    if (!PGW_WriteResultSeq_initialize(&result_sequence)) {
        status = PGW_INVALID;
        goto cleanup;
    }
    if (!PGW_WriteResultSeq_loan_contiguous(&result_sequence, &result, 0, 1)) {
        PGW_WriteResultSeq_finalize(&result_sequence);
        status = PGW_LOAN_ERROR;
        goto cleanup;
    }
    status = writer->iface->write(writer->state, &seq, &result_sequence);
    PGW_WriteResultSeq_unloan(&result_sequence);
    PGW_WriteResultSeq_finalize(&result_sequence);
cleanup:
    PGW_SampleSeq_unloan(&seq);
    PGW_SampleSeq_finalize(&seq);
    if (status != PGW_OK) return status;
    return result == PGW_WRITE_ACCEPTED ? PGW_OK :
        result == PGW_WRITE_BACKPRESSURE ? PGW_BACKPRESSURE : PGW_INVALID;
}

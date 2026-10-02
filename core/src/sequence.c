#include "pgw/core.h"
#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"

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
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate

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
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate

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
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate

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
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate

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
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate

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
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate

#define T PGW_Event
#define TSeq PGW_EventSeq
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
#include "reda/reda_sequence_defn.h"
#undef T
#undef TSeq
#undef REDA_SEQUENCE_API
#undef concatenate

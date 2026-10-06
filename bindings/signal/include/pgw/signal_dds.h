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

#ifndef PGW_SIGNAL_DDS_H
#define PGW_SIGNAL_DDS_H
#include <pgw/dds/connext_micro.h>
#include "pgw_codec.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PGW_SIGNAL_DECLARE_CATEGORY(name) \
    extern const PGW_DDSBinding PGW_signal_dds_binding_##name;
PGW_CODEC_CATEGORIES(PGW_SIGNAL_DECLARE_CATEGORY)
#undef PGW_SIGNAL_DECLARE_CATEGORY
bool PGW_signal_validate_dds(const void *);
PGW_Status PGW_signal_bind_view(void *, const PGW_Representation *);
PGW_Status PGW_signal_from_view(const PGW_SampleView *, void *, size_t);
PGW_Status PGW_signal_from_dds(const void *, void *, size_t);
DDS_ReturnCode_t PGW_signal_to_dds(const void *, void *);
PGW_Status PGW_signal_write_view(void *, DDS_DataWriter *, const PGW_SampleView *,
                                const struct DDS_Time_t *, DDS_ReturnCode_t *);
#ifdef __cplusplus
}
#endif
#endif

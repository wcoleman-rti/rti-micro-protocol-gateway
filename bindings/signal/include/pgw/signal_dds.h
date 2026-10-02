#ifndef PGW_SIGNAL_DDS_H
#define PGW_SIGNAL_DDS_H
#include <pgw/dds_micro.h>
#include "pgw_codec.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const PGW_DDSBinding PGW_signal_dds_binding;
#define PGW_SIGNAL_DECLARE_CATEGORY(name) \
    extern const PGW_DDSBinding PGW_signal_dds_binding_##name;
PGW_CODEC_CATEGORIES(PGW_SIGNAL_DECLARE_CATEGORY)
#undef PGW_SIGNAL_DECLARE_CATEGORY
#ifdef __cplusplus
}
#endif
#endif

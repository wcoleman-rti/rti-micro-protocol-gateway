#ifndef PGW_DIAGNOSTICS_BINDING_H
#define PGW_DIAGNOSTICS_BINDING_H
#include "pgw/dds_micro.h"
extern const PGW_DDSBinding PGW_diagnostics_binding;
PGW_Status PGW_DDS_export_snapshot(PGW_StreamWriter *, const PGW_CounterSnapshot *);
#endif

#ifndef PGW_PROBE_BINDING_H
#define PGW_PROBE_BINDING_H
#include "pgw/dds_micro.h"
typedef struct {uint32_t id; int32_t reading;} PGW_ProbeValue;
extern const PGW_DDSBinding PGW_probe_binding;
#endif

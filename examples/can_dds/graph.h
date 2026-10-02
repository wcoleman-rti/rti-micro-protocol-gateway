#ifndef PGW_EXAMPLE_GRAPH_H
#define PGW_EXAMPLE_GRAPH_H
#include "pgw/core.h"
#include "pgw/can.h"
PGW_Status PGW_example_attach_routes(PGW_Connection *, PGW_Connection *,
                                    PGW_RouteSeq *);
PGW_Status PGW_example_can_categories(const PGW_Schema *, PGW_CANCategorySeq *);
extern const unsigned pgw_config_can_receive_budget, pgw_config_can_write_capacity;
#endif

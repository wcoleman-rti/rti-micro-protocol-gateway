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

#ifndef PGW_EXAMPLE_GRAPH_H
#define PGW_EXAMPLE_GRAPH_H
#include "pgw/core.h"
#include "pgw/can.h"
PGW_Status PGW_example_attach_routes(PGW_Connection *, PGW_Connection *,
                                    PGW_RouteSeq *);
PGW_Status PGW_example_can_categories(const PGW_Schema *, PGW_CANCategorySeq *);
extern const unsigned pgw_config_can_receive_budget, pgw_config_can_write_capacity;
#endif

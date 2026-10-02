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

#ifndef PGW_TEST_ALLOCATION_H
#define PGW_TEST_ALLOCATION_H
#include <stdbool.h>
#include <stdint.h>
void PGW_allocation_monitor(bool);
uint64_t PGW_allocation_calls(void);
uint64_t PGW_osapi_allocation_calls(void);
#endif

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

#include "pgw/runtime.h"
#include "osapi/osapi_system.h"

bool PGW_Runtime_initialize(void)
{
    return OSAPI_System_initialize() == RTI_TRUE;
}

bool PGW_Runtime_monotonic_time_ns(uint64_t *nanoseconds)
{
    RTI_INT32 seconds = 0;
    RTI_UINT32 fraction = 0;
    if (nanoseconds == NULL ||
        !OSAPI_System_get_ticktime(&seconds, &fraction) || seconds < 0 ||
        fraction >= UINT32_C(1000000000)) {
        return false;
    }
    *nanoseconds = (uint64_t)seconds * UINT64_C(1000000000) + fraction;
    return true;
}

bool PGW_Runtime_monotonic_clock(void *context, uint64_t *nanoseconds)
{
    (void)context;
    return PGW_Runtime_monotonic_time_ns(nanoseconds);
}

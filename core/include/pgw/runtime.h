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

#ifndef PGW_RUNTIME_H
#define PGW_RUNTIME_H

/** @addtogroup pgw_core_api
 * @{
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Initialize Micro's process-global OSAPI runtime.
 * Call before using the monotonic clock or creating service sessions.
 * Finalization of OSAPI is
 * the application's responsibility after all Micro users have stopped.
 * @return True when OSAPI initialization succeeds; false otherwise.
 */
bool PGW_Runtime_initialize(void);
/** @brief Read the OSAPI monotonic clock in nanoseconds.
 * @param nanoseconds Receives the clock value on success.
 * @return True on success; false for a null output pointer or clock failure.
 */
bool PGW_Runtime_monotonic_time_ns(uint64_t *nanoseconds);
/** @brief Clock callback adapter for PGW_Service.
 * @param context Unused; may be null.
 * @param nanoseconds Receives monotonic time in nanoseconds.
 * @return Same result as PGW_Runtime_monotonic_time_ns().
 */
bool PGW_Runtime_monotonic_clock(void *context, uint64_t *nanoseconds);

/** @} */
#endif

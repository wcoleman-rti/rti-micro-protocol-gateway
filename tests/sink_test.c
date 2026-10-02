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

#define _POSIX_C_SOURCE 200809L
#include "pgw/local_sink.h"
#include "support/allocation.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    int descriptors[2];
    assert(!pipe(descriptors));
    char scratch[512];
    PGW_LocalSink sink;
    assert(PGW_LocalSink_initialize(&sink, descriptors[1], scratch, sizeof(scratch)) == PGW_OK);
    PGW_CounterSnapshot snapshot = {.version = 1, .entity_id = 42};
    snapshot.values[PGW_COUNT_INVALID] = 3;
    PGW_allocation_monitor(true);
    assert(PGW_LocalSink_snapshot(&sink, &snapshot) == PGW_OK);
    char readback[512] = {0};
    ssize_t length = read(descriptors[0], readback, sizeof(readback) - 1);
    assert(length > 0);
    assert(strstr(readback, "\"entity_id\":42"));
    assert(strstr(readback, "[0,0,0,3,"));
    char fill[512] = {0};
    while (write(descriptors[1], fill, sizeof(fill)) > 0) {}
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    assert(PGW_LocalSink_snapshot(&sink, &snapshot) == PGW_BACKPRESSURE);
    sink.capacity = 2;
    assert(PGW_LocalSink_snapshot(&sink, &snapshot) == PGW_CAPACITY);
    PGW_CounterSnapshot metrics;
    assert(PGW_Counters_snapshot(&sink.counters, 0, 1, 0, &metrics));
    assert(metrics.values[PGW_COUNT_ACCEPTED] == 1);
    assert(metrics.values[PGW_COUNT_BACKPRESSURE] == 1);
    assert(metrics.values[PGW_COUNT_EXPORT_ERRORS] == 1);
    snapshot.version = 2;
    assert(PGW_LocalSink_snapshot(&sink, &snapshot) == PGW_INVALID);
    snapshot.version = 1;
    assert(!PGW_allocation_calls() && !PGW_osapi_allocation_calls());
    assert(!close(descriptors[0]));
    sink.capacity = sizeof(scratch);
    assert(PGW_LocalSink_snapshot(&sink, &snapshot) == PGW_IO_ERROR);
    assert(!PGW_allocation_calls() && !PGW_osapi_allocation_calls());
    PGW_allocation_monitor(false);
    assert(!close(descriptors[1]));
    return 0;
}

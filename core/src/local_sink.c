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
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

static ssize_t write_record(int descriptor, const char *buffer, size_t count)
{
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    int error = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (error) { errno = error; return -1; }
    if (sigpending(&pending)) {
        error = errno;
        (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
        errno = error;
        return -1;
    }
    ssize_t result = write(descriptor, buffer, count);
    int saved = errno;
    if (result < 0 && saved == EPIPE && !sigismember(&pending, SIGPIPE)) {
        const struct timespec zero = {0, 0};
        (void)sigtimedwait(&blocked, NULL, &zero);
    }
    error = pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (error) { errno = error; return -1; }
    errno = saved;
    return result;
}

PGW_Status PGW_LocalSink_initialize(PGW_LocalSink *s, int descriptor,
                                   char *buffer, size_t capacity)
{
    if (!s || descriptor < 0 || !buffer || !capacity) return PGW_INVALID;
    if (!PGW_Counters_initialize(&s->counters)) return PGW_UNSUPPORTED;
    int flags = fcntl(descriptor, F_GETFL);
    if (flags == -1 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == -1)
        return PGW_IO_ERROR;
    s->descriptor = descriptor;
    s->buffer = buffer;
    s->capacity = capacity;
    return PGW_OK;
}

PGW_Status PGW_LocalSink_snapshot(PGW_LocalSink *s, const PGW_CounterSnapshot *snapshot)
{
    if (!s || !snapshot || !s->buffer || !s->capacity) return PGW_INVALID;
    if (snapshot->version != 1) {
        PGW_Counters_add(&s->counters, PGW_COUNT_INVALID, 1);
        return PGW_INVALID;
    }
    size_t count;
    if (!PGW_snapshot_json(snapshot, s->buffer, s->capacity, &count)) {
        PGW_Counters_add(&s->counters, PGW_COUNT_EXPORT_ERRORS, 1);
        return PGW_CAPACITY;
    }
    ssize_t written = write_record(s->descriptor, s->buffer, count);
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        PGW_Counters_add(&s->counters, PGW_COUNT_BACKPRESSURE, 1);
        return PGW_BACKPRESSURE;
    }
    if (written < 0 || (size_t)written != count) {
        PGW_Counters_add(&s->counters, PGW_COUNT_EXPORT_ERRORS, 1);
        return PGW_IO_ERROR;
    }
    PGW_Counters_add(&s->counters, PGW_COUNT_ACCEPTED, 1);
    return PGW_OK;
}

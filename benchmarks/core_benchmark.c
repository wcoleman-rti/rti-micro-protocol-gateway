#define _POSIX_C_SOURCE 200809L
#include "fake.h"
#include "allocation.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>

static uint64_t now_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) {
        perror("clock_gettime");
        exit(2);
    }
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static uint64_t percentile(const uint64_t *histogram, uint64_t count, uint64_t percent)
{
    uint64_t target = (count * percent + 99) / 100;
    uint64_t accumulated = 0;
    for (unsigned i = 0; i < 64; ++i) {
        accumulated += histogram[i];
        if (accumulated >= target) return i == 63 ? UINT64_MAX : (UINT64_C(1) << (i + 1)) - 1;
    }
    return 0;
}

static int usage(const char *executable)
{
    fprintf(stderr, "usage: %s [steps:1..100000000] [timing:0|1]\n", executable);
    return 2;
}

int main(int argc, char **argv)
{
    uint64_t steps = 100000;
    bool timing = true;
    if (argc > 3) return usage(argv[0]);
    if (argc >= 2) {
        char *end;
        errno = 0;
        steps = strtoull(argv[1], &end, 10);
        if (errno || *end || !steps || steps > 100000000 || argv[1][0] == '-') return usage(argv[0]);
    }
    if (argc == 3) {
        if (argv[2][0] != '0' && argv[2][0] != '1') return usage(argv[0]);
        if (argv[2][1]) return usage(argv[0]);
        timing = argv[2][0] == '1';
    }
    PGW_TestReader reader = {.values = {{1, 1}, {2, 2}, {3, 3}, {4, 4}}, .available = 4};
    PGW_TestWriter writer = {0};
    PGW_SampleRef references[4];
    PGW_WriteResult outcomes[4];
    PGW_Route route;
    PGW_test_route(&route, 1, &reader, &writer, references, outcomes, 4);
    PGW_Service service = {.route_budget = 1, .sample_budget = 4};
    if (PGW_test_service_set_routes(&service, &route, 1) != PGW_OK) return 3;
    uint64_t init_start = now_ns();
    if (PGW_Service_initialize(&service) != PGW_OK) return 3;
    uint64_t init_ns = now_ns() - init_start;
    uint64_t histogram[64] = {0};
    uint64_t minimum = UINT64_MAX, maximum = 0, sum = 0;
    PGW_allocation_monitor(true);
    uint64_t start = now_ns();
    for (uint64_t i = 0; i < steps; ++i) {
        uint64_t before = timing ? now_ns() : 0;
        if (PGW_Service_step(&service) != PGW_OK) return 4;
        if (timing) {
            uint64_t duration = now_ns() - before;
            if (duration < minimum) minimum = duration;
            if (duration > maximum) maximum = duration;
            if (sum > UINT64_MAX - duration) return 5;
            sum += duration;
            unsigned bucket = 0;
            for (uint64_t n = duration; n > 1; n >>= 1) ++bucket;
            ++histogram[bucket];
        }
    }
    uint64_t elapsed = now_ns() - start;
    uint64_t allocations = PGW_allocation_calls();
    uint64_t osapi_allocations = PGW_osapi_allocation_calls();
    PGW_allocation_monitor(false);
    PGW_CounterSnapshot snapshot;
    PGW_Counters_snapshot(&route.counters, 1, 1, now_ns(), &snapshot);
    if (allocations || osapi_allocations || reader.borrows != reader.returns ||
        snapshot.values[PGW_COUNT_ACCEPTED] != 4 * steps || writer.sum != 10 * steps)
        return 6;
    if (PGW_Service_stop(&service) != PGW_OK || PGW_Service_finalize(&service) != PGW_OK) return 7;
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage)) return 8;
    printf("{\"format_version\":1,\"workload\":\"opaque-core-four-samples\","
           "\"steps\":%" PRIu64 ",\"samples\":%" PRIu64 ",\"timing\":%s,"
           "\"elapsed_ns\":%" PRIu64 ",\"initialization_ns\":%" PRIu64 ","
           "\"samples_per_second\":%.3f,\"gateway_runtime_allocations\":%" PRIu64 ","
           "\"osapi_runtime_allocations\":%" PRIu64 ","
           "\"allocation_coverage\":\"wrapped libc malloc/calloc/realloc/aligned_alloc/posix_memalign and OSAPI allocate/realloc/allocate_buffer; core-only\","
           "\"gateway_static_fixture_bytes\":%zu,\"getrusage_maxrss_kib\":%ld,"
           "\"cpu_user_us\":%ld,\"cpu_system_us\":%ld,"
           "\"step_latency_ns\":{\"boundary\":\"scheduler entry to return; not wire latency\","
           "\"count\":%" PRIu64 ",\"min\":%" PRIu64 ",\"max\":%" PRIu64 ","
           "\"mean\":%.3f,\"p50_upper\":%" PRIu64 ",\"p95_upper\":%" PRIu64 ","
           "\"p99_upper\":%" PRIu64 ",\"histogram\":\"64 power-of-two buckets; upper-bound percentiles\"}}\n",
           steps, steps * 4, timing ? "true" : "false", elapsed, init_ns,
           elapsed ? steps * 4.0 * 1e9 / elapsed : 0.0, allocations, osapi_allocations,
           sizeof(service) + sizeof(route) + sizeof(reader) + sizeof(writer) +
               sizeof(references) + sizeof(outcomes) + sizeof(histogram),
           usage.ru_maxrss, usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec,
           usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec,
           timing ? steps : 0, timing ? minimum : 0, maximum,
           timing ? (double)sum / steps : 0.0,
           timing ? percentile(histogram, steps, 50) : 0,
           timing ? percentile(histogram, steps, 95) : 0,
           timing ? percentile(histogram, steps, 99) : 0);
    return 0;
}

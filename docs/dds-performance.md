<!--
  (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.

  RTI grants Licensee a license to use, modify, compile, and create derivative
  works of the Software. Licensee has the right to distribute object form only
  for use with RTI products. The Software is provided "as is", with no warranty
  of any type, including any warranty for fitness for any purpose. RTI is under no
  obligation to maintain or support the Software. RTI shall not be liable for any
  incidental or consequential damages arising out of the use or inability to use
  the software.
-->

# RTI Connext Micro, MAG, and CAN benchmark

`pgw_dds_benchmark` links the installed RTI Connext Micro libraries, actual MAG
generated named gateway/companion entities, generated Signal/Probe bindings,
and the memory CAN transport. It is not a DDS simulation; memory CAN substitutes
only for the transport side of the CAN adapter.

## Reproduce

Enable DDS, CAN, examples, tests, and benchmarks at configuration time. Select
the installed Micro SDK/JRE as described in [build.md](build.md). For example:

```sh
cmake -S . -B build-dds \
  -DPGW_ENABLE_DDS=ON -DPGW_ENABLE_CAN=ON \
  -DPGW_BUILD_EXAMPLES=ON -DPGW_BUILD_TESTS=ON -DPGW_BUILD_BENCHMARKS=ON \
  -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_JREHOME="<installed Java 17 runtime>"
cmake --build build-dds --target pgw_dds_benchmark
ctest --test-dir build-dds -R REQ_BENCHMARK_DDS --output-on-failure
python benchmarks/runner/run.py build-dds/benchmarks/dds/pgw_dds_benchmark \
  --batches 10000 --timing 1 --metadata 1 --repeat 3 --timeout 90 \
  --results results/dds
python benchmarks/runner/run.py build-dds/benchmarks/dds/pgw_dds_benchmark \
  --batches 1000 --timing 0 --metadata 0 --results results/dds
```

Executable arguments are `batches timing [preserve_probe_timestamp]`: batches
1..100000, timing 0/1, preservation 0/1, defaults 1000/1/0. The runner's
`--metadata` selects preservation, not metadata capture. Capture remains enabled.
Management endpoints/export can separately be disabled with
`PGW_DDS_DIAGNOSTICS=OFF`.

Each process takes the project domain lock and checks the configured private
domain's entire RTPS UDP port range before creating participants. Occupancy
returns 77 (CTest skips; the profiling runner reports unavailable/failure).
This conservative gate coordinates project processes, not unrelated DDS
users. It does not establish network namespace isolation or authorize use of
an occupied domain. Advisory-lock waiting precedes the measured initialization.

Dedicated-vcan verification is not part of this workload. No vcan interface is
created or assumed; all DDS/CAN benchmark results use the bounded memory CAN
transport. See the separate SocketCAN integration coverage and its requirements
in [vcan-integration.md](vcan-integration.md).

## Workload and accounting

Each batch injects one real Engine CAN frame. Its connection receiver decodes
four powertrain signals, notifies the typed reader listener, and the session
worker forwards them through the actual local DDS writer. The harness waits for
the companion's correlated speed state. The companion writes a speed command
over DDS; the gateway patches the existing CAN baseline and dequeues the
resulting frame. Assertions verify the new speed bytes and preservation of the
other six bytes. Every eighth batch deliberately fills the single-slot CAN TX ring:
the command must report backpressure and the owned filler is removed.

One independent, non-CAN Probe is written and observed each batch. In
preservation mode its explicit portable source timestamp is checked exactly.
That synthetic protocol timestamp is never the observation clock. Management
snapshots are exported/read on a 100 ms monotonic timer when enabled; this
benchmark timer is separate from session reader-readiness dispatch.

JSON separates offered/decoded states, local DDS accepted states/commands,
local CAN accepted commands, peer state/Probe/management observations, and
dequeued CAN frames. A successful DDS write does **not** imply delivery.
BEST_EFFORT KEEP_LAST can replace unread intermediate states; peer reader
`samples_lost`/`samples_rejected` are actual SDK statuses.
`unobserved_state_samples_at_stop` is the local-minus-observed gap, not a
claimed transport-loss count: the final unread/in-flight states also contribute.
Deliberate CAN backpressure, invalid commands, RX signal drops, and outstanding
loans are independently reported.

`samples_per_second` uses four decoded states plus one offered command per
batch; Probe and management are excluded. This cooperative, peer-gated
workload is not a maximum sustainable DDS throughput measurement.

## Timing and bounded storage

Timing uses same-process POSIX `CLOCK_MONOTONIC`, not wall time or DDS source
timestamps. The service event callback and diagnostic snapshot observation
timestamps use fallible `PGW_Runtime_monotonic_time_ns` from `PGW::core`, backed
by `OSAPI_System_get_ticktime`. On the installed SDK the OSAPI tick clock is
coarse; using it for sub-tick latency histograms would produce misleading zero
durations. The host benchmark harness uses its own monotonic clock for its
latency measurements. Clock failure is reported; no synthetic zero timestamp is
treated as valid.

* Local: immediately before memory-CAN injection through receiver
  dequeue/decode, reader notification, bounded session dispatch, local DDS
  acceptance, and route loan return. It is not wire latency.
* Roundtrip: the same start through companion observation, DDS command,
  event-driven gateway application, and memory-CAN TX dequeue. Peer-read
  waiting/sleep remains in this end-to-end measurement. Blocked batches end at the explicitly observed drop;
  this histogram therefore is not a delivered-only latency distribution.
* Elapsed workload excludes initialization and initial matching, but includes
  Probe, management, peer-read attempts, and intentional backpressure.

Both histograms use fixed 64-element arrays. Bucket i covers durations below
`2^(i+1)` nanoseconds (bucket 0 includes zero); reported percentiles are bucket
upper bounds, not exact quantiles. Raw bucket counts are persisted. Timing-off
runs capture no latency samples but retain workload/counter checks.

The JSON reports arena reservation/usage, CAN's computed bound and actual
mutable bytes, core's resource report, adapter fixed-storage reports, and
actual DDS entity/history QoS getters. `fixture_primary_storage_bytes` covers
the principal arena, route/reference/result arrays, rings, transport object,
and histograms; it is not every local variable or total process memory.
RSS/CPU are process observations. Exact middleware bytes are not inferred
from entity limits.

The 524288-byte arena backing is one successful fixed-size `malloc` during
initialization, with no fallback or growth, and is freed after all adapters
shut down. `arena_initialization_bytes` and `arena_reserved_bytes` report this
reservation; the allocation is included in initialization interception.
Allocated storage has no declared type, allowing private object construction
without relying on effective-type-unsafe casts into a declared byte array.
Caller-owned route/result/ring arrays remain genuinely typed objects.

Readers reserve two physical samples per known key: one retained latest value
and one potentially outstanding loan. History depth remains one; writer
history remains one/key with zero blocking time. Reader totals derive from
the real category key inventory. MAG alone owns entity topology/factory
allocation counts. The strict compiler rejects inconsistent totals and
reader per-key limits outside the supported one/two-slot policy.

## Allocation scope and interpretation

Five libc and three OSAPI allocation controls plus an arena control verify
the interceptors before measurement. Initialization monitoring starts after
OSAPI initialization/control verification and covers MAG/model/entity/type-binding
construction. After service initialization the arena is frozen and monitoring includes initial
endpoint matching, the first business/Probe/management traffic, all batches,
resource/status/snapshot reads, and service STOP.

Discovery starts during participant initialization; matching is a separately
reported post-initialization phase, not a claim that discovery first begins then.
The process contains only the declared peers, not later undeclared-peer churn.

The arena must receive zero runtime requests and must not grow. Libc/OSAPI
phase totals include middleware/threads and are **not** ownership attribution.
Therefore `gateway_runtime_allocations` is null rather than an unsupported
gateway-versus-middleware heap count; gateway arena requests are explicitly
zero. Zero observed totals mean no calls through the covered symbols, not
that libc internals, kernel allocation, or every vendor-internal allocator
were measured. Formatting/cleanup occur outside the runtime monitoring phase.

## Baselines and limitations

Runner manifests under `results/dds/` persist the executable/source/input
fingerprints, selected build/SDK/compiler, metadata mode, timeout, repeated
measurements, external RSS, counters, actual resources, and histogram counts.
Compare only like workloads, fingerprints, resource/configuration limits,
metadata/timing modes, clock source, and timing boundaries.

There are no hard-time/WCET thresholds. These Linux loopback results establish
functional workload/resource/allocation observations, not schedulability,
network loss guarantees, security, deployment-target RAM, or real-bus timing.

The maintained result directory is configurable and ignored by version control.
Each run captures its own machine/compiler/SDK provenance for local comparison;
do not commit those environment-specific measurements as portable defaults.

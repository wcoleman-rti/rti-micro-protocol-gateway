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

# Memory-CAN adapter benchmark

`pgw_can_benchmark [batches] [timing0|1] [metadata0|1]` measures the generated CAN
codec, adapter, and core routing paths. Batches default to 100,000 and must be
within 1..100,000,000. Timing and metadata capture default to 1. This is **not a
DDS simulation**, live SocketCAN test, physical-bus throughput measurement, or
wire-latency estimate.

## Workload and assertions

Two actual `PGW_CANAdapter` connections use independent bounded memory
transports. One category route forwards four decoded Engine signals from
the ingress connection's receiver, through its registered reader listener and
the session worker, into the second connection's negotiated synchronous
writer. Generated descriptors and codecs
come from the maintained example DBC/mapping via `pgw_generate_dbc`.
CAN catalogs, scratch values, baseline/staging records, private loan slots and
write sidecars use Micro typed sequences. Core route/reference/result members
also use typed sequences; see [Typed sequences](sequences.md) for their setup
and ownership.

After service initialization, the output receives its first raw baseline. Each subsequent batch
injects one input Engine frame with changing speed/state values, a signed
Motorola torque value and boolean command. Unmapped input bytes differ from
the output baseline. All accepted output frames are checked against an
independent eight-byte expected vector, including the output baseline's
reserved bits and unrelated bytes. Each four-command batch produces one
coalesced output frame.

Every eighth batch encounters TX backpressure: the preceding accepted frame
was intentionally left in the one-frame TX ring. The dropped batch is not
retried. The harness then drains and verifies the preceding frame, and
normal processing continues. A final held frame is drained if the run ends
between these two phases. The workload checks exact outcomes:

- Routed/attempted samples: `4 * batches`.
- Backpressured frames: `floor(batches / 8)`; backpressured commands: four times
  that count.
- Accepted/coalesced frames: `batches - floor(batches / 8)`.
- Accepted commands: four times accepted frames.
- Received frames: `batches + 1`, including the output baseline.
- Decoded signals: `4 * batches + 4`, including baseline decoding.
- Checked command bytes: eight times accepted frames.
- Invalid commands, RX dropped samples and outstanding route loans: zero.

There is no warm-up phase. First baseline reception, first routing traffic,
all backpressure/error-free recovery work and first per-batch timing observations
occur within the allocation monitoring interval.
The first baseline's four decoded samples are also borrowed and inspected:
enabled timestamp capture returns the exact injected timestamp; disabled capture
returns `PGW_NO_DATA` and invalid timestamps. This borrowed batch is returned
before business routing. It does not alter the stored baseline or route counts.

## Allocation and timing boundaries

The benchmark links the shared `pgw_allocation_probe`. Before the
post-initialization measurement barrier, deliberate controls verify link interception of
`malloc`, `calloc`, `realloc`, `aligned_alloc`, `posix_memalign`,
`OSAPI_Heap_allocate`, `OSAPI_Heap_realloc` and
`OSAPI_Heap_allocate_buffer`. All eight are checked individually or against
explicit counter increases. Runtime traffic asserts zero libc and OSAPI
**allocation** calls. This does not prohibit ordinary bounded libc operations
such as `memcpy`, or claim coverage of every SDK/private allocator, kernel
queues or a whole DDS process.

Opaque private objects are constructed in one fixed 32,768-byte allocation
made during initialization, split into two fixed 16,384-byte arenas. Allocated
storage has no declared backing type, allowing the owning adapter's typed
stores to establish effective types. The benchmark does not construct private
objects by overlaying declared byte arrays or alignment unions. The block is
never resized and is freed only after loans are finalized and both connections
are closed. Typed transport/frame/reference arrays remain caller-owned fixed
objects. This initialization allocation is included in initialization timing
and the resource report, not hidden as runtime allocation.

When enabled, the fixed 64-bin histogram samples immediately before memory-CAN
injection through receiver dequeue/decode/category demux, reader notification,
session-worker wake, bounded core forwarding, CAN patch/coalesced transport
acceptance and loan return. Golden-byte comparisons are outside this per-batch
boundary but inside total elapsed time. The initial output-baseline read is
outside the batch histogram and inside total elapsed time. Neither boundary
starts at real network arrival or ends at physical delivery.

The monotonic host clock is used only for these local execution durations.
Histogram bins cover powers of two; bucket zero covers 0..1 ns. Reported
p50/p95/p99 are bucket upper bounds, not exact sample percentiles. Counter-only
mode disables per-batch clock calls and reports histogram count/values zero,
while retaining start/end clocks, functional assertions and allocation checks.
Timing and counter-only results are intentionally not comparable to each other.

## Metadata and resources

The workload injects valid portable receive timestamps. Metadata mode 1
captures timestamps and private frame/interface context. Mode 0 sets
`disable_metadata_capture` on both CAN connections, skipping optional per-sample
context stores. Sample storage, capacities, payload decode/command patches and
golden vectors are identical in both modes. Fixed wrapper loan copies still
copy the entire representation; this mode does not claim reduced memory or
elimination of all metadata-related representation costs. Raw transport frames
and baseline-age timestamps remain stored in both modes.

The JSON `metadata` boolean and `metadata_capture` object describe the actual
selected mode. Timestamp requests are checked explicitly and return `PGW_NO_DATA`
when disabled; ordinary logical samples remain valid and route successfully.
Output timestamp/identity preservation is unsupported in CAN and remains false
in both modes. Native CAN cannot express those fields without an explicit new
payload schema; this benchmark therefore measures capture on/off, **not**
pretended preservation on/off. Receive timestamps are not native sender times.

JSON separates attempted sample throughput from accepted commands/frames,
and includes frame/signal/drop counters, exact checked bytes, category high
water, fixed transport/route capacities and these resource measurements:

- `arena_bytes_used`: actual mutable adapter arena consumption across both
  connections.
- `arena_bytes_bound`: conservative combined size including alignment padding.
- `arena_reserved_bytes`: fixed backing allocation reserved by this fixture.
- `gateway_initialization_heap_bytes` / `gateway_initialization_heap_blocks`:
  the requested 32,768 bytes / one block, excluding allocator bookkeeping;
  no fallback or runtime growth exists.
- `gateway_static_fixture_bytes`: `sizeof` accounting of major fixture objects,
  including backing pointers, rings, routing arrays and histogram, but excluding
  separately reported arena heap storage;
  not measured peak process RAM or complete call-stack accounting.
- `immutable_descriptor_bytes`: descriptor arrays only; excludes constant
  strings, choice tables, codec executable code, SDK memory and OS resources.

Schema fingerprint, allocation-coverage text, workload version, capacities and
metadata semantics are included in each measurement. The runner adds executable
hash, source digest, build configuration, host and sampled process RSS
provenance.

## Build, smoke and persistent baselines

```sh
cmake -S . -B build -DPGW_ENABLE_CAN=ON -DPGW_BUILD_BENCHMARKS=ON \
  -DPGW_BUILD_TESTS=ON -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_JREHOME="<installed Java 17 runtime>"
cmake --build build --target pgw_can_benchmark
ctest --test-dir build -R '^REQ_BENCHMARK_CAN_' --output-on-failure

python3 benchmarks/runner/run.py build/benchmarks/can/pgw_can_benchmark \
  --batches 100000 --timing 1 --metadata 1 --repeat 3 --results results/can
```

Use the returned JSON path as the baseline for a matching second run:

```sh
python3 benchmarks/runner/run.py build/benchmarks/can/pgw_can_benchmark \
  --batches 100000 --timing 1 --metadata 1 --repeat 3 --results results/can \
  --compare results/can/RETURNED_BASELINE.json
```

Capture a matching disabled-mode baseline separately:

```sh
python3 benchmarks/runner/run.py build/benchmarks/can/pgw_can_benchmark \
  --batches 100000 --timing 1 --metadata 0 --repeat 3 --results results/can
build/benchmarks/can/pgw_can_benchmark 1000 1 0
```

The common runner names this workload's result files `can-<timestamp>.json`;
the measurement `workload` identifies the workload version unambiguously.
Only compare matching workload/version, batch count, timing/coverage, metadata
mode, build configuration and host. Capture-enabled and disabled modes are
intentionally marked noncomparable for like-for-like regression checks; paired
runs still quantify capture overhead as an observational experiment. Metadata
mode is fixed at initialization, never toggled during a measured run.

Record paired timing-enabled/counter-only runs to quantify observer overhead,
but do not interpret their runner comparison as like-for-like performance.
Zero runtime allocations, bounded queues, exact outcomes and baseline-bit
preservation are gates. Throughput and latency are observations; there are no
invented hard realtime deadlines or quantitative regression thresholds.

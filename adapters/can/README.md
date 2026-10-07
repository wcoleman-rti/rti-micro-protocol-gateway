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

<img src="../../docs/assets/rti-logo.png" alt="RTI logo: Your systems. Working as one." width="220">

# CAN adapter

Targets: `PGW::adapter_can`, `PGW::can_memory`, and, on Linux,
`PGW::can_socketcan`. The adapter uses `PGW::core`/Micro sequences and the
native signal contract, **not** DDS entities or generated DDS types.
Public `PGW_Sample` and `PGW_Connection` remain incomplete. Concrete storage uses
owner-specific private tags such as `PGW_CANSample` and `PGW_CANConnection`;
opaque references are cast back only inside their owning adapter.

## Initialization and bindings

Construct a `PGW_CANConfig` with an immutable mapping, named categories with
their exact `PGW_Schema` fingerprints, transport, receive-frame budget, and
maximum writer batch. Call `PGW_CAN_storage_size` for a conservative arena
bound, then `PGW_CANAdapter.create`. Configuration tables and callback/context
objects must outlive the connection. Failed arena provisioning restores
`arena.used`; no heap fallback exists.

For strict C11 effective-type correctness, back an arena holding private
adapter objects with fixed-size `malloc`/OSAPI-allocated storage obtained during
initialization, or genuinely matching typed storage. A declared
`unsigned char[]`, including one inside a `max_align_t` union, addresses
alignment but does **not** make typed private-object construction portable.
The adapter does not allocate the backing block itself; the owner frees it
after all loans are returned and the connection is closed. No fallback/growth
is permitted after initialization.

The generated codec can be bridged without copying its tables:

```c
static PGW_CodecStatus decode(void *unused, size_t index,
    const uint8_t *data, size_t length, PGW_Signal *out,
    size_t capacity, size_t *count)
{
    const PGW_MessageDescriptor *m = &PGW_codec_messages[index];
    (void)unused;
    return PGW_codec_decode(m->frame_id, m->extended, m->fd,
        data, length, out, capacity, count);
}

static PGW_CodecStatus patch(void *unused, uint32_t id,
    const PGW_Value *value, uint8_t *data, size_t length)
{
    PGW_Signal signal = {id, *value};
    (void)unused;
    return PGW_codec_patch(&signal, data, length);
}
```

Adopt the generated immutable tables as real Micro typed-sequence views:

```c
PGW_CANConfig config = {0};
PGW_Schema schema = {PGW_codec_schema.name, PGW_codec_schema.version,
                     PGW_codec_schema.fingerprint};
PGW_CANCategory categories[2] = {
    {"powertrain", 4, &schema}, {"auxiliary", 3, &schema}
};
PGW_CANCategorySeq category_sequence;
if (!PGW_CANCategorySeq_initialize(&category_sequence) ||
    !PGW_CANCategorySeq_loan_contiguous(&category_sequence, categories, 0, 2)) {
    /* handle initialization failure */
}
if (!PGW_CANCategorySeq_set_length(&category_sequence, 2)) {
    /* handle invalid capacity */
}
PGW_Status status = PGW_CANMapping_initialize(&config.mapping,
    PGW_codec_messages, PGW_codec_message_count,
    PGW_codec_signals, PGW_codec_signal_count, NULL, decode, patch);
if (status == PGW_OK)
    status = PGW_CANConfig_set_categories(&config, &category_sequence);
```

These helpers only initialize/attach sequence descriptors; they do not copy
immutable codec definitions, grow buffers or alter the SDK-free generated codec
ABI. `PGW_CANConfig_finalize` detaches/finalizes the caller's views after
connections close. Each connection creates independent native sequence
descriptors over the same immutable tables, rather than copying REDA headers.
Custom codecs must obey their bounded output capacity, report exact
active-signal counts, and never allocate or block. The bundled generated codec
validates type, range, finite doubles, inverse scaling/nearest rounding and
multiplexing before modifying bytes.

Each category has one serialized reader and writer. A writer binds one immutable
source representation before runtime. Binding requires an exact logical schema,
versioned `copy_value` access and synchronous copying into `PGW_Signal`. It does
not cast foreign private samples or require their storage layout to match.
Rebinding to a different descriptor is rejected, preventing two routes from
silently changing each other's access callbacks.

## Receive, loans and metadata

Each connection owns one receiver thread. It receives at most the configured
frame budget per receiver turn, then decodes and demultiplexes frames into the
existing bounded typed category queues. When a reader queue may contain data,
the adapter invokes that reader's registered listener; the callback only
signals session readiness. The session worker performs the bounded read and
route dispatch. The adapter does not require application polling or a second
sample queue.

Queue publication, bounded reads, re-arming, listener registration and
unregistration are synchronized with the receiver. If a read leaves category
samples queued, the adapter notifies again before returning. Unregistration
quiesces any in-flight receiver notification before the session can finalize
its callback context. Unknown, malformed, control/error and echo frames still
consume the receiver's frame budget, before signal expansion/demultiplexing.

Configured valid frames are decoded into fixed scratch storage and become the
message baseline even when category queues overflow. Newly decoded signals
drop individually when their own category is full; unrelated categories remain
usable. Inactive multiplex branches are absent, not fabricated zero updates.
Unconfigured frames are counted and ignored. RTR/error frames are counted
diagnostic/control inputs, never DBC payloads.

Loan slots are distinct from RX queues, so the receiver cannot overwrite
borrowed samples. Readers require an initialized sequence with the caller/core's
fixed reference array already attached and fill within that capacity.
Exactly one `return_loan` is
required after `PGW_OK`; `PGW_NO_DATA` has no loan. Double borrowing/returning
and closing with an outstanding loan fail explicitly. Return releases the
private sample loan and clears length; it never unloans the caller-owned pointer
array. The caller unloans/finalizes its sequence at shutdown.

Private sample wrappers retain decoded value, frame ID/format, interface index
and any supplied receive timestamp for their loan lifetime. `copy_value`
exposes only the logical signal. Optional `source_timestamp` exposes the
transport receive timestamp, **not** a CAN sender-generated timestamp.
SocketCAN's kernel software RX timestamps use realtime seconds/nanoseconds;
they are marked portable, not claimed synchronized or hardware arrival times.
The memory backend preserves explicitly injected timestamp validity/clock
meaning; absence stays absent. No CAN source identity or native sequence is
invented. Writers ignore all input metadata and transmit logical values only;
CAN cannot express DDS identity/timestamps. No preservation option is advertised.

Set `PGW_CANConfig.disable_metadata_capture = true` at initialization to omit
optional timestamp/frame/interface provenance from decoded sample wrappers.
The default is false, preserving normal capture. Logical payloads, queue/loan
capacities and baseline/shadow command behavior are unchanged. Fixed sample
storage is still provisioned and zero-initialized; disabled capture skips
metadata field stores, not the payload or native transport/baseline records.
`source_timestamp` then returns `PGW_NO_DATA` with `valid=false`, never a valid
zero substitute. A destination requiring timestamp preservation must treat this
absence explicitly under its configured policy. Raw received-frame timestamps
remain available through `PGW_CAN_baseline_timestamp` for baseline-age diagnostics.
Configuration cannot be changed while running.

## Command behavior

Commands require a known key belonging to the writer's category, exact union
tag, finite/range-representable value, active multiplex branch and a received
baseline. No zero/default frame, expiry, delayed command queue or retry exists.
Selector changes are rejected by the generated codec.

Within one bounded writer call, updates stage and coalesce into one send per
message. Each patch starts from the latest successful TX shadow or newest RX
frame and preserves unrelated bits. An invalid command does not invalidate
other commands. A scratch candidate prevents a failing patch from modifying
valid staging. On successful local transport acceptance, the shadow updates;
on backpressure or I/O failure, affected samples receive their respective
per-sample outcomes and staging is discarded. Failed values cannot leak into
later sends. A fresh RX frame supersedes the TX shadow.
The caller/core sets `PGW_WriteResultSeq` length to the input sample count
before dispatch. The writer rejects mismatched lengths without resizing the
outcome collection or sending a frame; it fills typed reference slots only.

Multiple accepted commands for the same key in one batch are applied in input
order (last wins). Acceptance means the local send mechanism accepted the
coalesced frame, **not** physical delivery or acknowledgment.

Baselines persist indefinitely. `PGW_CAN_baseline_timestamp` reports the original
latest receive timestamp even after successful sends, allowing callers to
observe age in a matching clock domain. There is intentionally no freshness
rejection. Stale unrelated bits are a real application safety risk.

## Transports and observability

Memory RX/TX rings use caller-provided arrays and serialized single-owner
operations. Injection intentionally accepts malformed records for testing.
Full RX rejects the new record and increments `rx_overflow`; full TX increments
`tx_backpressure` and returns immediately. `receive_failure`/`send_failure`
inject deterministic failures. Only transport is replaced, not DDS.

SocketCAN explicitly opens the requested interface with nonblocking/CLOEXEC
flags. Initialization enables FD when requested, receive timestamps, kernel
RX-overflow ancillary records, optional error frames, and up to 64 static raw
filters. Filter IDs/masks use Linux raw CAN flag encoding; no filters selects
the kernel default (all data frames). FD-disabled sockets reject FD sends.
Classic/FD MTUs, standard/extended IDs and BRS/ESI flags are handled explicitly.
Default own-message reception is off; if enabled, `MSG_CONFIRM` records mark
own TX echoes and the adapter excludes them from publication. This does not
prevent loops between independent gateways.

Socket `last_errno`, RX/TX error totals and the latest cumulative kernel
`SO_RXQ_OVFL` counter remain observable. Overflow is reported only when the
kernel supplies ancillary data; this is not complete physical CAN loss
measurement. `EAGAIN`/`EWOULDBLOCK` and TX `ENOBUFS` are nonfatal backpressure.
Other socket errors return explicit status and preserve errno.

`PGW_CAN_stats` separates frames, decoded signals, dropped signals, unknown/
malformed/echo frames, missing baselines, invalid commands, accepted commands,
backpressure and I/O failures. `PGW_CAN_counters` exposes core-compatible atomic
counters independently of logging. Snapshot stats are single-owner observations,
not a cross-thread API. Diagnostic events 1001/1002 denote RX/TX errors,
1003 category overflow, 1004 invalid command batches and 1005 backpressure
batches. Events aggregate within calls; event-ring overflow cannot block routing.

## Resource accounting

All mutable adapter objects use the arena once at initialization:

- one connection and one endpoint state per category;
- three fixed 64-byte-payload transport records per message (received baseline,
  successful-send shadow, staging), plus validity/staging flags;
- one decoded-signal scratch slot per mapped signal;
- one message-index result sidecar per maximum writer batch;
- per category, `capacity` queued private samples and separate loan samples.
  Caller/core-owned opaque reference slots are accounted separately.

`PGW_CAN_storage_size` includes the private category ring slots, private loan
slots, and worst-case alignment padding. The memory transport's caller-owned
RX/TX ring slots are `PGW_CANFrameSeq` loans and their bytes are separately
provisioned by the harness; memory ring occupancy remains head/count state.
`PGW_CANMemory_finalize` detaches/finalizes those native sequence views at
shutdown, dropping any remaining queued frames. `stats.mutable_bytes` records
actual arena consumption. `queue_high_water`
is the largest occupancy of any category queue, not aggregate occupancy.
Immutable descriptor/string/choice tables, externally owned transport rings,
SocketCAN kernel queues and SDK infrastructure are separate resources. Runtime
paths perform no allocation, libc formatting or blocking I/O.

## Collection inventory: actual Micro sequences and justified exclusions

Every sequence below embeds its own actual installed Micro native typed
sequence, declared and instantiated directly by the owning component with
RTI's installed `reda_sequence_decl.h` and `reda_sequence_defn.h` templates.
Fixed buffers are attached during initialization. Runtime changes only bounded
logical lengths; allocating copies, growth and fallback are absent.

| Collection | Representation / behavior |
|---|---|
| Mapping message definitions | `PGW_CANMessageDefinitionSeq`, readonly borrowed generated descriptors |
| Mapping signal definitions | `PGW_CANSignalDefinitionSeq`, readonly borrowed generated descriptors |
| Configured category definitions | `PGW_CANCategorySeq`, readonly borrowed typed configuration records |
| Connection endpoint/category catalog | Private `PGW_CANCategoryStateSeq`, fixed complete endpoint state records |
| Received baselines, successful shadows, batch staging | Private `PGW_CANMessageSeq`, one record per message with three distinct frame-role fields |
| Decode scratch values | Private `PGW_CANDecodedSeq`, fixed maximum signal expansion, active decoded length |
| Write-to-message result sidecar | Private `PGW_CANIndexSeq`, fixed batch bound and current input length |
| Per-category RX ring slots | Private `PGW_CANSampleSeq`, typed fixed backing slots with separate head/count occupancy |
| Memory transport RX/TX ring slots | `PGW_CANFrameSeq`, typed caller-owned backing slots with separate head/count occupancy |
| Private borrowed sample slots | Private `PGW_CANLoanSeq` per category, current loan length reset on return |
| Opaque sample references | Core `PGW_SampleSeq`; caller-owned fixed pointer slots, adapter never unloans them |
| Synchronous per-sample outcomes | Core `PGW_WriteResultSeq`, passed directly to the writer instead of pointer/capacity arguments |
| SocketCAN configured filter catalog | `PGW_CANSocketFilterSeq`, readonly borrowed definitions; kernel API conversion remains bounded |
| Benchmark routes/results/references | Core `PGW_RouteSeq`, `PGW_WriteResultSeq`, `PGW_SampleSeq`, adopted by `PGW_Session_set_routes` / `PGW_Route_initialize_storage` |

Deliberate exclusions:

- Memory transport RX/TX and per-category RX queues are **true circular FIFO
  queues** with head/wrap/full/drop semantics, not plain vectors. Their storage
  is a typed native sequence; its logical length covers the fixed slot array,
  while head/count describe live FIFO occupancy. Replacing the FIFO semantics
  with linear sequence ordering would change overflow/order behavior.
- CAN payload byte arrays and length/DLC describe a native wire record, not
  object catalogs. Preserve the fixed 64-byte payload and protocol lengths.
- Pure generated codecs, message/signal/choice definitions and their
  immutable array/count symbols retain their SDK-free ABI. Adapter-owned
  sequence views wrap these tables without introducing Micro into the codec.
- Generated decode output and raw input bytes retain pointer/length/capacity
  arguments at that pure-codec ABI boundary; native sequence buffers/maxima
  supply the adapter's bounded views.
- Linux `setsockopt` filters and `recvmsg`/`send` use vendor/kernel-native
  buffer/byte-count interfaces, not an invented sequence ABI.
- Arena byte budgets, session sample budgets, maximum batch limits, scalar stream
  capacity declarations, strings and callback context pointers are not
  pointer-plus-cardinality collections.

No other maintained CAN catalog, staging table, scratch vector or stored loan
vector retains an independent raw pointer/count/capacity collection. Sequence
descriptors are included in actual arena footprint and conservative sizing.

## Verification and deliberate gaps

Build/run the requirement tests from the project:

```sh
cmake -S . -B build -DPGW_ENABLE_CAN=ON -DPGW_BUILD_TESTS=ON \
  -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build --target pgw_can_test pgw_can_socket_test
ctest --test-dir build -R '^can\.' --output-on-failure
```

The selected Python must have the generator's pinned cantools dependency.
Tests generate real codecs from the example DBC; no runtime Python is used.

| Requirement | Test / assertion |
|---|---|
| CAN-GOLDEN / CAN-COALESCE | Exact Intel/Motorola signed decode and coalesced patch bytes; unrelated bits preserved |
| CAN-BASELINE | Prebaseline command rejected; RX replaces successful shadow even during category overflow |
| CAN-BUDGET / CAN-DROP | Unknown frame consumes receive budget; bounded queues count exact dropped signal expansion; RX/TX ring overflow |
| CAN-MUX | Extended FD active branch decoded; inactive command and selector changes rejected |
| CAN-PARTIAL | Mixed valid/invalid samples and batches spanning two messages have exact separate accepted/backpressure results |
| CAN-RETRY | Backpressure and I/O failure do not contaminate next successful shadow |
| CAN-INVALID | Malformed sizes, RTR/error, wrong tag, NaN/Infinity, range and unknown keys rejected |
| CAN-LOAN / CAN-METADATA | Caller-owned attached sequences remain attached after return, double loans/returns, close guard, captured receive timestamp |
| CAN-METADATA-DISABLED | Valid payloads and partial command results with capture disabled; requested timestamp access explicitly returns NO_DATA |
| CAN-NOALLOC | First traffic, overload and failures under malloc/calloc/realloc/aligned_alloc link interception, with a deliberate coverage probe |
| CAN-SOCKET-ERROR | Nonexistent interface, closed-fd RX/TX errno, invalid ID/filter count; no CAN traffic |

Live SocketCAN traffic, kernel echo/overflow behavior and real FD transmission
remain dedicated-interface integration checks, not simulated passes. These
tests never open shared `vcan0`, mutate an interface or send physical traffic.
Successful isolated-vcan traffic remains an external integration requirement;
see [`docs/vcan-integration.md`](../../docs/vcan-integration.md) for setup and
the verification boundary.
There is no general CAN conformance, physical loss, J1939, ISO-TP or WCET claim.
Allocation interception covers linked libc entry points in the memory gateway
path, not kernel queues, every SDK/private allocator, or whole-process behavior.

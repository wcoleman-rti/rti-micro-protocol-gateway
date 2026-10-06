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

# Core contracts

`PGW_Sample` is incomplete. Only a negotiated representation accessor may
interpret a sample. Schema name, version and fingerprint must all match.
No payload/metadata tuple, per-sample operations table, forwarding queue or
runtime discovery is part of the routing core.

Readers fill the route's fixed `PGW_SampleSeq` pointer array. `OK` creates an
adapter loan, including an empty sequence; `NO_DATA` does not. Every successful
read has exactly one adapter `return_loan`, even when writing fails. The
pointer-array association lasts until service finalization; native sequence
unloan is **not** a protocol loan return. Writers cannot retain references.
Returning a loan ends all sample and captured metadata lifetimes.
`PGW_Route_initialize_storage` accepts preinitialized typed sample-reference
and outcome sequences before service initialization. It adopts views of their
fixed storage; writers receive `PGW_WriteResultSeq`, not a separate
pointer/count result span. The core sets its logical length to the sample count.

Routes synchronously consume one bounded batch per selected step. `route_budget`
limits visited routes, `sample_budget` limits each read, and round-robin cursor
movement includes faulted/empty routes. Backpressure and invalid commands drop
the affected samples; fatal read/write/return failures fault that route with
`PGW_Error` context. Other routes remain runnable. Callers receive the error and
must not assume service-wide shutdown or retry.
Route faults have their own counter, distinct from per-sample fatal write
outcomes; faulting a route does not double-count a rejected sample.
`PGW_Route_pause` excludes a ready/running route from routing without changing
the service's round-robin schedule; `PGW_Route_resume` makes it eligible for
the next step. Both are synchronous and allocation-free. Repeating either
action returns `PGW_NO_CHANGE`, allowing a control layer to avoid publishing
duplicate state changes. A faulted route cannot be resumed.

With `PGW_ENABLE_REMOTE_CONTROL`, a service may freeze a borrowed control
resource catalog and nonblocking endpoint before initialization. A step takes at
most `PGW_CONTROL_MAX_COMMANDS_PER_STEP` commands, applies explicit actions,
publishes results with source correlation metadata, then visits routes. Route
actions use the same pause/resume operations above; adapter actions use optional
versioned operation tables and are checked against their declared capability
mask. Initialization seeds one state snapshot per selected resource. Failed
state writes leave only the latest snapshot dirty; a step retries at most one
dirty resource. Failed result writes are counted and are not retried, so they do
not imply controller delivery. `PGW_Service_control_counters` copies the
bounded local counters without adding another DDS endpoint. When the feature is
compiled but no control catalog is configured, service stepping performs no
control reads or writes.

Storage comes from caller arrays or a checked, initialization-only arena.
There is no runtime resizing or fallback allocator. Interface ABI versions,
required callbacks, capacities, duplicate registrations and exact schemas are
validated before READY. Registries are frozen by the embedding application
after static registration.
Adapter/binding registries and route catalogs are `PGW_AdapterSeq`,
`PGW_RepresentationSeq` and `PGW_RouteSeq`. Their initialization functions
accept native typed sequence views over caller-provisioned storage.
Registry registration and traversal
use native capacity/length/reference APIs; no duplicate gateway count/capacity
fields are maintained. `PGW_Service_set_routes` adopts the fixed typed route
sequence storage.
Service finalization detaches the catalog and each route's sample/outcome views;
reinitialization explicitly adopts storage again.
`PGW_core_resource_report` sums actual object sizes (including internal padding),
fixed pointer/result arrays and optional diagnostics storage with checked
arithmetic. Separately supplied allocations must each satisfy their type's
alignment; a combined arena also accounts for alignment gaps. Adapter sample/
metadata pools, constant codec tables, middleware memory and thread stacks are
separate resources, not concealed in this core report.
For typed opaque-object construction, arena backing should be a fixed
initialization-time allocation with no declared type, or a properly typed pool.
A declared byte array is useful for byte storage/address sizing but is not a
portable C11 substitute for arbitrary private struct objects.

## Diagnostics

Lock-free unsigned counters remain independent of event publication. Values
wrap modulo the host `uint_fast64_t` width (the supported host uses 64 bits).
Snapshots are individually atomic, not transactionally simultaneous.
The bounded event ring uses a non-waiting atomic try-lock: full-buffer and
contention drops have independent counters. There are no retry loops and no
recursive diagnostic events. Severity filtering does not filter operational
counters. Event time validity is separate from its numeric timestamp. The
service's observation-clock callback is fallible and never substitutes zero
when it fails. If a clock-backed event cannot be timestamped, the step returns
`PGW_IO_ERROR` while preserving already-updated route counters and loan
behavior. CAN-native timestamps remain separate sample metadata.
Optional initialization-time event rate configuration bounds publication within
a monotonic-clock window and counts rate drops separately. Producers must use
the same observation clock; protocol source timestamps are not event clocks.

Caller-owned snapshots, event drains and JSON buffers allocate nothing.
Truncated JSON returns false with the required length; never publish its
partial output as a valid record. Local file/console consumers belong outside
the routing thread. Regular files may block despite `O_NONBLOCK`.

`PGW::diagnostics_local` provides a caller-buffer JSON Lines snapshot sink.
Initialize it with an already opened descriptor before RUNNING; ownership stays
with the caller. A full nonblocking descriptor drops the new record with explicit
backpressure. Truncation, short writes and I/O errors return failure and update
sink counters without recursive events. Broken pipes return an I/O error:
the sink blocks/consumes only its newly generated SIGPIPE on the calling thread
and restores that thread's mask without changing process-global handlers.

## SDK boundary

Sequences instantiate installed Micro REDA templates without copying SDK
implementation source. Only the fixed-buffer subset is exposed. Core links
Micro infrastructure/OSAPI, not discovery/Appgen or DDS entities. This subset
and its ABI require a separate audit against an actual Connext Cert release.

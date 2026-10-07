# Event-driven session runtime

## Public execution model

A service contains explicitly configured sessions and routes. Each session
owns a `PGW_AsyncWaitSet`: a Micro DDS
`WaitSet`, one serialized OSAPI worker thread, a shared lifecycle/control wake
condition, and one readiness condition per route. Service initialization
attaches the conditions and registers every input reader listener. Service
start launches one worker per session. Stop signals each session and joins all
workers; service finalization unregisters listeners, detaches conditions, and
destroys the dispatcher. Adapters own connections and typed endpoints and
destroy them only after service finalization.

Each route has exactly one consuming reader. A reader endpoint cannot be shared
by routes because both would compete for the same samples. Writers may be
shared, including across sessions, only when their shared endpoint state and
binding operations are synchronized by the adapter. The core validates shared
writer representation compatibility during initialization.

## Reader notifications and readiness

Each typed reader implements `register_listener` and `unregister_listener`.
The listener's `on_data_available` callback is a thread-safe, nonblocking,
allocation-free signal only: it does not read or take samples, route data, or
wait for the session worker. A notification sets that reader's route-owned
guard condition; the `PGW_AsyncWaitSet` dispatches the handler associated with
each active route condition. The separate shared wake condition is reserved
for stop, route-state, and control work.

DDS bridges its `DDS_DATA_AVAILABLE_STATUS` listener to the route condition.
Unread samples remain in the DataReader history; there is no mirrored queue.
CAN uses one receiver per connection to decode frames into the existing
bounded typed category storage and notify the affected readers. Producer,
consumer, queue, and listener operations are synchronized; CAN does not insert
a second sample queue.

Conditions are coalescing readiness signals, not sample counts. Before
dispatching an active route, the worker clears its route condition and consumes
the coalesced pending flag. A racing notification re-triggers the condition
for the next wait. Adapters must re-notify before returning from a read if they
know input remains. CAN checks its queue occupancy; the DDS adapter
conservatively re-notifies when a typed take fills its bound, which can cause
one harmless empty read when the history was exactly drained. The core does
not infer unread transport data from a full batch. This protocol prevents data
from being stranded without generating adapter-independent spurious reads.
Each wait result dispatches at most one sample-budget batch for each active
route in rotating order. A route that remains ready cannot monopolize the
worker: the starting route cursor advances after every wait result.

## Sample ownership and backpressure

`read` returns opaque `PGW_Sample` references and, on success, owns one loan
until `return_loan`. The core forwards each reference synchronously to its
writer while the loan is active, then returns the loan exactly once, including
when writing or outcome processing fails. Writers must not retain sample
references or payload pointers beyond their call. The core does not inspect,
normalize, or copy payloads.

The configured sample budget, caller-provided route storage, and endpoint
capacity bound every read. An adapter's bounded input storage defines ingress
backpressure. If it is full, the adapter drops or rejects input according to
its documented policy and updates its counters; storage does not grow
dynamically. A writer reports acceptance, backpressure, invalid data, or fatal
failure per sample. Samples reported as backpressured are consumed from that
loan and counted; the core does not retain or replay them.

`PGW_TypeInfo` carries a static logical type identity (name, version, and
fingerprint), not a field-level schema. `PGW_SampleRepresentation` describes
the local C sample layout and access/view contract. Generated DDS
operations and conversion glue live in a `PGW_DDSTypeBinding`, not in either
core descriptor. Typed endpoint instances compose a reusable type binding with
adapter-owned connection, entity, and loan state.
Cross-schema conversion is explicit in the selected binding, never implicit in
the core.

## Lifecycle, control, and timers

Configuration, session membership, endpoint binding, listener registration,
condition attachment, and `PGW_AsyncWaitSet` construction complete before
workers start. Stop first marks each session stopping, signals its shared wake
condition, and joins its worker. Finalization then unregisters/quiesces input
listeners before detaching and destroying route conditions and the underlying
WaitSet; adapters may close readers and connections only after this completes.
Listener context points to route/session storage that remains alive through
synchronous unregistration, so no callback can access a finalized session.

Route pause/resume is synchronized with dispatch. A pending event for a paused
route remains pending and is re-signaled when the route becomes eligible.
Counters and shared writer state support concurrent snapshots/writes according
to their atomic/adapter synchronization contracts. A route fault disables only
that route and is surfaced through its error and diagnostics.

When remote control is enabled, its command reader listener is registered with
the explicitly selected control session and signals the shared wake condition.
The session worker processes bounded command batches on wake. Telemetry
deadlines and dirty-state retries use timed `WaitSet` waits.

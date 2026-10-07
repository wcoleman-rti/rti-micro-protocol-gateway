# Event-driven session runtime

## Public execution model

A service contains one or more explicitly configured sessions. Every route
belongs to exactly one session; there is no implicit session, manual stepping
mode, or periodic runner. Each session owns one DDS `WaitSet`, one wake
`GuardCondition`, and one serialized worker thread. Service initialization
creates the session conditions and registers every input reader listener.
Service start launches one worker per session. Stop signals each session and
joins all workers; service finalization unregisters listeners before releasing
session conditions. Adapters own connections and typed endpoints and destroy
them only after service finalization.

Each route has exactly one consuming reader. A reader endpoint cannot be shared
by routes because both would compete for the same samples. Writers may be
shared, including across sessions, only when their shared endpoint state and
binding operations are synchronized by the adapter. The core validates shared
writer representation compatibility during initialization.

## Reader notifications and readiness

Each typed reader implements `register_listener` and `unregister_listener`.
The listener's `on_data_available` callback is a thread-safe, nonblocking,
allocation-free signal only: it does not read or take samples, route data, or
wait for the session worker. A notification records readiness in a coalescing
per-route pending flag and signals the owning session's core-owned wake
condition. A notification identifies its reader through the listener context;
the adapter does not attach its own conditions to the session `WaitSet`.

DDS bridges its real `DDS_DATA_AVAILABLE_STATUS` listener to this interface.
Unread samples remain in the DataReader history; there is no mirrored queue.
CAN uses one receiver per connection to decode frames into the existing
bounded typed category storage and notify the affected readers. Producer,
consumer, queue, and listener operations are synchronized; CAN does not insert
a second sample queue.

The wake condition is coalescing, not a count of samples. The worker clears it
before draining pending reader flags. Each wake processes pending routes in
round-robin order, with at most one configured sample-budget batch per route
per wake. Adapter reads that reach their bound while data remains must notify
again before returning. A notification racing with a read sets the pending flag
again; the corresponding session wake is observed on the next wait. The
adapter's producer/consumer synchronization and this re-arm rule prevent an
enqueue from being hidden by a condition clear or a bounded read. A route that
remains active cannot monopolize the worker: the starting route cursor rotates
after each wake.

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

Generated strongly typed representations are defined per logical schema. The
shared core `PGW_Representation` descriptor identifies that schema and its
native typed sample access; it is distinct from protocol/type binding
descriptors such as `PGW_DDSBinding`. Typed endpoint instances compose a
reusable binding with adapter-owned connection, queue/entity, and loan state.
Bindings implement conversion to and from the schema's typed representation.
Cross-schema conversion is explicit in the selected binding, never implicit in
the core.

## Lifecycle, control, and timers

Configuration, session membership, endpoint binding, listener registration,
and `WaitSet` construction complete before workers start. Stop first marks each
session stopping, signals its wake condition, and joins its worker. Finalization
then unregisters/quiesces input listeners before destroying the session
condition and `WaitSet`; adapters may close readers and connections only after
this completes. Listener context points to route/session storage that remains
alive through synchronous unregistration, so no callback can access a finalized
session.

Route pause/resume is synchronized with dispatch. A pending event for a paused
route remains pending and is re-signaled when the route becomes eligible.
Counters and shared writer state support concurrent snapshots/writes according
to their atomic/adapter synchronization contracts. A route fault disables only
that route and is surfaced through its error and diagnostics.

When remote control is enabled, its command reader listener is registered with
the explicitly selected control session. The session worker processes bounded
command batches on wake. Telemetry deadlines and dirty-state retries use timed
`WaitSet` waits, so they remain active without reader traffic; no step counter
drives control or telemetry.

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

# Connext Micro adapter

`PGW::adapter_dds_connext_micro` uses the installed Micro 4.3.0 C, Appgen, DPDE,
history, UDP, and OSAPI libraries. It neither creates replacement endpoints
nor interprets unknown discovered types.
The public API is declared in `<pgw/dds/connext_micro.h>`.

It does not complete the public opaque `PGW_Connection` or `PGW_Sample` tags.
Connection/sample storage uses distinct private `PGW_DDSConnection` and
`PGW_DDSSample` types; opaque references are converted back only inside this
adapter's owning callbacks.

Writer callbacks consume the core's typed `PGW_WriteResultSeq`: callers
initialize and loan bounded result storage before use. The adapter sets its
logical length to the input batch and reports each outcome through typed
references, without allocating or substituting a private result array.

For strict C11 effective-type safety, backing storage used to construct private
objects should be allocated storage without a declared type (for example one
fixed initialization `malloc`), not a declared `unsigned char[]` cast to
unrelated structs. The examples reserve their complete arena once, never grow
or fall back, and free it after successful shutdown. The core arena only
computes aligned addresses and cannot change the caller backing's effective
type.

## Initialization and ownership

1. Generate type support with Micro `rtiddsgen`, non-interpreted C.
2. Compile the conventional participant-library XML with MAG in its default
   resource-adjustment mode. Never pass `-dontUpdateResourceLimits`.
3. Call `PGW_DDS_register_model(APPGEN_get_library_seq())` once.
4. Supply immutable `PGW_DDSConfig` and endpoint/type-binding descriptors to
   `PGW_DDSConnextMicroAdapter.create`. The adapter instantiates the named participant
   and adopts named readers/writers.
5. Bind the source representation before writing.

The built-in immutable descriptors `PGW_DDSConnextMicroAdapter` and
`PGW_DDSConnextMicroConnection` are public. `PGW_DDS_register_adapter(registry)` registers
the adapter in the core's static registry without creating DDS entities.
Applications can construct through the registered adapter callback or the typed
`PGW_DDS_create(config, arena, out)` convenience function; both share the same
initialization and ownership implementation.

The factory QoS comes **directly from MAG's public model**, before the factory's
first pool use. Without this step the SDK's default single-participant pool can
prevent creating a second configured participant. All selected model factory
QoS must agree; conflicting process-global policies are rejected, not merged.
Registration is idempotent for the same model and rejects a different model.
An embedding application must coordinate the singleton factory and registry.
The adapter does not replace the application's global middleware logger.

Connections own their MAG-instantiated participant. Closing deletes its
contained entities and participant, but refuses an outstanding loan. Caller
arena storage remains caller-owned. Successful initialization freezes all
gateway buffers; failures unwind the participant and restore the arena mark.
The embedding application owns final process-global middleware shutdown.

## Typed plugins and loans

`PGW_DDSBinding` is a compiled dispatch table, independent of CAN. The example
generates its per-type table and the repeated sequence, typed `take`,
`return_loan`, and write wrappers from RTI type XML. Its bounded typed state and
scratch are initialized from the caller's arena. The application declares the
canonical representation and selects fieldwise generation or explicit
conversion/key callbacks.

This dispatch table complements rather than duplicates RTI's generated type
support. `rtiddsgen` provides the concrete DDS type and type-specific APIs; it
does not define the gateway representation. The generated table lets the
generic connection code call different generated types through one interface.
Fieldwise scalar conversion and simple key registration can be generated;
union validation and application-specific mappings remain explicit callbacks.
See [developing adapters and DDS bindings](../../../docs/developing-adapters.md)
for the extension workflow.

The current example bindings use pointer-free fixed scalars, arrays, and the
signal tagged union. A binding with heap-owned strings/collections would need
an explicit destruction contract; that extension is not currently supported.
Unknown bindings, wrong registered type names, wrong named endpoint roles,
nonfinite/unbounded histories, or nonzero writer blocking time are rejected.

Readers receive the core's **already attached fixed reference sequence**.
They fill its slots, retaining a separate typed middleware loan and private
wrappers. Every successful read must be followed by exactly one adapter
`return_loan`; generic `PGW_SampleSeq_unloan` is never a DDS loan return.
NO_DATA has no public loan. References and adapter metadata expire at return.
Multiple readers cannot adopt the same native endpoint.

Lifecycle-only SampleInfo is counted separately and returned to DDS without
becoming a CAN command. `PGW_DDS_statistics` also retrieves supported public
loss/rejection/matching/incompatible-QoS totals. It does not claim to count
every KEEP_LAST replacement. Adapter statistics are caller-serialized with
the cooperative worker; middleware status getters provide the DDS-owned side.

## Metadata and writes

`PGW_DDS_metadata` copies only public source/reception timestamps, scoped
publication sequence/handle, instance state, and validity from a live DDS loan.
Publication identity/sequence and reception time are observation provenance,
not replayed outgoing DDS identity.

Normal writes use the destination writer's ordinary timestamp/identity.
`preserve_source_timestamp=true` requires a source timestamp accessor at bind
time. Every requested timestamp must be valid, marked portable, have seconds
in `0..INT32_MAX` and nanoseconds below one billion. Missing, monotonic-only,
negative, oversized, or SDK-rejected values yield `PGW_WRITE_INVALID`, never
silent clamping/regeneration. Accepted values use generated
`write_w_timestamp`. Preserving bindings register known keys at epoch before
RUNNING so preregistration's ordinary current time does not preclude an earlier
portable source timestamp. DDS-native timestamps are wire-protocol timestamps;
this is not a guarantee that remote clocks are synchronized.

Local DDS acceptance is not an acknowledgment. Timeout/resource exhaustion
maps to per-sample backpressure, invalid arguments/preconditions to invalid
input, and other SDK failures to fatal results. The adapter retains no samples
for retries and does not block on reliable delivery.

## Resources and limits

`PGW_DDS_effective_resources` reports actual participant/factory QoS getters,
including topology/discovery allocations and gateway arena bytes.
`PGW_DDS_effective_history` reports actual instance/history/sample/blocking
values. Call QoS/resource reporting during initialization; these getters copy
SDK QoS structures and are not a whole-process allocation-free guarantee.

MAG remains the sole topology-sizing authority. The host compiler derives
only signal key cardinality from the mapping inventory. XML allocation maxima,
not initial/incremental allocation counts, are the relevant Micro pool limits.
Entity counts are not a DDS RAM estimate, and the declared gateway/companion
inventory does not provide arbitrary DPDE peer headroom.

See [the runnable example](../../examples/can_dds/README.md) for verified paths
and explicit coverage gaps.

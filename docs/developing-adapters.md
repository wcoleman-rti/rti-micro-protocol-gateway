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

# Developing adapters and DDS bindings

Use this guide to add a protocol adapter or DDS binding while keeping transport
code separate from application types.

## Source ownership

Configuration grammars live in `resources/schema/`. The
[common remote-control IDL](../core/control/idl/control_common.idl) is owned
with the core API, while the signal-specific DDS wire contract lives with its
binding in `bindings/signal/idl/`. These are source inputs; generated service
IDL and RTI-generated type support belong in the build tree.

## Separate transport adapters from type bindings

The core routes samples through versioned `PGW_SampleRepresentation` descriptors and
the reader/writer operations of a `PGW_Connection`. A protocol adapter implements
the `PGW_AdapterI` and `PGW_ConnectionI` contracts; its
`lookup_stream_reader`/`lookup_stream_writer` operations return handles to
connection-owned endpoints and do not create those endpoints. It does not define the
application meaning of every sample it transports. See [core contracts](core.md).

`PGW::adapter_dds_connext_micro` is a reusable RTI Connext Micro transport
implementation. Its participants and named DDS entities come from the generated
AppGen model. The adapter is not hard-coded for the signal topic: each
`PGW_DDSEndpointConfig` selects a generated endpoint and a `PGW_DDSTypeBinding`
appropriate for that endpoint's type. The binding is a typed bridge between
RTI Connext Micro and the gateway:

- RTI `rtiddsgen` produces type-specific DDS structures and typed APIs, including
  operations used to take, return, and write that type.
- `PGW_DDSTypeBinding` erases those type-specific calls behind one C callback table
  so the generic adapter can manage heterogeneous endpoints.
  - The type binding also selects the gateway `PGW_TypeInfo` /
    `PGW_SampleRepresentation` contract and connects conversion between the DDS
    sample and that representation. RTI type support does not
    define the gateway representation or conversion policy.
- `PGW_SampleView` lets a destination binding inspect the borrowed source sample
  together with its adapter-specific metadata while the input loan is active.
  Its `PGW_SampleViewDescriptor` declares the exact view shape and identities.
  A `bind-view` callback negotiates that contract against the destination
  binding before `PGW_ENABLED`; a paired `write-view` callback translates the borrowed
  pair directly into the statically typed DDS sample without first copying the
  source into scratch storage. Neither callback may retain borrowed pointers.
  Configure both callbacks for explicit view translation. A cross-schema
  direct-native write is also possible when a binding opts into
  `direct-native-write="true"` and the source and destination use the exact same
  generated DDS type; that path does not require a translator.
  `PGW_UNSUPPORTED` from `bind-view` rejects the route before `PGW_ENABLED`.
  `PGW_UNSUPPORTED` from `write-view` falls back to canonical copy/write only
  when schemas match; on a negotiated cross-schema route it becomes
  `PGW_WRITE_INVALID` for that sample and routing continues.
  For example:

  ```xml
  <type-binding ... bind-view="PGW_signal_bind_view"
           write-view="PGW_signal_write_view"/>
  ```
- Key registration, timestamp-aware writes, native sequence access, and DDS
  loan return are connected to the gateway's lifecycle through the binding.

  The example's type-specific sequence/take/loan/write glue is generated into
  `dds_type_bindings.c` from Micro `rtiddsgen -convertToXml` output and the
  service's `<type-binding>` declarations. `pgw_micro_convert()` schedules this
  conversion at **build time** through RTI's
  `connextdds_rtiddsgen_convert()` helper. Do not parse IDL with ad hoc regular
  expressions or modify RTI-generated support files.

  `conversion="fieldwise"` generates a canonical C struct and scalar
  field-by-field conversion only when the DDS type is a struct of supported
  primitive fields with matching names and compatible C types. The generator
  emits compile-time type assertions and rejects arrays, unions, or unsupported
  fields in this mode. An optional `register-key-value` asks it to register the
  single DDS scalar key with that value; `supports-timestamp="true"` uses epoch
  registration before timestamp-preserving writes.

  Use `conversion="callbacks"` when conversion or validation has semantic
  behavior that cannot safely be inferred. The declaration names the
  `dds-to-native` and `native-to-dds` callbacks; `register-keys` can name an
  application key policy, and `sample-copy` can expose an application-native
  representation as a source. For example, the signal union's discriminant and
  value must be checked against the DBC-derived signal descriptor, while the
  diagnostic snapshot filters the entity kind and maps its counter array to the
  core snapshot. Those policies remain in
  [signal DDS conversion](../bindings/signal/src/dds_conversion.c) and
  [diagnostics conversion](../examples/can_dds/diagnostics_conversion.c);
  generic typed operations are not duplicated there.

Generated fieldwise conversion is bounded in-memory field assignment, not an
extra serialization format. The core still forwards sample references.
Bindings may opt into `direct-native-write="true"` when the same generated DDS
type can be forwarded without semantic conversion. Callback-mode bindings must
also name `validate-native`; the DDS adapter checks that callback before writing
the borrowed sample. The adapter verifies source/destination generated type
identity at bind time and on every borrowed view; schema names and topic names
do not determine this exact generated-type match. Separately generated type
plugins are not assumed to have equivalent C layouts, even if their IDL fields
look alike; use an explicit view translator or schema-compatible canonical
conversion unless they share the exact generated type identity. The direct DDS
writer call still performs the middleware's normal write/history/transport
work.

The `dds.real_gateway` test exercises the real reader/writer path. Optional
route latency metrics report local reader-to-writer batch time; they do not
measure subscriber delivery or establish WCET. See the
[DDS performance guide](dds-performance.md) to run the benchmarks on a target.

## Add a DDS topic or type

1. **Define the wire type.** Keep the source IDL with the binding or protocol
   that owns that wire contract, rather than in the generated build directory.
   For example, [signal.idl](../bindings/signal/idl/signal.idl) belongs with the
   signal binding; DBC generation copies it to `signals.idl` before Micro
   code generation.
2. **Add the static DDS model.** Register the type and define its topic,
   participant, reader/writer entities, and explicit bounded QoS in the XML
   consumed by AppGen. Generate Micro type support from the IDL and AppGen
   entities from the model. Use the repository's
   [`pgw_micro_codegen` and `pgw_micro_appgen` helpers](build.md#generator-helpers);
   do not hand-edit generated files.
3. **Declare the binding.** Add a service `<type-binding>` with its DDS type,
   canonical schema/version/fingerprint, native C type, support header, and
   conversion mode. Use fieldwise generation for compatible scalar structs;
   otherwise provide conversion callbacks and key policy only for the semantic
   work the inputs do not describe. Generated per-type code owns sequence
   access, `take`, matching loan return, writer calls, state allocation, and the
   `PGW_DDSTypeBinding` instances.
4. **Connect the configured endpoint.** A `PGW_DDSConfig` names the registered
   AppGen participant; each `PGW_DDSEndpointConfig` names a generated reader or
   writer and points to its binding. The service XML connects those endpoint
   names to binding symbols and routes, as in the
   [CAN/DDS example](../examples/can_dds/gateway.xml.in). Ensure the DDS topic's
   registered type name agrees with the binding's `dds_type_name`.
5. **Verify the target pipeline.** Build with Micro `rtiddsgen` and MAG, then
   test the real reader/writer path. Cover conversion and invalid samples,
   keys, loan lifetime, timestamps if enabled, QoS/history bounds, allocation
   behavior, and the actual target's DDS pool and arena limits. Host-only fake
   adapters do not qualify middleware resources.

Adding another topic to an existing domain usually reuses its participant and
the same Connext Micro adapter; it adds type support, a binding declaration, and
generated endpoints. Fieldwise-compatible bindings need no handwritten C
conversion implementation.
For another domain, add a participant/domain to the AppGen model and configure
the corresponding `PGW_DDSConfig` to use that participant. In the example,
`PGW_EXAMPLE_DDS_DOMAIN` is substituted into generated DDS XML at build time.
The startup-selected domain for remote control is a separate, dedicated code
path; ordinary data-plane connections do not currently have a general
runtime-domain override.

## Add a protocol adapter

1. Implement a versioned `PGW_AdapterI` factory and `PGW_ConnectionI`
   `lookup_stream_reader`, `lookup_stream_writer`, and `close` callbacks for
   the protocol. Convert native samples to and from a declared
   `PGW_SampleRepresentation`; do not expose protocol-native pointers as if
   they were another adapter's sample type.
2. Preserve the core's synchronous loan contract: bound each batch, expose
   samples only for the loan lifetime, return a successful read loan exactly
   once, and report per-sample write outcomes. Provision all runtime state from
   initialization-time caller storage; do not add allocation or unbounded
   retrying to a route step.
3. Add the adapter to CMake and register it in the application's static
   registry. Declare optional remote-control actions and telemetry in its
   versioned `adapter.xml` manifest only if implemented. The manifest is a
   capability declaration, not a substitute for implementing the callback.
4. Extend strict configuration/code generation only for concepts the new
   adapter requires. Add tests for malformed configuration and unsupported
   capabilities, core tests with a fake adapter, and a protocol integration
   test with the real transport.

The CAN and Connext Micro adapters demonstrate the native-protocol and
DDS-entity approaches. For Micro-specific model registration, ownership, loans,
and resource limits, see the [Connext Micro adapter guide](../adapters/dds/connext_micro/README.md).

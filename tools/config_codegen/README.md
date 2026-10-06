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

# Gateway configuration compiler

Install the pinned host dependency into the project's selected environment:

```sh
.venv/bin/python -m pip install -r tools/config_codegen/requirements.txt
```

`config_codegen.py` validates `resources/schema/gateway.xsd` with lxml, then applies
semantic checks before emitting C. The parser rejects DTD/entities, unknown
elements/attributes, duplicate IDs/native endpoints, unresolved participants,
topics/types/bindings/routes, role mismatch, stale mapping fingerprints,
incompatible schemas, unsafe capacities, conflicting timestamp policies,
cyclic/unresolved QoS inheritance, unbounded keyed history, and blocking writers.
KEEP_LAST depth one is required. Writers allow one physical sample per key;
readers allow one or two, with consistent total capacity. The second reader
slot is bounded loan/replacement storage, not an increased retention depth.

The intentionally small grammar contains compiled bindings, DDS connections
with named endpoint references, external native CAN stream catalogs, and routes.
CAN transports are supplied by the application; the XML never secretly chooses
a physical interface. Generated native capacities, CAN polling/write budgets,
route graph, scheduler budgets, and diagnostics period are consumed by the example.
This is not Routing Service XML or a general runtime configuration loader.

`PGW_ENABLE_REMOTE_CONTROL` is a compile-time option, defaults OFF, and requires
the DDS adapter. With it enabled, the example compiler emits a deterministic
service-specific `Resource` enum and a flattened standalone controller IDL for
the resources explicitly selected in `<control>`. Versioned adapter-library
XML declares capabilities under `<adapter><control>` and individual metrics
under `<adapter><telemetry>`. Capability `actions` are pipe-delimited (for
example, `actions="up|down"`). The manifest is validated against the generated
macros used by each adapter's runtime capability descriptor. The compiler
rejects unlinked adapters, unsupported actions, invalid stream directions,
duplicate selections, and normalized enum collisions. The common control IDL
and `combine_idl.py` preserve include dependency order and reject missing/cyclic
includes. A telemetry topic/writer is generated only when a declared metric is
selected in the service XML. The feature-off build does not invoke control IDL
generation.

`dds_workload.py` substitutes category **key cardinalities** from the generated
mapping inventory into a standard DDS XML template. It also includes/excludes
the management inventory according to the build option. It does not count
local/remote participants, endpoints, matches, or discovery resources.

Probe/management schema fingerprints are generated from their IDL inputs into
both XML and the compiled binding header; the signal fingerprint comes from
the richer DBC/mapping inventory. IDL-to-XML conversion supplies a generated type-declaration cross-check
artifact. MAG 4.3.0 warns and ignores an inline `<types>` section, and rejects
that section in referenced files. Therefore the MAG system input deliberately
omits `<types>` and uses `-idlFile model.idl` with a generated plugin-header
umbrella. Actual compiled type-plugin symbols and registered type-name getters
complete the check. Do not whitelist the unsupported-types warning.
The workload compiler rejects DDS registered-type references absent from the
generated type inventory.

The CMake chain is:

```
DBC/mapping -> inventory + IDL + codecs
IDL -> typed support + type XML
inventory/type XML/templates -> DDS XML + gateway XML
gateway XML + DDS XML + XSD -> compiled PGW configuration
DDS XML -> real MAG model
all generated sources -> statically linked gateway/companion
```

Explicit dependencies include mapping/IDL inputs, tool sources, XML templates,
XSD, configured workload settings, and the persistent JRE-aware RTI launcher.
Normal RTI warnings fail the build; Java/environment exceptions must be specific,
reviewed, surfaced allowlist entries. The verified pipeline uses Java 17.

For DDS bindings, the example runs `pgw_micro_convert(FROM IDL TO XML)` at build
time. `dds_binding_codegen.py` reads that RTI-produced type model and the
generated gateway binding declarations to emit typed Micro reader/writer,
sequence, loan-return, and `PGW_DDSBinding` glue. `conversion="fieldwise"`
generates a canonical scalar struct and matching-name conversion with compile-
time type checks; `conversion="callbacks"` connects explicit semantic mapping
functions. Key-registration values for a single scalar key can be generated;
composite or application-policy keys use a callback. An identical generated
DDS type can opt into `direct-native-write="true"`; callback-based conversion
also requires `validate-native`. The DDS adapter compares generated plugin
identity in the borrowed sample view and calls the typed writer directly,
regardless of schema labels. For cross-schema translation, pair `bind-view`
and `write-view`: bind-view must validate the source representation's static
view contract before READY; write-view translates that borrowed payload/context
to the generated DDS type. Canonical conversion fallback is available only
when the schemas match. Neither path bypasses Micro's own history/serialization
work.

Tests: `config.strict_xml` covers grammar/references/policies/bounds;
`config.actual_mag` runs the actual installed MAG, demonstrates topology-resource
regeneration after an added endpoint, and verifies that warnings for an
unsupported QoS policy are rejected.
Verification also checked that a no-op build preserves generated output
timestamps, and touching only DDS XML regenerates MAG/configuration but leaves
the DBC inventory, IDL, and typed type support unchanged.

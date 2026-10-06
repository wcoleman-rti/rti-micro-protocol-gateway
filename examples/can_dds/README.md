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

# RTI Micro Protocol Gateway with CAN and DDS

The gateway is bidirectional: the real DDS/MAG integration test receives
CAN-state values through RTI Connext Micro and sends a DDS command back through
the CAN adapter, checking the exact patched frame bytes. The runtime executable
supports SocketCAN, but successful vcan or physical socket traffic is not
verified here. Use `--memory` for the safe no-interface demo below.

## Build and run safely

With the installed Micro 4.3.0 SDK and configured Java 17 runtime (see
[host dependency and SDK setup](../../docs/build.md)):

```sh
.venv/bin/pip install -r tools/dbc_codegen/requirements.txt \
    -r tools/config_codegen/requirements.txt
cmake -S . -B build-dds -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
    -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
    -DRTIME_PIL_ARCH="<installed PIL architecture>" \
    -DRTIME_PSL_ARCH="<installed PSL architecture>" \
    -DRTIME_TARGET_NAME="<installed target name>" \
    -DPGW_JREHOME="<installed Java 17 runtime>" \
    -DPGW_ENABLE_DDS=ON -DPGW_ENABLE_CAN=ON -DPGW_BUILD_EXAMPLES=ON \
    -DPGW_WARNINGS_AS_ERRORS=ON -DPGW_EXAMPLE_DDS_DOMAIN=83
cmake --build build-dds -j
ctest --test-dir build-dds -R 'dds\\.|config\\.' --output-on-failure
```

The standard DDS system describes gateway and companion inventories, with
separate powertrain/auxiliary state and command topics, DPDE, loopback-only UDP,
best effort, bounded KEEP_LAST depth 1, and zero writer blocking time.
Signal key counts come from the mapping manifest, not handwritten topology
resource formulas. Generated headers/C remain under the build directory.

CTest takes a project-global per-domain advisory lock and checks the entire
RTPS domain port range before any DDS process starts. Occupied ports cause an
explicit skip (77), not traffic on an existing domain. Domain 83 avoids the
usual Linux ephemeral port range; choose a different unused domain when needed.
The lock coordinates this project's tests, not unrelated applications racing
to open ports after the check. No network-namespace isolation is claimed.

For a **no-CAN-interface** demonstration, run the two executables in separate
terminals on an unused configured domain:

```sh
build-dds/examples/can_dds/pgw_can_dds_gateway --memory 10000
build-dds/examples/can_dds/pgw_dds_companion 123.4 500
```

Memory CAN injects a bounded example Engine baseline every 25 scheduler steps.
DDS is still the real licensed Micro implementation. The companion publishes a
speed command only after receiving a state, and the gateway prints transmitted
memory-CAN bytes after stopping. Expected interaction is visible in the separate
terminal outputs:

```text
# companion terminal
state key=1001 kind=2 value=1000

# gateway terminal, when it stops
CAN memory sent id=256 bytes=d20400000101abcd
```

The companion prints received states but does not print a separate “command
sent” message; the exact patched frame printed by the gateway confirms the
reverse DDS-command-to-CAN path. The gateway's 10,000-step example run takes
about ten seconds; the companion exits after its requested polling steps.

For a real, **explicitly selected** interface:

```sh
build-dds/examples/can_dds/pgw_can_dds_gateway my_owned_can_interface 10000
```

Nothing creates/reconfigures `vcan0`, chooses a physical bus, or sends CAN test
traffic automatically. Creating a dedicated virtual interface/network namespace
is an explicit external prerequisite. The standalone programs do not perform
CTest's port-reservation check: the operator must choose an unused domain.

## Customize CAN signals and DDS topics

### Add or change CAN signals

1. Edit [`example.dbc`](example.dbc) and [`mapping.json`](mapping.json). Every
   DBC signal needs an explicit, stable, globally unique positive `uint32`
   signal ID and a logical category. Do not reuse retired IDs.
2. Reconfigure/rebuild the example. `PGW::binding_signal`'s build-time generator
   validates the documented bounded DBC subset and regenerates the native
   codec, inventory, schema fingerprint and keyed `PGW_DDS::Signal` IDL in the
   build tree. The executable does not parse DBC at runtime.
3. For signals in the existing `powertrain` or `auxiliary` categories, the
   current DDS state/command topics already carry the keyed Signal type. For a
   new category, add its distinct state and command topics/endpoints in
   [`dds.xml.in`](dds.xml.in), then add the gateway writer/reader, companion
   reader/writer, native CAN streams, and directed routes in
   [`gateway.xml.in`](gateway.xml.in). Use the generator-provided
   `PGW_signal_dds_binding_<category>` symbol and matching schema fingerprint.
   The category must be a valid generated C identifier.
4. Keep `capacity` values consistent with the application key/history budget.
   The configuration compiler supplies inventory key counts to the QoS
   template; MAG derives topology resources. Do not hand-edit MAG output or
   duplicate its topology count.

The DBC generator's supported numeric, endian, multiplexing and rejection
rules are in [`tools/dbc_codegen/README.md`](../../tools/dbc_codegen/README.md).
For a different logical DDS type rather than another Signal category, use the
separate IDL/type-binding pattern demonstrated by `Probe`; topic edits alone do
not make an unknown type routable.

### Change the DDS domain or named topics

- Select an unused demonstration domain at configure time, then regenerate and
  rebuild:

  ```sh
  cmake -S . -B build-dds \
    -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
    -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
    -DRTIME_PIL_ARCH="<installed PIL architecture>" \
    -DRTIME_PSL_ARCH="<installed PSL architecture>" \
    -DRTIME_TARGET_NAME="<installed target name>" \
    -DPGW_JREHOME="<installed Java 17 runtime>" \
    -DPGW_ENABLE_CAN=ON -DPGW_ENABLE_DDS=ON -DPGW_BUILD_EXAMPLES=ON \
    -DPGW_EXAMPLE_DDS_DOMAIN=84
  cmake --build build-dds -j
  ```

  `PGW_EXAMPLE_DDS_DOMAIN` substitutes the `@domain@` placeholder in the
  example DDS XML before MAG runs. Use a fresh value and check for conflicts;
  the standalone programs do not run CTest's socket-range reservation check.
- Add/rename the standard DDS topic and named publisher/subscriber endpoint
  definitions in `dds.xml.in`. Update each corresponding `endpoint` reference
  in `gateway.xml.in` using `PublisherName::WriterName` or
  `SubscriberName::ReaderName`, and ensure participant/type/topic references
  resolve. The strict compiler rejects missing references and role/type
  mismatches before code generation.
- State and command flows intentionally use **separate topics** to avoid
  gateway reflection. Preserve that separation unless adding an explicitly
  reviewed loop-prevention policy.

Then run the real integration selectors:

```sh
ctest --test-dir build-dds -R '^(config\.actual_mag|dds\.real_gateway|dds\.companion_processes)$' \
  --output-on-failure
```

`dds.real_gateway` verifies real DDS delivery in both directions while using
the in-memory backend only for CAN transport. Successful SocketCAN/vcan traffic
is a separate integration result; memory mode is not evidence that the Linux
CAN socket or physical bus works.

## Compiled graph and resources

`gateway.xml.in` references MAG-created named entities and declares the native
CAN stream catalog, category capacities, four bidirectional routes, CAN work
budgets, scheduler budgets, and a diagnostics period. The executable consumes
these compiled tables. Its fixed example provisioning allows two categories,
four routes, and eight references/results per route; incompatible graph growth
fails initialization rather than allocating more runtime storage.

`PGW_DDS_DIAGNOSTICS=ON` adds a fixed typed route-counter snapshot writer/reader,
keyed by entity kind (1 = route) and ID, four known route keys, and history resources **before MAG runs**. Readers reserve a bounded second physical sample per key for an outstanding loan while retaining KEEP_LAST depth one; writers remain one/key. The exporter
publishes at the XML-configured step period with a maximum four snapshots per
period; it does not replay events. Export failure increments the affected route's
export counter and does not recursively log or fault business routes. Turning
the option OFF removes the management inventory and lets MAG recompute resources.
Operational counters and final fixed-buffer JSON reports remain enabled.

Startup prints actual factory/participant/endpoint QoS from public getters,
not presumed XML literal values or inferred DDS bytes. Monotonic collection
times are local snapshot observations, never forwarded as DDS source timestamps.

## Requirement verification

The initial version-1 management snapshot has twelve operational counters.
`FATAL` counts fatal per-sample write outcomes; appended slot 11
`ROUTE_FAULTS` counts route fault transitions. They have distinct units and
are exported independently. Compile-time checks tie the generated IDL array
extent to the current core inventory; the real DDS test roundtrips both slots.

| Requirement / risk | Verification |
| --- | --- |
| XML strictness and reference/policy integrity | `config.strict_xml`: XSD and semantic negative tests |
| Actual MAG authority / unsupported QoS warning | `config.actual_mag`: real generation, added endpoint changes generated allocations, lifespan warning fails |
| Named creation/adoption and rollback | `dds.real_gateway`: missing binding/wrong role rejected, failed initialization restores arena, configured endpoints adopted |
| Bidirectional gateway semantics | Real DDS state speed 1000; command 123.4 patches exactly `d20400000101abcd` |
| Loan ownership and lifecycle samples | Reborrow/double return/shutdown-with-loan rejection; actual DDS dispose produces no CAN frame |
| Generic typed dispatch | Non-CAN `PGWTest::Probe` binding sends/receives native values using the same adapter |
| Metadata preservation policy | Missing capability rejected at bind; nonportable/negative/oversized/nanosecond-invalid timestamps rejected; actual peer sees `123s + 456ns` |
| Diagnostic visibility/isolation | Actual typed management reader sees accepted/export-error counters; rejected exporter input does not fault routing |
| Bounded operation / allocation coverage | 1,000 recorded stress steps and 4,004 local state acceptances; zero post-READY arena requests; libc/OSAPI interception reports observed allocations |
| Actual companion executable | `dds.companion_processes`: separate owned processes communicate over real DDS and verify exact memory-CAN bytes |
| Enabled/disabled management inventories | Both configurations clean-built and ran all four DDS/config tests |

The runtime allocation monitor starts before first business traffic, metadata
rejection, and snapshot export, after initialization and initial matching.
Deliberate libc/arena control probes verify those interceptors. Observed libc
and OSAPI calls were zero in the tested run; counts are middleware-inclusive
and do not cover libc internal calls or kernel allocations. Gateway arena
growth is independently rejected. This is not a whole-process/late-discovery
allocation qualification or a WCET claim.

**Unverified/deferred:** dedicated SocketCAN/vcan end-to-end and echo tests
require an owned interface; no privileges/shared-interface substitutions were
used. Reliable DDS saturation/backpressure stress, late/undeclared-peer
discovery allocation, middleware memory attribution, hostile peer topology,
non-loopback deployments, and Cert API/tool qualification remain separate work.
The example does not guarantee peer observation of every best-effort state
acceptance. Retained CAN baselines have no expiry; stale-baseline commands are
an explicitly documented risk.

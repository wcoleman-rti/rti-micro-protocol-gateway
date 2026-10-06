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

# DDS remote control

## Build and activation

Remote control is compile-time opt-in and requires the DDS adapter:

```sh
cmake -S . -B build-control \
  -DPGW_ENABLE_DDS=ON -DPGW_ENABLE_REMOTE_CONTROL=ON
cmake --build build-control --parallel
```

The default is `PGW_ENABLE_REMOTE_CONTROL=OFF`; that build creates no control
IDL, participant, endpoint, or command-processing path. In an opt-in build the
service XML selects exposed resources, but the gateway still starts with remote
control inactive. In the CAN/DDS example, explicitly activate it with a startup
domain before gateway initialization:

```sh
build-control/examples/can_dds/pgw_can_dds_gateway \
  --memory --control-domain 84 --telemetry-period-ms 100 100000
build-control/examples/can_dds/pgw_dds_control_controller \
  84 13 6 --telemetry
```

Run the gateway and controller in separate terminals. The controller arguments
are DDS domain ID, generated `Resource` enum ordinal, and explicit action enum
ordinal. The service-specific standalone
controller IDL is installed under `share/pgw/controllers`. The example's
`control_resources.h` and generated IDL give the authoritative resource
ordinals for a service; do not carry ordinals across service compositions.

Options are typed, validated at startup, and used before service initialization.
The default telemetry period is zero. The example service XML selects the CAN
`received_frames` metric and sets a 100 ms minimum period; faster nonzero
requests are rejected. Set `PGW_EXAMPLE_CONTROL_TELEMETRY=OFF` to build the
example without that metric; no telemetry type, topic, writer, or reader is
then generated. Omitting `--telemetry-period-ms` leaves the selected writer
idle.
Only the dedicated control participant is implemented; participant sharing is
not enabled.

Adapter manifests group supported actions under `<control>` and declare each
supported metric under `<telemetry>`. For example:

```xml
<adapter name="can" version="1" control-api-version="1">
  <control>
    <capability kind="connection" actions="up|down"/>
  </control>
  <telemetry>
    <metric resource-kind="connection" name="received_frames"
            scalar="uint64" unit="frames"/>
  </telemetry>
</adapter>
```

The service XML must separately select a declared metric before code generation
adds its telemetry type, topic, and writer.

## Interface behavior

The enabled example generates one keyed state writer, one result writer, and
one command reader on the dedicated control participant. State is keyed by
`Resource`, RELIABLE, TRANSIENT_LOCAL, and KEEP_LAST depth one. Command and
result are unkeyed, RELIABLE, VOLATILE, and KEEP_LAST depth four. With the
selected example metric, one additional telemetry topic/writer is created;
telemetry is keyed by `(Resource, TelemetryKind)`, BEST_EFFORT, VOLATILE, and
KEEP_LAST depth one. State/result writers use zero `max_blocking_time`. The
control participant is created
in code at the startup-selected domain; its statically generated AppGen types,
entities, and QoS are retained. The real Micro 4.3.0 integration test exercises
this domain override and checks the created endpoint histories/QoS.

Commands use explicit connection `UP`/`DOWN`, input/output `ENABLE`/`DISABLE`,
and route `PAUSE`/`RESUME` actions. CAN and Connext Micro adapters expose
versioned capability manifests and synchronous logical gating callbacks.
Physical link state and DDS entity activation are not implied. Route scheduling
pause/resume is implemented in core. At most four commands are processed before
route work in a service step. Repeated actions return `NO_CHANGE`; a route
no-op does not publish duplicate state.

When selected, telemetry publishes the adapter's unsigned frame counter with
its declared `frames` unit. The only current metric is `received_frames` on a
CAN connection; it is sampled at the configured period and sent BEST_EFFORT
with no retry.

Every selected resource receives an initial state snapshot. Changed state is
published immediately; if a write fails, the latest snapshot remains dirty and
one dirty resource is retried per subsequent step. Result events are not
retried: a failed result write increments a bounded local counter and does not
imply delivery. `PGW_Service_control_counters` exposes processed commands,
unsupported/invalid/failed actions, command-read errors, state failures/retries,
and rejected result writes. These counters are local observability, not DDS
endpoints. Telemetry read/write failures and delivered sample attempts also
have bounded counters; telemetry samples are not retried.

Results carry the outcome and the command sample's 16-byte DDS publication
handle plus publication sequence-number high/low fields. There is no
application command ID. The controller is an example client, not a privileged
or authenticated identity.

## Security and target qualification

No application-level authentication or allowlist is added. Admission depends on
DDS discovery/peer configuration and any deployed DDS security. Restrict
controller admission and protect the DDS domain in the integrator's deployment.

The selected developer target was built and exercised with
`RTIME_TARGET_NAME=x86_64leElfgcc13.3.0-Linux6` and Connext Micro 4.3.0. This
configuration used the installed host GNU 15.2 compiler with the SDK's matching
x86_64 Linux 6 libraries. Target integration verified a startup-selected
domain, four control topics/endpoints with telemetry selected (command reader,
state/result/telemetry writers), 16 keyed state instances, one telemetry key,
and the configured history/blocking QoS. The caller-side control resource,
endpoint, state-handle, and telemetry storage measured 1,449 bytes with
telemetry and 1,344 bytes without it. The example's PGW arena used 8,208 of its
262,144 bytes; DDS participant pools are separate.

The target benchmark performed 10,000 local BEST_EFFORT telemetry writes in
30,000,000 ns (about 3 microseconds per write, or 333,333 writes/second in that
test). The example service XML sets a 100 ms minimum period, limiting its one
selected metric to 10 samples/second. A 1,000-step active gateway run emitted
10 telemetry samples with zero telemetry read/write failures. This is a target
measurement for the one-metric example, not a WCET claim or a ceiling for other
compositions.

The generated gateway executable `size` totals were 1,077,385 bytes with
`PGW_ENABLE_REMOTE_CONTROL=OFF`, 1,141,003 bytes with remote control compiled
and telemetry unselected, and 1,162,459 bytes with telemetry selected. The
telemetry-enabled controller executable was 1,084,888 bytes.

In the OFF runtime integration run, effective gateway limits were 2 factory
participants, 6 local readers, 8 local writers, 10 local topics, and remote
limits 2/6/6 (participants/readers/writers). With control compiled but inactive,
no control participant/endpoints were created; local limits stayed 6/8/10 and
effective factory/remote limits were 4/9/9 without telemetry and 4/10/10 with
the metric selected. The active participant queried limits of 5 local readers, 6/7 local
writers, 7/8 local topics, and 7/8 local types without/with telemetry; remote
reader/writer limits were 9/9 without telemetry and 10/10 with telemetry.
Three 300-step RSS runs yielded
3,380–3,488 KiB (feature OFF), 3,476–3,604 KiB (control compiled, telemetry
unselected/inactive), 3,484–3,628 KiB (telemetry selected/inactive), and
3,748–3,760 KiB (dedicated participant and telemetry active). Linux allocator
and scheduling variation makes RSS unsuitable as an exact pool budget.

These measurements qualify the supported dedicated Linux6 example profile.
Participant sharing is not implemented. Telemetry measurement covers only the
selected CAN counter on this target; other metrics, compositions, and targets
need separate pool/rate qualification. A different RTOS/board/toolchain still
requires its own Micro/AppGen pool, full arena, and WCET qualification.

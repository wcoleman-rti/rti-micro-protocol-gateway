<!--
  (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.

  RTI grants Licensee a license to use, modify, compile, and create derivative
  works of the Software. Licensee has the right to distribute object form only
  for use with RTI products. The Software is provided "as is", with no warranty
  of any type, including any warranty for fitness or support. RTI shall not be
  liable for incidental or consequential damages.
-->

# RTI Connext Micro remote control

Remote control is optional, defaults OFF, and requires the DDS adapter. Build
with:

```sh
cmake -S . -B build-control -DCMAKE_BUILD_TYPE=Release \
  -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_JREHOME="<installed Java 17 runtime>" \
  -DPGW_ENABLE_DDS=ON -DPGW_ENABLE_REMOTE_CONTROL=ON
cmake --build build-control --parallel
ctest --test-dir build-control --output-on-failure
```

See the [build guide](build.md) for SDK and Python environment setup. With the
feature enabled, the service XML selects resources and metrics, but control
starts inactive. In the CAN/DDS example, activate it at startup by selecting a
dedicated control domain:

```sh
build-control/examples/can_dds/pgw_can_dds_gateway \
  --memory --control-domain 84 --telemetry-period-ms 100 100000
build-control/examples/can_dds/pgw_dds_control_controller 84 13 6 --telemetry
```

Run the gateway and controller in separate terminals. The controller arguments
are DDS domain ID, generated `Resource` ordinal, and action ordinal. The
service-specific controller IDL is installed under
`share/pgw/controllers`; generated `control_resources.h` is authoritative for
the resource ordinals in a particular service.

The example selects CAN connection/input/output and route resources in its
`<control>` section. Adapter manifests declare supported actions and telemetry
metrics:

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

Selected resources receive an initial state snapshot. Changed state is
published immediately; a failed state write leaves the latest snapshot dirty
for bounded retry. Results carry the action outcome and source DDS correlation
metadata; failed result writes are counted but not retried. Commands are
processed in bounded batches on the selected session worker. Route actions
pause/resume core dispatch; adapter connection/input/output actions apply the
capabilities declared in the adapter manifest.

The example's optional `received_frames` metric uses a monotonic telemetry
period and BEST_EFFORT delivery. Its configured minimum period is 100 ms.
Setting `PGW_EXAMPLE_CONTROL_TELEMETRY=OFF` omits the telemetry type, topic,
writer, and reader.

Only a dedicated control participant is supported; participant sharing is not
available. The interface adds no application-level authentication or
allowlist. Admission depends on DDS peer configuration and any deployed DDS
security. Restrict controller admission and protect the DDS domain in the
integrator's deployment.

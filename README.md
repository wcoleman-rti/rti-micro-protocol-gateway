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

# RTI Micro protocol gateway

A resource-bounded C11 gateway for routing data between protocol adapters. The
core uses opaque samples and statically registered bindings; adapters own
connectivity and sample metadata. DDS bindings can use loan-scoped sample/context
views for direct native writes or explicitly negotiated static cross-schema
translation. Remote control is optional and defaults off.

## Quick start

You need CMake 3.24+, a C11 compiler, Python 3, a licensed Connext Micro 4.3.0
installation, and Java 17 for the RTI generators. Install the host-tool
dependencies and configure with the paths/architecture for your SDK:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install \
  -r tools/dbc_codegen/requirements.txt \
  -r tools/config_codegen/requirements.txt
cmake -S . -B build \
  -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_JREHOME="<installed Java 17 runtime>"
cmake --build build
ctest --test-dir build --output-on-failure
```

Try the safe, no-CAN-interface demo after building. Run each command in a
separate terminal; DDS uses the licensed Micro implementation:

```sh
build/examples/can_dds/pgw_can_dds_gateway --memory 10000
build/examples/can_dds/pgw_dds_companion 123.4 500
```

Full SDK selection, component options, and package-consumer instructions are in
the [build guide](docs/build.md). Developers extending the system can start with
[adapter and DDS binding development](docs/developing-adapters.md); the optional
[DDS remote-control interface](docs/remote-control.md) has its own guide.
For more detail, start at the [hosted documentation site](https://wcoleman-rti.github.io/rti-micro-protocol-gateway/).

## Project map

- **Core** — bounded synchronous routing, lifecycle, schemas, diagnostics, and
  optional scheduler policy.
- **Adapters** — CAN transports and the Connext Micro DDS integration.
- **Bindings and tools** — application value conversion and build-time
  configuration/DBC code generation.
- **Schemas and bindings** — strict XML configuration schemas and IDL stored
  with the core or the binding that owns each wire type.
- **Examples, tests, benchmarks** — runnable integration example, verification,
  and measurement workloads.

Browse the [C API reference](https://wcoleman-rti.github.io/rti-micro-protocol-gateway/reference/index.html)
or the guides on [core contracts](https://wcoleman-rti.github.io/rti-micro-protocol-gateway/docs/core.html),
[testing](https://wcoleman-rti.github.io/rti-micro-protocol-gateway/docs/testing.html),
[performance](https://wcoleman-rti.github.io/rti-micro-protocol-gateway/docs/performance.html),
and [migration boundaries](https://wcoleman-rti.github.io/rti-micro-protocol-gateway/docs/limitations.html).

This is a prototype, not a certified implementation, an SDK-free core, an
arbitrary-topic router, or a whole-process allocation guarantee. No physical or
shared virtual CAN interface is configured automatically.

## License

Project-authored files are licensed under [LICENSE](LICENSE). Files with
separate third-party notices retain their own license terms.

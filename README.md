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

Resource-bounded C11 protocol gateway with opaque samples, statically registered
bindings, build-time configuration and Connext Micro-backed sequences.
Adapters own connectivity and sample metadata; the core never interprets DDS
samples or CAN frames.

## Build

Requires CMake 3.24+, a C11 compiler, licensed Connext Micro 4.3.0 installed
libraries/tools, Python 3 for host generation, and Java 17 for RTI generators.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install \
  -r tools/dbc_codegen/requirements.txt \
  -r tools/config_codegen/requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_JREHOME="<installed Java 17 runtime>" \
  -DPGW_BUILD_BENCHMARKS=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

See [build configuration](docs/build.md) for SDK/JRE selection and independent
component options. Generated files stay in build trees; the executable does not
parse XML or DBC at runtime.

Core-only validation links real installed Micro infrastructure without creating
DDS entities:

```sh
cmake -S . -B build-core -DPGW_ENABLE_CAN=OFF -DPGW_ENABLE_DDS=OFF \
  -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_JREHOME="<installed Java 17 runtime>" \
  -DPGW_BUILD_TESTS=ON -DPGW_BUILD_BENCHMARKS=ON
cmake --build build-core -j 4
ctest --test-dir build-core --output-on-failure
python3 benchmarks/runner/run.py build-core/benchmarks/pgw_core_benchmark
```

Generic libraries and headers support relocatable `find_package(PGW CONFIG)`
installation; see [package consumption](docs/build.md). Licensed SDK libraries
remain external, and application-generated bindings remain application-owned.

## Components

- [Core](core/): versioned C interfaces, exact schema negotiation, native
  fixed-buffer sequences, fair synchronous routing, diagnostics and optional
  scheduler worker policy over RTI OSAPI.
- [Adapters](adapters/): CAN transports/codec integration and Micro DDS
  generated entity adoption.
- [Bindings](bindings/): application schema and compiled value conversion.
- [Host tools](tools/): strict DBC/schema and XML configuration generation.
- [Examples](examples/): separated state/command flows.
- [Tests](tests/), [benchmarks](benchmarks/): requirement checks and persistent
  measurement reports.

The runnable bidirectional Micro DDS/CAN example, its `--memory` invocation,
and signal/topic/domain customization workflow are documented in
[examples/can_dds/README.md](examples/can_dds/README.md).

Read [core contracts](docs/core.md), [verification scope](docs/testing.md) and
[typed sequence support](docs/sequences.md), plus
[performance measurement boundaries](docs/performance.md). No physical CAN
interface or shared virtual CAN interface is configured automatically.

This is a prototype, not a certified implementation, an SDK-free core,
an arbitrary-topic router, an end-to-end zero-copy claim or a whole-process
allocation guarantee. See [migration boundaries](docs/limitations.md).

## License

Project-authored files are licensed under [LICENSE](LICENSE). Files with
separate third-party notices retain their own license terms.

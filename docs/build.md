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

# Building with installed Connext Micro

## Prerequisites

Use CMake 3.24+, a C11 compiler, Python 3, a licensed installed **Connext Micro
4.3.0** SDK, and Java 17 for the RTI host generators. Select the SDK root,
matching PIL/PSL architecture pair, target name and JRE for your installation.

For the maintained DBC/XML host tools, install their declared build-time
dependencies in a project-local environment and select it explicitly:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install \
  -r tools/dbc_codegen/requirements.txt \
  -r tools/config_codegen/requirements.txt
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_JREHOME="<installed Java 17 runtime>"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Supply the SDK's exact `RTIME_PIL_ARCH`, `RTIME_PSL_ARCH`, and
`RTIME_TARGET_NAME`; the finder verifies those values against installed
archives. SDK and Java locations are explicit inputs, not embedded defaults. The
`RTIME_TARGET_NAME` selects the installed Micro OSAPI/NETIO library profile; it
does not require downstream gateway code to be compiled with a compiler of the
same version encoded in that target name. GCC 13 or newer may compile the
gateway and examples for the selected profile. The verified developer build
uses GNU 15.2 with the Micro Linux6 library profile.
The persisted launcher in
`build/pgw-tools` embeds the configured JRE, so subsequent builds work even with
`JREHOME` unset in the shell. Generator outputs stay in the build tree.
`PGW_PYTHON_EXECUTABLE` explicitly selects one interpreter for every host
generator and Python test, taking precedence over `Python3_EXECUTABLE`.
When it is empty, the standard CMake `Python3_EXECUTABLE` override/finder remains
available. Configure does not implicitly activate `.venv` or install packages.

The configure report records SDK, PIL/PSL, Micro archive configuration, JRE,
Codegen and MAG versions. Each build tree also records these and the compiler/
CMake versions/options in `pgw-build-info-<configuration>.txt`.
The same verified tool versions, SDK/PIL/PSL paths and selected build flags/options
are machine-readable in `provenance.json`, for benchmark manifest consumption.
Multi-configuration generators place it in `<configuration>/provenance.json`.
Micro 4.3.0 bundles Codegen 4.7.0; those numbers need not
match. MAG does not implement `-version`: the finder checks its Java launcher with
`-help` and reads `Implementation-Version` from its distribution JAR manifest.
An unexpected SDK version, root, architecture, missing archive, unsupported
rescan linker, or mismatched Debug/Release selection is an error, not a fallback.

## Options and components

| Setting | Default | Purpose |
| --- | --- | --- |
| `PGW_ENABLE_CAN` | ON | CAN adapter and memory/SocketCAN transports |
| `PGW_ENABLE_DDS` | ON | Real Micro DDS adapter, Appgen and discovery |
| `PGW_ENABLE_REMOTE_CONTROL` | OFF | Opt-in control IDL/core interface; requires DDS |
| `PGW_REMOTE_CONTROL_MAX_CONTROLLERS` | 1 | Compile-time bound for control-domain peers (1..32) |
| `PGW_BUILD_TESTS` | ON | Requirement verification, including core runtime lifecycle |
| `PGW_BUILD_EXAMPLES` | ON | Runnable examples |
| `PGW_BUILD_BENCHMARKS` | OFF | Benchmark workloads |
| `PGW_ENABLE_RUNNER` | ON | Core's initialization-created optional scheduler policy |
| `PGW_WARNINGS_AS_ERRORS` | OFF | Strict gateway C compiler diagnostics |
| `PGW_GENERATOR_WARNINGS_AS_ERRORS` | ON | Reject RTI generation warnings |
| `RTIME_LIBS_BUILD_TYPE` | Auto | Match Debug archives to Debug, otherwise Release |

The top-level build adds `core`, `tools`, enabled adapters, `bindings`,
and selected examples/tests/benchmarks only when their component CMake files
exist. Disabling DDS **does not remove the installed Micro requirement**: core
typed sequences, clock adaptation and optional runner use Micro infrastructure
and OSAPI directly.
Static archive linkage is required (`BUILD_SHARED_LIBS=OFF`). Multi-configuration
generators use `RTIME_LIBS_BUILD_TYPE=Auto`; RelWithDebInfo/MinSizeRel use Release
archives. Use a fresh build directory when changing SDK or architecture, to
avoid stale finder cache entries.

`PGW::micro_infrastructure` rescans only `RTIConnextMicroDDS::core` and
`RTIConnextMicroDDS::osapi`, plus their system dependencies. Its link does not
pull in histories, discovery, network transport, or Appgen. The DDS adapter adds
those imported targets independently. Appgen's transitive DPSE library does not
enable DPSE discovery behavior.
When DDS is enabled, `PGW::micro_dds` is a separate convenience target combining
the imported C runtime, DPDE, Appgen and infrastructure targets; core never links it.
The Linux imported core target propagates SDK include directories (including
`include/rti_me`) and the `RTI_UNIX`/`RTI_LINUX` platform definitions.

## Generator helpers

`cmake/PGWCodegen.cmake` exposes the vendor helpers and two convenience macros:

```cmake
pgw_micro_codegen(
  IDL_FILE "${schema}"
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated"
  VAR SIGNALS
  DEPENDS "${imported_idl}")
# Returns SIGNALS_C_SOURCES and SIGNALS_C_HEADERS; always C, Micro,
# non-interpreted support (-interpreted 0).

pgw_micro_convert(
  FROM IDL TO XML INPUT "${schema}"
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated"
  VAR TYPE_MODEL
  DEPENDS "${schema_target}")
# TYPE_MODEL is the build-time rtiddsgen XML representation of the IDL types.

pgw_micro_appgen(
  XML_FILE "${system_xml}"
  IDL_FILE "${schema}"
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated"
  REFERENCED_FILES "${qos_xml}"
  DEPENDS "${schema_target}"
  VAR SYSTEM)
# Returns SYSTEM_C_APPGEN_SOURCES and SYSTEM_C_APPGEN_HEADERS.
```

Pass all imported IDL and mapping inputs through `DEPENDS`; include directories
are search paths, not a substitute for dependency files. Generated inputs must
have producing CMake custom commands/targets. The reused helpers preserve output
tracking, explicit XML/IDL/reference dependencies, launcher dependencies and
`VERBATIM` quoting. Changes to the persistent launcher, helper module or generator
distribution JAR cause regeneration. MAG's `IDL_FILE` names a type-support
include, not a parsed IDL input: the helper supplies its basename to MAG while
tracking a provided absolute input path as a dependency.
Nonzero exit statuses and zero-status error diagnostics fail the build; warnings
are strict by default. For a known acceptable warning, set the narrow regular
expression `PGW_GENERATOR_WARNING_ALLOW_REGEX`; do not mask unsupported-field
warnings or resource-limit generation failures.

## Core runtime policy and OSAPI limits

Include `pgw/runtime.h` and link `PGW::core`. Call
`PGW_Runtime_initialize()` before using the monotonic clock or runner. The fallible
`PGW_Runtime_monotonic_time_ns()` uses Micro's monotonic ticktime, not wall-clock
time. Its units are nanoseconds, but resolution follows the installed PSL timer;
it is not a nanosecond-resolution benchmark clock. The convenience
`PGW_Runtime_monotonic_clock(void *, uint64_t *)` has the service callback
signature and propagates failures rather than substituting a zero timestamp.
An event whose service clock callback fails is not captured, and the step
returns `PGW_IO_ERROR`; adapters may emit untimed events with `time_valid=false`.

The optional runner uses a zero-initialized caller-owned `PGW_Runner`.
`initialize` creates its semaphores/native thread and starts that thread blocked;
`start` only releases it, without thread creation or allocation. Period and stack
size are initialization settings. `stop` wakes and joins; no callbacks run after
it returns successfully. Pass a configured **UNINITIALIZED** `PGW_Service` to
runner initialization. A one-second startup handshake establishes the parked
native thread before the runner calls `PGW_Service_initialize()` and exposes
READY. There are no startup retries in the operating phase. The runner exclusively drives
`PGW_Service_step()`; after joining, it calls `PGW_Service_stop()`. Its atomic
`last_step_status` records the latest step result without replacing route
diagnostics. Service finalization remains caller-owned. Lifecycle calls are
serialized by the caller and may not execute inside an adapter callback.
Finalize/reinitialize both service and runner before restarting.
Adapter callbacks must obey the gateway's nonallocating/nonblocking contract. Destroy
all Micro users before the application calls `OSAPI_System_finalize()`.
Before runner initialization, provision each route with
`PGW_Route_initialize_storage()` and attach the route catalog with
`PGW_Service_set_routes()`. These borrow fixed typed backing arrays behind actual
per-element Micro sequences. Service finalization releases those attachments;
repeat both helpers before restarting. Core runtime policy storage contains only
thread/semaphore handles and scalar lifecycle state, not sequence-like catalogs.
The core runtime lifecycle test observes initialization allocations, then verifies
zero wrapped libc/OSAPI heap calls across start, first/repeated steps and stop.
This is instrumentation coverage, not proof of allocation freedom inside
unintercepted vendor/OS internals. The implementation adds no platform backend:
clock adaptation calls `OSAPI_System_initialize/get_ticktime`, and runner policy
uses SDK threads/semaphores. Blocking-thread behavior is verified on installed
Linux6 PSL, not universally qualified for other PSL/Cert profiles.

## Build utilities

The three bundled RTI CMake utility modules retain their original copyright and
license notices. Micro-specific adaptations correct generator validation,
explicit JRE propagation, version probes, dependency and quoting handling, and
archive-configuration mappings. These are build-time utilities; the licensed
SDK runtime remains an external build dependency.

## Install and independent consumers

The generic targets form a relocatable installed CMake package:

```sh
cmake -S . -B build-package -DCMAKE_BUILD_TYPE=Release \
  -DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_JREHOME="<installed Java 17 runtime>" \
  -DRTIME_PIL_ARCH="<installed PIL architecture>" \
  -DRTIME_PSL_ARCH="<installed PSL architecture>" \
  -DRTIME_TARGET_NAME="<installed target name>" \
  -DPGW_BUILD_TESTS=OFF -DPGW_BUILD_EXAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/build-package/package-prefix"
cmake --build build-package --parallel
cmake --install build-package
```

Consumers use their own build directory and require the same licensed Micro
4.3.0 SDK version and PIL/PSL pair:

```cmake
cmake_minimum_required(VERSION 3.24)
project(consumer LANGUAGES C)
find_package(PGW 0.1 CONFIG REQUIRED COMPONENTS core adapter_can)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE PGW::core PGW::adapter_can)
```

Configure with `-DCMAKE_PREFIX_PATH=/path/to/package-prefix` and the selected
external SDK root, e.g. `-DRTIMEHOME=/path/to/rti_connext_dds_micro-4.3.0`. The
installed package supplies its required PIL/PSL/target names. It discovers RTI archives
externally; it contains only PGW archives/public headers and the three licensed
CMake utility modules, not RTI archives or SDK implementations. The project
license is installed at `${CMAKE_INSTALL_DATADIR}/pgw/LICENSE`. Runtime-only
consumers need neither Python nor Java. Exported names preserve `PGW::core`,
`PGW::diagnostics_local`, `PGW::adapter_can`,
`PGW::can_memory`, `PGW::can_socketcan`, `PGW::adapter_dds_connext_micro`, and
`PGW::binding_signal`, when those components were built. The infrastructure and
optional DDS convenience targets are also exported for dependency closure.

The installed configuration binds PGW and Micro to the archive variant that
was installed (Release or Debug), including when the consumer uses another
configuration name. Do not override `RTIME_LIBS_BUILD_TYPE` to a different
variant. Installing another configuration selects that variant coherently.
Existing incompatible RTI imported targets are rejected. Relative GNUInstallDirs
lib/include destinations are supported; absolute destinations are rejected
because they cannot form a relocatable package.

Application-specific generated codecs/models, typed signal DDS binding sources,
and the mapping-dependent `pgw/signal_dds.h` facade are deliberately excluded.
Applications generate and compile those against the generic adapter targets.
The standalone validation fixture `cmake/tests/package_consumer` links every
generic adapter target without opening a CAN interface or creating DDS entities.
It was built and run against both the original and a moved installation prefix.
It also declares/instantiates an application-owned typed Micro sequence by defining
`T`/`TSeq` and including installed RTI `reda_sequence_decl.h`/`reda_sequence_defn.h`
directly, following the generator pattern. It exercises
the registry's typed adapter catalog. Fixture arrays are initialization-time
borrowed backing storage, not independent pointer/count/capacity collection APIs.
There is no PGW container façade or `PGW::platform_osapi` compatibility target.
Core supplies runtime adaptation and optional runner policy directly.

```sh
cmake -S cmake/tests/package_consumer -B build-package/consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build-package/package-prefix" \
  -DRTIMEHOME="<installed Micro 4.3.0 SDK root>"
cmake --build build-package/consumer
build-package/consumer/pgw_package_consumer
```

For a custom nested library directory, use `PGW_DIR` to name its
`lib/<custom>/cmake/PGW` configuration directory explicitly.

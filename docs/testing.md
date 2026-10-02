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

# Requirement verification

Tests exercise gateway requirements rather than independently qualifying
CAN, DDS, REDA, libc or MAG. Use `ctest --test-dir build --output-on-failure`
after a build; component test names identify their requirement scope.

| Requirement | Verification | Observable / remaining scope |
| --- | --- | --- |
| CORE-OPAQUE | core requirement executable | Negotiated accessors, exact schema rejection, fixed native pointer sequence |
| CORE-LOAN | core requirement executable | Normal, empty, NO_DATA, invalid, backpressure and fatal writes return exactly once |
| CORE-FAIR | core requirement executable | 10,000 steps; saturated route does not stop second route |
| CORE-BOUNDS | core requirement executable | Arithmetic overflow, arena alignment/exhaustion, registry freeze, no sequence growth |
| DIAG-ATOMIC | core requirement executable | Four concurrent producers; 40,000 exact counter updates; wrap |
| DIAG-EVENT | core requirement executable | Every attempted event accounted as drained, full or contention drop; failed service clock surfaces `PGW_IO_ERROR` and never publishes an invalid zero timestamp as valid |
| DIAG-FORMAT | core requirement executable | Fixed JSON buffer and explicit truncation |
| DIAG-SINK | local sink requirement executable | Exact snapshot JSON, saturated nonblocking output, counted truncation, no runtime allocation |
| ALLOC-CORE | core requirement executable | Wrapped libc and OSAPI allocation control probes; no allocation across first traffic/10,000 steps/errors |
| PERF-CORE | core benchmark smoke | Exact counts and loan balance; bounded timing capture; zero runtime allocation |
| CODEC | `pgw_codegen_host`, `pgw_codec_golden`, `pgw_signal_dds_conversion` | Reproducible build-only generation, strict rejection, golden endian/mux/FD/extended bytes, exact int64 boundaries and generated DDS conversions |
| CAN | `can.bounded_mapping`, `can.socket_errors` | Baselines, byte preservation, loan lifetime, partial sends/rollback, bounded transport, metadata and explicit command/error outcomes |
| DDS/MAG | `dds.real_gateway`, `config.actual_mag`, `dds.companion_processes`, `config.strict_xml` | Real bidirectional gateway traffic, second schema, timestamp policy, diagnostics subscriber, effective MAG resources and separate processes |
| BUILD | `build.rti_launcher`, `core.runtime_lifecycle` | Persistent JRE/warning policy, initialization-created parked runner using RTI OSAPI, no allocations during activation/first/repeated steps |
| PERF-CAN | `REQ_BENCHMARK_CAN_SMOKE`, `REQ_BENCHMARK_CAN_COUNTER_ONLY` | Actual memory-CAN decode/core/patch pipeline, golden transmitted bytes, explicit backpressure and all wrapped allocator controls |
| PERF-DDS | `REQ_BENCHMARK_DDS_SMOKE`, `REQ_BENCHMARK_DDS_COUNTER_ONLY` | Actual Micro/MAG gateway and companion over memory CAN, peer-observed DDS samples, separate accepted/offered/drop units and opt-in portable timestamp preservation |

Allocation coverage is limited to calls resolved through wrapped libc
`malloc/calloc/realloc/aligned_alloc/posix_memalign` and OSAPI
`Heap_allocate/realloc/allocate_buffer`. Control probes verify
interception, not whole-process compliance. Kernel/middleware allocations,
custom allocators and internal shared-library calls need separate attribution.
Core tests do not create DDS entities; middleware memory is not misreported
as gateway-owned storage.

Dedicated virtual CAN integration must use an owned interface. Shared
interfaces and physical buses are never test fallbacks.
Target hardware, cross-host time synchronization, Cert qualification, WCET,
physical-bus delivery and arbitrary undeclared DDS peer capacity are outside
the maintained host verification.

## Explicit noncoverage

- Hardware/kernel queue allocation and physical transmission: no owned physical
  bus or target instrumentation; SocketCAN error paths and memory transport are
  not substitutes for live-interface integration.
- Dedicated-vcan flow: requires an owned network namespace/interface; the
  maintained suite does not claim successful live SocketCAN traffic.
- Perf hardware counters: process-limited optional capture can be unavailable
  under system policy; unavailable capture is recorded without system changes.
- Undeclared DPDE peers and late discovery beyond configured XML inventory:
  outside the finite topology envelope; no unlimited connectivity claim.
- Cert/other compiler/RTOS counter backends and private-object pool storage:
  separate target qualification, not inferred from lock-free C11
  counters or SDK template availability.
- Exact whole-process heap/RAM/stack attribution and WCET: wrapper coverage and
  RSS are bounded evidence, not allocator/kernel/vendor qualification.

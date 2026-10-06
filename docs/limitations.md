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

# Extension and certification boundaries

The implementation requires an externally installed, licensed Connext Micro
4.3.0 SDK. RTI runtime libraries and implementation sources are external; the
bundled CMake utility modules retain their original license notices.
Gateway-authored files carry the project license in [LICENSE](../LICENSE);
third-party files with separate notices remain under their own terms.

The SDK's custom sequence templates are isolated behind `PGW_SampleSeq`.
Installed availability is not qualification evidence, a Cert compatibility
guarantee or a cross-version infrastructure ABI promise.

## Deliberately deferred

- DPSE profile/remote assertions; this prototype uses finite DPDE resources.
- Shared-library plugins, hot reload, runtime graph/type discovery and
  dynamic stream creation.
- Runtime type discovery, arbitrary schema transformation, DynamicData and
  serialized CDR forwarding. Statically compiled DDS bindings may provide
  explicit cross-schema translators over `PGW_SampleView`; there is no generic
  transformation engine or runtime-discovered mapping.
- Parallel gateway sessions/thread pools.
- Extended DBC multiplexing, floating-point DBC fields, J1939 and ISO-TP.
- Profinet, OPC UA, MQTT and non-Linux CAN transports.
- Native downstream replay of DDS publication identity/sequence numbers.
- Clock synchronization/conversion and arbitrary cross-host timing claims.

Metadata capture does not imply destination preservation. Timestamp policies
are adapter-specific and opt-in; source clock validity and precision must be
explicit. Publication handles and sequence numbers are scoped provenance,
not globally portable identity or order. The schema is not silently extended
to carry private metadata.

CAN commands need a received baseline, preserve unrelated bits and never
invent a zero-filled frame. Baselines do not expire: stale-state risk is an
explicit selected behavior, not freshness assurance. Local adapter acceptance
does not establish physical delivery or DDS acknowledgment.

MAG owns topology-derived resources for the declared XML inventory.
Key/history/workload budgets remain application inputs. An undeclared peer
can exceed that engineering envelope; dynamic discovery is not unlimited
capacity.

## Future Cert assessment

Audit the actual Cert product/release for OSAPI and sequence subset availability,
static type support, initialization/deletion lifecycle, threading, PSL/BSP,
discovery, permitted allocators and supported resource/diagnostic APIs. Then
qualify tools and verification artifacts against the target process.

Host tests are not RTOS/hardware qualification. Kernel/middleware memory,
thread stacks, all allocator entry points, bounded scheduling under target
loads and WCET/deadline assurance require target-specific measurements.
Absence of vendor logging must not remove operational counters.

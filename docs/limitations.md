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

# Current scope and limits

The gateway requires an installed, licensed RTI Connext Micro 4.3.0 SDK. The
core uses Micro's typed sequences and OSAPI even when the DDS adapter is
disabled.

## Configuration and routing

- Topology, endpoint types, adapters, and routes are configured before service
  initialization. There is no runtime plugin loading, endpoint creation, or
  stream discovery.
- A route has one reader and one writer. A consuming reader belongs to one
  route. Each session has one worker; routes within a session are dispatched
  serially in bounded, round-robin batches.
- Storage and queues are fixed-capacity. Capacity exhaustion follows the
  documented adapter policy; the core does not retain or retry samples after
  writer backpressure.
- Generated DDS types are statically bound. `PGW_TypeInfo` provides logical
  type identity, not a runtime-introspectable field schema. Type-specific
  conversion belongs to generated `PGW_DDSTypeBinding` callbacks.

## Protocol and delivery behavior

- CAN supports the bounded DBC subset documented by the
  [DBC code generator](../tools/dbc_codegen/README.md). CAN commands require a
  received baseline, and baselines do not expire.
- CAN is available through the memory transport and Linux SocketCAN. The
  maintained suite does not claim physical-bus delivery or live vcan traffic;
  see [vcan integration](vcan-integration.md).
- Connext Micro 4.3.0 writes support source timestamps and destination instance
  handles. Its `write_w_params` implementation does not propagate input sample
  identity or related-sample identity.
- Local CAN acceptance and local DDS write acceptance are not proof of
  physical delivery or remote DDS observation. Source timestamps do not
  synchronize clocks across hosts.
- Resource limits are based on the declared AppGen topology. Undeclared peers
  can exceed the configured discovery/resource budget.

## Qualification

The project is a prototype, not a certified implementation or a whole-process
allocation, WCET, or deadline guarantee. Host tests and benchmarks do not
qualify an RTOS, hardware target, kernel, physical CAN bus, or every
middleware/vendor allocator.

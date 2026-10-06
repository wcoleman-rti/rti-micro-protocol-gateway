<!--
  (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
-->

# C API guide

Use this guide to find the public interfaces by task. The generated [API
reference](reference/index.html) is organized by the same areas; use its
search or alphabetical index when you already know a symbol.

## Assemble and run a gateway

Start with the [core and lifecycle API](reference/group__pgw__core__api.html)
and the [core contracts guide](docs/core.md). Initialize a `PGW_Registry`,
register adapters and representations, provision route storage, and attach the
route catalog to a `PGW_Service`. Then initialize and step the service, stop it,
and finalize its caller-owned storage. The main entry points are
`PGW_Registry_initialize`, `PGW_Registry_register_adapter`,
`PGW_Registry_register_binding`, `PGW_Route_initialize_storage`,
`PGW_Service_set_routes`, `PGW_Service_initialize`, `PGW_Service_step`,
`PGW_Service_stop`, and `PGW_Service_finalize`. For periodic execution, see
`PGW_Runner` in the core group.

## Connect CAN

Use the [CAN adapter and transports API](reference/group__pgw__can__api.html)
with the [signal representation API](reference/group__pgw__signal__api.html).
Initialize a `PGW_CANMapping`, configure categories and a transport on
`PGW_CANConfig`, then register `PGW_CANAdapter` with the core. The memory
transport is useful for tests and examples; SocketCAN must be opened and
configured explicitly. See the [CAN adapter guide](adapters/can/index.html) for
mapping, command baselines, frame metadata, and transport behavior.

## Add an RTI Connext Micro DDS endpoint

See the [RTI Connext Micro adapter API](reference/group__pgw__dds__api.html)
and [signal representation API](reference/group__pgw__signal__api.html), then
follow [adapter and DDS binding development](docs/developing-adapters.md).
Register the DDS adapter, describe named endpoints with
`PGW_DDSEndpointConfig` and `PGW_DDSConfig`, then create the connection with
`PGW_DDS_create`. Bind each endpoint to generated type support and a
representation; borrowed DDS samples and metadata remain valid only while
their reader loan is active.

## Collect diagnostics

The [diagnostics and local export API](reference/group__pgw__diagnostics__api.html)
provides counters, snapshots, bounded event collection, JSON formatting, and a
nonblocking local sink. Start with `PGW_Counters_initialize` and
`PGW_Counters_snapshot` for counters or `PGW_Diagnostics_initialize`,
`PGW_Diagnostics_emit`, and `PGW_Diagnostics_drain` for events. For the optional
DDS control and telemetry endpoints, see the [remote-control guide](docs/remote-control.md).

## Browse by API area

| Area | Public headers | Reference group |
| --- | --- | --- |
| Core and lifecycle | `pgw/core.h`, `pgw/runtime.h`, `pgw/sequence.h` | [Core API](reference/group__pgw__core__api.html) |
| CAN adapter and transports | `pgw/can.h`, `pgw/can_memory.h`, `pgw/can_socketcan.h` | [CAN API](reference/group__pgw__can__api.html) |
| RTI Connext Micro adapter | `pgw/dds/connext_micro.h` | [DDS API](reference/group__pgw__dds__api.html) |
| Signal representation | `pgw/signal.h` | [Signal API](reference/group__pgw__signal__api.html) |
| Diagnostics and local export | `pgw/diagnostics.h`, `pgw/local_sink.h` | [Diagnostics API](reference/group__pgw__diagnostics__api.html) |

The reference keeps symbol-level search and alphabetical lookup available
alongside these groups.

#!/usr/bin/env python3
#
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is under no
# obligation to maintain or support the software. RTI shall not be liable for any
# incidental or consequential damages arising out of the use or inability to use
# the software.
#

"""Generate the standalone controller IDL for an explicitly controlled service."""
import argparse
from pathlib import Path
import re

from config_codegen import (
    ConfigError,
    compile_control_resources,
    control_catalog,
    validate_gateway_xml,
)


def generate(gateway, common_idl, output, adapter_manifests=(), resource_header=None):
    common = Path(common_idl).resolve()
    if not common.is_file():
        raise ConfigError(f"common control IDL does not exist: {common}")
    service = validate_gateway_xml(gateway)
    route_names, connection_adapters, stream_adapters, stream_roles = control_catalog(service)
    resources, metrics, minimum_period_ms, max_controller_peers = compile_control_resources(
        service.find("control"), route_names, connection_adapters,
        stream_adapters, stream_roles, adapter_manifests)
    if not resources:
        raise ConfigError("controller IDL requires an explicit non-empty control section")
    enums = [resource["enum"] for resource in resources]
    if len(enums) != len(set(enums)):
        raise ConfigError("control resource enum names collide")
    if any(not re.fullmatch(r"[A-Z_][A-Z0-9_]*", name) for name in enums):
        raise ConfigError("unsafe normalized control resource name")
    telemetry_kinds = sorted({metric["enum"] for metric in metrics})
    lines = [
        '#include "control_common.idl"',
        "",
        "module PGW_ControlService {",
        "    enum Resource {",
        ",\n".join(f"        {name}" for name in enums),
        "    };",
        "",
        "    struct Command {",
        "        Resource resource;",
        "        PGW_Control::Action action;",
        "    };",
        "",
        "    struct State {",
        "        Resource resource; //@key",
        "        PGW_Control::ResourceStatus status;",
        "        unsigned long command_capabilities;",
        "        unsigned long telemetry_capabilities;",
        "    };",
    ]
    if metrics:
        lines.extend([
            "",
            "    enum TelemetryKind {",
            ",\n".join(f"        {name}" for name in telemetry_kinds),
            "    };",
            "",
            "    union TelemetryValue switch (PGW_Control::TelemetryScalarType) {",
            "        case PGW_Control::BOOLEAN_VALUE: boolean boolean_value;",
            "        case PGW_Control::INT32_VALUE: long int32_value;",
            "        case PGW_Control::UINT32_VALUE: unsigned long uint32_value;",
            "        case PGW_Control::INT64_VALUE: long long int64_value;",
            "        case PGW_Control::UINT64_VALUE: unsigned long long uint64_value;",
            "        case PGW_Control::DOUBLE_VALUE: double double_value;",
            "    };",
            "",
            "    struct Telemetry {",
            "        Resource resource; //@key",
            "        TelemetryKind kind; //@key",
            "        PGW_Control::TelemetryScalarType scalar_type;",
            "        TelemetryValue value;",
            "        octet unit[33];",
            "    };",
        ])
    lines.extend([
        "",
        "    struct Result {",
        "        PGW_Control::CommandOutcome outcome;",
        "        octet publication_handle[16];",
        "        long publication_sequence_high;",
        "        unsigned long publication_sequence_low;",
        "    };",
        "};",
    ])
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines) + "\n")
    if resource_header:
        header_path = Path(resource_header)
        header_path.parent.mkdir(parents=True, exist_ok=True)
        header_path.write_text(
            "#ifndef PGW_CONTROL_RESOURCES_H\n"
            "#define PGW_CONTROL_RESOURCES_H\n"
            f"#define PGW_CONTROL_RESOURCE_COUNT {len(resources)}u\n"
            f"#define PGW_CONTROL_MAX_CONTROLLER_PEERS {max_controller_peers}u\n"
            f"#define PGW_CONTROL_TELEMETRY_KIND_COUNT {len(telemetry_kinds)}u\n"
            f"#define PGW_CONTROL_TELEMETRY_METRIC_COUNT {len(metrics)}u\n"
            f"#define PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS {minimum_period_ms}u\n"
            "#endif\n")
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gateway", required=True)
    parser.add_argument("--common-idl", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--adapter-manifest", action="append", default=[])
    parser.add_argument("--resource-header")
    args = parser.parse_args()
    try:
        generate(args.gateway, args.common_idl, args.output, args.adapter_manifest,
                 args.resource_header)
    except (ConfigError, OSError) as exc:
        parser.exit(1, f"controller IDL generation failed: {exc}\n")


if __name__ == "__main__":
    main()

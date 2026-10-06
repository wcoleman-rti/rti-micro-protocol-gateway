#!/usr/bin/env python3
#
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI shall not
# be liable for any incidental or consequential damages arising out of the use
# or inability to use the software.
#

"""Generate reusable adapter capability IDL from its versioned XML manifest."""
import argparse
from pathlib import Path
import re

from config_codegen import ConfigError, parse_adapter_manifest


RESOURCE_BITS = {"connection": 0, "input": 1, "output": 2}
ACTION_BITS = {
    "connection": {"up": 0, "down": 1},
    "input": {"enable": 2, "disable": 3},
    "output": {"enable": 4, "disable": 5},
}


def generate(manifest_path, common_idl, output, header=None):
    common = Path(common_idl)
    if not common.is_file():
        raise ConfigError(f"common control IDL does not exist: {common}")
    name, metadata = parse_adapter_manifest(manifest_path)
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        raise ConfigError(f"unsafe adapter IDL module name {name!r}")
    capabilities = metadata["capabilities"]
    resource_mask = sum(1 << RESOURCE_BITS[kind] for kind in capabilities)
    action_mask = 0
    for kind, capability in capabilities.items():
        action_mask |= sum(1 << ACTION_BITS[kind][action]
                           for action in capability["actions"])
    metrics = sorted(metadata["metrics"].values(),
                     key=lambda item: (item["resource_kind"], item["name"]))
    telemetry_mask = sum(1 << metric["bit"] for metric in metrics)
    text = (
        '#include "control_common.idl"\n\n'
        f"module PGW_Adapter_{name} {{\n"
        "    const unsigned long MANIFEST_VERSION = 1;\n"
        f"    const unsigned long RESOURCE_KIND_MASK = {resource_mask};\n"
        f"    const unsigned long ACTION_MASK = {action_mask};\n"
        f"    const unsigned long TELEMETRY_METRIC_MASK = {telemetry_mask};\n"
        + "".join(f"    const unsigned long TELEMETRY_{metric['name'].upper()}_BIT = "
                  f"{metric['bit']};\n" for metric in metrics)
        +
        "    struct ControlManifest {\n"
        "        unsigned long version;\n"
        "        unsigned long resource_kind_mask;\n"
        "        unsigned long action_mask;\n"
        "        unsigned long telemetry_metric_mask;\n"
        "    };\n"
        "};\n"
    )
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text)
    if header:
        macro = name.upper()
        header_lines = [
            "#include <stdint.h>",
            f"#define PGW_{macro}_CONTROL_MANIFEST_VERSION 1u",
            f"#define PGW_{macro}_CONTROL_RESOURCE_KIND_MASK UINT32_C({resource_mask})",
            f"#define PGW_{macro}_CONTROL_ACTION_MASK UINT32_C({action_mask})",
            f"#define PGW_{macro}_CONTROL_TELEMETRY_METRIC_MASK UINT32_C({telemetry_mask})",
            *[f"#define PGW_{macro}_TELEMETRY_METRIC_{metric['name'].upper()} "
              f"{metric['bit']}u" for metric in metrics],
            "",
        ]
        header_path = Path(header)
        header_path.parent.mkdir(parents=True, exist_ok=True)
        header_path.write_text("\n".join(header_lines))
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--common-idl", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--header")
    args = parser.parse_args()
    try:
        generate(args.manifest, args.common_idl, args.output, args.header)
    except (ConfigError, OSError) as exc:
        parser.exit(1, f"adapter IDL generation failed: {exc}\n")


if __name__ == "__main__":
    main()

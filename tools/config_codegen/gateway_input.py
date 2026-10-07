#
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is under no
# obligation to maintain or support the Software. RTI shall not be liable for any
# incidental or consequential damages arising out of the use or inability to use
# the software.
#

import argparse
import hashlib
import json
import xml.etree.ElementTree as ET
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument("--template", required=True)
parser.add_argument("--inventory", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--diagnostics", choices=("0", "1"), default="1")
parser.add_argument("--probe-idl", required=True)
parser.add_argument("--diagnostics-idl", required=True)
parser.add_argument("--schema-header", required=True)
parser.add_argument("--remote-control", action="store_true")
parser.add_argument("--telemetry", action="store_true")
parser.add_argument("--max-controller-peers", type=int, default=1)
args = parser.parse_args()
inventory = json.loads(Path(args.inventory).read_text())
text = Path(args.template).read_text().replace("@fingerprint@", inventory["fingerprint"])
macros = []
for name, path in (("probe", args.probe_idl), ("diagnostics", args.diagnostics_idl)):
    fingerprint = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    text = text.replace(f"@{name}_fingerprint@", fingerprint)
    macros.append(f'#define PGW_{name.upper()}_SCHEMA_FINGERPRINT "{fingerprint}"')
Path(args.schema_header).write_text("\n".join(macros) + "\n")
root = ET.fromstring(text)
if args.diagnostics == "0":
    for parent in root.iter():
        for child in list(parent):
            if child.get("id") == "diagnostics" or child.get("binding") == "diagnostics":
                parent.remove(child)
if args.remote_control:
    if not 1 <= args.max_controller_peers <= 32:
        parser.error("maximum controller peers must be 1..32")
    sessions = root.findall("session")
    if not sessions:
        parser.error("remote control requires at least one explicit session")
    control_attributes = {
        "max-controller-peers": str(args.max_controller_peers),
        "session": sessions[0].get("name")
    }
    if args.telemetry:
        control_attributes["minimum-telemetry-period-ms"] = "100"
    control = ET.SubElement(root, "control", control_attributes)
    controlled_connections = {"can", "gateway"}
    for connection in root.findall("connection") + root.findall("native-connection"):
        connection_id = connection.get("id")
        if connection_id not in controlled_connections:
            continue
        ET.SubElement(control, "resource", {
            "kind": "connection", "ref": connection_id, "actions": "up down"})
        for stream in connection.findall("stream"):
            role = stream.get("role")
            if role == "reader":
                ET.SubElement(control, "resource", {
                    "kind": "input",
                    "ref": f"{connection_id}::{stream.get('name')}",
                    "actions": "enable disable"})
            elif role == "writer":
                ET.SubElement(control, "resource", {
                    "kind": "output",
                    "ref": f"{connection_id}::{stream.get('name')}",
                    "actions": "enable disable"})
    for session in sessions:
        for route in session.findall("route"):
            ET.SubElement(control, "resource", {
                "kind": "route", "ref": route.get("id"), "actions": "pause resume"})
    if args.telemetry:
        ET.SubElement(control, "metric", {
            "resource-kind": "connection",
            "resource": "can",
            "name": "received_frames",
        })
elif args.telemetry:
    parser.error("--telemetry requires --remote-control")
ET.indent(root)
ET.ElementTree(root).write(args.output, encoding="utf-8", xml_declaration=True)

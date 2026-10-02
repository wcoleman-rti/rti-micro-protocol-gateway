#!/usr/bin/env python3
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

"""Inject only application key cardinality and type declarations; MAG owns topology."""
import argparse
import collections
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("--template", required=True)
parser.add_argument("--inventory", required=True)
parser.add_argument("--types", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--domain", type=int, required=True)
parser.add_argument("--diagnostics", choices=("0", "1"), default="1")
args = parser.parse_args()
if not 1 <= args.domain <= 232:
    parser.error("DDS domain must be 1..232")
manifest = json.loads(Path(args.inventory).read_text())
counts = collections.Counter(s["category"] for s in manifest["signals"])
substitutions = dict(counts, domain=args.domain)
substitutions.update({f"{category}_reader_samples": count * 2
                      for category, count in counts.items()})
text = Path(args.template).read_text()
for key in re.findall(r"@([A-Za-z_][A-Za-z0-9_]*)@", text):
    if key not in substitutions or substitutions[key] <= 0:
        parser.error(f"unresolved/empty workload category {key}")
    text = text.replace(f"@{key}@", str(substitutions[key]))
root = ET.fromstring(text)
if args.diagnostics == "0":
    for parent in root.iter():
        for child in list(parent):
            if child.get("name") == "Diagnostics" or (
                child.tag == "register_type" and child.get("name") == "Snapshot"):
                parent.remove(child)
types = ET.parse(args.types).getroot().find("types")
if types is None:
    parser.error("generated type XML has no types")
declared = set()
def visit(node, namespace=()):
    for child in node:
        if child.tag == "module":
            visit(child, namespace + (child.get("name"),))
        elif child.tag in ("struct", "union", "enum"):
            name = "::".join(namespace + (child.get("name"),))
            if name in declared:
                parser.error(f"duplicate generated type {name}")
            declared.add(name)
visit(types)
for registration in root.iter("register_type"):
    if registration.get("type_ref") not in declared:
        parser.error(f"unresolved compiled IDL type {registration.get('type_ref')}")
# MAG obtains type plugins from the IDL-generated headers. Its 4.3.0 loader
# warns and ignores <types>; never pass that unsupported section to MAG.
ET.indent(root)
ET.ElementTree(root).write(args.output, encoding="utf-8", xml_declaration=True)

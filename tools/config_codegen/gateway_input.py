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
    root.set("diagnostic-period-steps", "0")
    for parent in root.iter():
        for child in list(parent):
            if child.get("id") == "diagnostics" or child.get("binding") == "diagnostics":
                parent.remove(child)
ET.indent(root)
ET.ElementTree(root).write(args.output, encoding="utf-8", xml_declaration=True)

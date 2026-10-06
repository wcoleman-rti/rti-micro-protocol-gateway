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
parser.add_argument("--gateway")
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
if args.gateway:
    gateway = ET.parse(args.gateway).getroot()
    control = gateway.find("control")
    if control is not None:
        resource_count = len(control.findall("resource"))
        metric_count = len(control.findall("metric"))
        telemetry_selected = metric_count > 0
        if not resource_count:
            parser.error("remote control requires at least one selected resource")
        domain = root.find("domain_library/domain[@name='Private']")
        participants = root.find("domain_participant_library")
        qos_library = root.find("qos_library")
        if domain is None or participants is None or qos_library is None:
            parser.error("control endpoints require the Private DDS domain and libraries")
        registrations = {entry.get("name"): entry.get("type_ref")
                         for entry in domain.findall("register_type")}
        topics = {entry.get("name") for entry in domain.findall("topic")}
        control_types = (
            ("ControlCommand", "PGW_ControlService::Command"),
            ("ControlState", "PGW_ControlService::State"),
            ("ControlResult", "PGW_ControlService::Result"),
        )
        for name, type_ref in control_types:
            if type_ref not in declared:
                parser.error(f"control IDL type {type_ref} is absent from generated IDL")
            if name in registrations and registrations[name] != type_ref:
                parser.error(f"conflicting registered control type {name}")
            if name not in registrations:
                ET.SubElement(domain, "register_type", {
                    "name": name, "type_ref": type_ref})
        if telemetry_selected:
            telemetry_type = "PGW_ControlService::Telemetry"
            if telemetry_type not in declared:
                parser.error(f"control IDL type {telemetry_type} is absent from generated IDL")
            if "ControlTelemetry" in registrations and \
                    registrations["ControlTelemetry"] != telemetry_type:
                parser.error("conflicting registered control telemetry type")
            if "ControlTelemetry" not in registrations:
                ET.SubElement(domain, "register_type", {
                    "name": "ControlTelemetry", "type_ref": telemetry_type})
        for name, _ in control_types:
            if name in topics:
                parser.error(f"conflicting control topic {name}")
            ET.SubElement(domain, "topic", {
                "name": name, "register_type_ref": name})
        if telemetry_selected:
            if "ControlTelemetry" in topics:
                parser.error("conflicting control telemetry topic")
            ET.SubElement(domain, "topic", {
                "name": "ControlTelemetry", "register_type_ref": "ControlTelemetry"})
        def add_profile(name, role, depth, instances, samples, per_instance,
                        durability, reliability_kind="RELIABLE_RELIABILITY_QOS"):
            profile = ET.SubElement(qos_library, "qos_profile", {
                "name": name, "base_name": "Bounded"})
            qos = ET.SubElement(profile, "datawriter_qos" if role == "writer" else "datareader_qos")
            history = ET.SubElement(qos, "history")
            ET.SubElement(history, "kind").text = "KEEP_LAST_HISTORY_QOS"
            ET.SubElement(history, "depth").text = str(depth)
            limits = ET.SubElement(qos, "resource_limits")
            ET.SubElement(limits, "max_instances").text = str(instances)
            ET.SubElement(limits, "max_samples").text = str(samples)
            ET.SubElement(limits, "max_samples_per_instance").text = str(per_instance)
            reliability = ET.SubElement(qos, "reliability")
            ET.SubElement(reliability, "kind").text = reliability_kind
            if role == "writer":
                blocking = ET.SubElement(reliability, "max_blocking_time")
                ET.SubElement(blocking, "sec").text = "0"
                ET.SubElement(blocking, "nanosec").text = "0"
            durability_qos = ET.SubElement(qos, "durability")
            ET.SubElement(durability_qos, "kind").text = durability
        add_profile("ControlCommand", "reader", 4, 1, 4, 4,
                    "VOLATILE_DURABILITY_QOS")
        add_profile("ControlCommandWriter", "writer", 4, 1, 4, 4,
                    "VOLATILE_DURABILITY_QOS")
        add_profile("ControlResult", "writer", 4, 1, 4, 4,
                    "VOLATILE_DURABILITY_QOS")
        add_profile("ControlState", "writer", 1, resource_count, resource_count, 1,
                    "TRANSIENT_LOCAL_DURABILITY_QOS")
        add_profile("ControlResultReader", "reader", 4, 1, 4, 4,
                    "VOLATILE_DURABILITY_QOS")
        add_profile("ControlStateReader", "reader", 1, resource_count, resource_count, 1,
                    "TRANSIENT_LOCAL_DURABILITY_QOS")
        if telemetry_selected:
            add_profile("ControlTelemetry", "writer", 1, metric_count, metric_count, 1,
                        "VOLATILE_DURABILITY_QOS", "BEST_EFFORT_RELIABILITY_QOS")
            add_profile("ControlTelemetryReader", "reader", 1, metric_count,
                        metric_count, 1, "VOLATILE_DURABILITY_QOS",
                        "BEST_EFFORT_RELIABILITY_QOS")
        participant = ET.SubElement(participants, "domain_participant", {
            "name": "RemoteControl", "domain_ref": "GatewayDomains::Private"})
        publisher = ET.SubElement(participant, "publisher", {"name": "Out"})
        for name in ("ControlState", "ControlResult"):
            writer = ET.SubElement(publisher, "data_writer", {
                "name": name, "topic_ref": name})
            ET.SubElement(writer, "datawriter_qos", {
                "base_name": f"GatewayQos::{name}"})
        if telemetry_selected:
            telemetry_writer = ET.SubElement(publisher, "data_writer", {
                "name": "ControlTelemetry", "topic_ref": "ControlTelemetry"})
            ET.SubElement(telemetry_writer, "datawriter_qos", {
                "base_name": "GatewayQos::ControlTelemetry"})
        subscriber = ET.SubElement(participant, "subscriber", {"name": "In"})
        reader = ET.SubElement(subscriber, "data_reader", {
            "name": "ControlCommand", "topic_ref": "ControlCommand"})
        ET.SubElement(reader, "datareader_qos", {
            "base_name": "GatewayQos::ControlCommand"})
        participant_qos = ET.SubElement(participant, "domain_participant_qos", {
            "base_name": "GatewayQos::Bounded"})
        controller = ET.SubElement(participants, "domain_participant", {
            "name": "RemoteController", "domain_ref": "GatewayDomains::Private"})
        controller_publisher = ET.SubElement(controller, "publisher", {"name": "Out"})
        command_writer = ET.SubElement(controller_publisher, "data_writer", {
            "name": "ControlCommand", "topic_ref": "ControlCommand"})
        ET.SubElement(command_writer, "datawriter_qos", {
            "base_name": "GatewayQos::ControlCommandWriter"})
        controller_subscriber = ET.SubElement(controller, "subscriber", {"name": "In"})
        for name in ("ControlState", "ControlResult"):
            reader = ET.SubElement(controller_subscriber, "data_reader", {
                "name": name, "topic_ref": name})
            ET.SubElement(reader, "datareader_qos", {
                "base_name": f"GatewayQos::{name}Reader"})
        if telemetry_selected:
            telemetry_reader = ET.SubElement(controller_subscriber, "data_reader", {
                "name": "ControlTelemetry", "topic_ref": "ControlTelemetry"})
            ET.SubElement(telemetry_reader, "datareader_qos", {
                "base_name": "GatewayQos::ControlTelemetryReader"})
        controller_qos = ET.SubElement(controller, "domain_participant_qos", {
            "base_name": "GatewayQos::Bounded"})
        controller_limits = ET.SubElement(controller_qos, "resource_limits")
        for name, value in (
            ("local_writer_allocation", 1),
            ("local_reader_allocation", 2 + int(telemetry_selected)),
            ("local_publisher_allocation", 1),
            ("local_subscriber_allocation", 1),
            ("local_topic_allocation", 3 + int(telemetry_selected)),
            ("local_type_allocation", 3 + int(telemetry_selected)),
            ("remote_participant_allocation", 1),
            ("remote_writer_allocation", 2 + int(telemetry_selected)),
            ("remote_reader_allocation", 1),
            ("matching_writer_reader_pair_allocation", 3 + int(telemetry_selected)),
            ("matching_reader_writer_pair_allocation", 3 + int(telemetry_selected)),
        ):
            allocation = ET.SubElement(controller_limits, name)
            ET.SubElement(allocation, "max_count").text = str(value)
        limits = ET.SubElement(participant_qos, "resource_limits")
        for name, value in (
            ("local_writer_allocation", 2 + int(telemetry_selected)),
            ("local_reader_allocation", 1),
            ("local_publisher_allocation", 1),
            ("local_subscriber_allocation", 1),
            ("local_topic_allocation", 3 + int(telemetry_selected)),
            ("local_type_allocation", 3 + int(telemetry_selected)),
            ("remote_participant_allocation", 1),
            ("remote_writer_allocation", 1),
            ("remote_reader_allocation", 2 + int(telemetry_selected)),
            ("matching_writer_reader_pair_allocation", 3 + int(telemetry_selected)),
            ("matching_reader_writer_pair_allocation", 3 + int(telemetry_selected)),
        ):
            allocation = ET.SubElement(limits, name)
            ET.SubElement(allocation, "max_count").text = str(value)
# MAG obtains type plugins from the IDL-generated headers. Its 4.3.0 loader
# warns and ignores <types>; never pass that unsupported section to MAG.
ET.indent(root)
ET.ElementTree(root).write(args.output, encoding="utf-8", xml_declaration=True)

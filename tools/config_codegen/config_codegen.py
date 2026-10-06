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

"""Strict host-only gateway compiler. DDS topology resource sizing belongs to MAG."""
import argparse
import json
import re
from pathlib import Path
import xml.etree.ElementTree as ET
from lxml import etree


class ConfigError(ValueError):
    pass


CONTROL_ACTIONS = {
    "connection": {"up": 0, "down": 1},
    "input": {"enable": 2, "disable": 3},
    "output": {"enable": 4, "disable": 5},
    "route": {"pause": 6, "resume": 7},
}
CONTROL_SCALAR_TYPES = {
    "boolean": "BOOLEAN_VALUE",
    "int32": "INT32_VALUE",
    "uint32": "UINT32_VALUE",
    "int64": "INT64_VALUE",
    "uint64": "UINT64_VALUE",
    "double": "DOUBLE_VALUE",
}


def parse(path):
    raw = Path(path).read_bytes()
    if b"<!DOCTYPE" in raw or b"<!ENTITY" in raw:
        raise ConfigError("DTD/entities are forbidden")
    try:
        return ET.fromstring(raw)
    except ET.ParseError as exc:
        raise ConfigError(str(exc)) from exc


def shape(node, required, optional=()):
    unknown = set(node.attrib) - set(required) - set(optional)
    missing = set(required) - set(node.attrib)
    if unknown or missing or (node.text and node.text.strip()):
        raise ConfigError(f"{node.tag}: unknown={sorted(unknown)}, missing={sorted(missing)}")
    if node.tail and node.tail.strip():
        raise ConfigError(f"{node.tag}: unexpected text")
    if any(not value for value in node.attrib.values()):
        raise ConfigError(f"{node.tag}: empty attribute")


def identifier(value):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value):
        raise ConfigError(f"unsafe identifier {value!r}")
    return value


def positive(value):
    if not re.fullmatch(r"[1-9][0-9]*", value) or int(value) > 65535:
        raise ConfigError(f"unsafe/unbounded capacity {value!r}")
    return int(value)


def named_children(parent, tag):
    result = {}
    for child in parent.findall(tag):
        name = child.get("name")
        if not name or name in result:
            raise ConfigError(f"missing/duplicate DDS {tag} name")
        result[name] = child
    return result


def endpoint_qos(model, endpoint, role):
    profiles = {}
    for library in model.findall("qos_library"):
        for profile in named_children(library, "qos_profile").values():
            key = library.get("name") + "::" + profile.get("name")
            if key in profiles:
                raise ConfigError("duplicate QoS profile")
            profiles[key] = profile
    tag = "datareader_qos" if role == "reader" else "datawriter_qos"
    qos = endpoint.find(tag)
    if qos is None:
        raise ConfigError("explicit endpoint QoS reference required")
    sections = [qos]
    base = qos.get("base_name")
    visited = set()
    while base:
        if base in visited or base not in profiles:
            raise ConfigError("cyclic/unresolved QoS inheritance")
        visited.add(base)
        profile = profiles[base]
        section = profile.find(tag)
        if section is not None:
            sections.append(section)
        parent = profile.get("base_name")
        if parent and "::" not in parent:
            parent = base.split("::")[0] + "::" + parent
        base = parent
    def field(path):
        for section in sections:
            value = section.findtext(path)
            if value is not None:
                return value.strip()
        raise ConfigError(f"explicit bounded {tag}/{path} required")
    if field("history/kind") != "KEEP_LAST_HISTORY_QOS" or positive(field("history/depth")) != 1:
        raise ConfigError("latest-state KEEP_LAST depth 1 required")
    instances = positive(field("resource_limits/max_instances"))
    samples = positive(field("resource_limits/max_samples"))
    per_instance = positive(field("resource_limits/max_samples_per_instance"))
    allowed_per_instance = (1,) if role == "writer" else (1, 2)
    if per_instance not in allowed_per_instance or samples < instances * per_instance:
        raise ConfigError("inconsistent bounded keyed history")
    if role == "writer" and (field("reliability/max_blocking_time/sec") != "0" or
                            field("reliability/max_blocking_time/nanosec") != "0"):
        raise ConfigError("zero writer blocking time required")


def parse_adapter_manifest(path):
    schema_path = (Path(__file__).resolve().parents[2] / "resources" / "schema" /
                   "adapter-library.xsd")
    schema = etree.XMLSchema(etree.parse(str(schema_path)))
    xml_parser = etree.XMLParser(resolve_entities=False, no_network=True, load_dtd=False)
    try:
        document = etree.parse(str(path), xml_parser)
        schema.assertValid(document)
    except (etree.XMLSyntaxError, etree.DocumentInvalid) as exc:
        raise ConfigError(f"adapter manifest XSD: {exc}") from exc
    root = parse(path)
    shape(root, ("name", "version", "control-api-version"))
    name = identifier(root.get("name"))
    if root.get("version") != "1" or root.get("control-api-version") != "1":
        raise ConfigError(f"unsupported adapter manifest version for {name}")
    capabilities = {}
    metrics = {}
    for section in root:
        for child in section:
            if section.tag == "control" and child.tag == "capability":
                kind = child.get("kind")
                if kind not in CONTROL_ACTIONS or kind in capabilities:
                    raise ConfigError(f"invalid/duplicate adapter capability kind {kind!r}")
                actions = child.get("actions").split("|")
                if not actions or len(actions) != len(set(actions)) or \
                        any(action not in CONTROL_ACTIONS[kind] for action in actions):
                    raise ConfigError(f"invalid {kind} actions in adapter manifest {name}")
                capabilities[kind] = {
                    "actions": actions,
                    "mask": sum(1 << CONTROL_ACTIONS[kind][action] for action in actions),
                }
            elif section.tag == "telemetry" and child.tag == "metric":
                kind = child.get("resource-kind")
                metric_name = identifier(child.get("name"))
                scalar = child.get("scalar")
                unit = child.get("unit")
                key = (kind, metric_name)
                if kind not in CONTROL_ACTIONS or scalar not in CONTROL_SCALAR_TYPES or \
                        not unit or len(unit.encode("utf-8")) > 32 or key in metrics:
                    raise ConfigError(f"invalid/duplicate telemetry metric {kind}:{metric_name}")
                metrics[key] = {"resource_kind": kind, "name": metric_name,
                                "scalar": scalar, "unit": unit}
            else:
                raise ConfigError(f"unknown adapter manifest element {child.tag}")
    ordered_metrics = sorted(metrics.values(), key=lambda item:
                             (item["resource_kind"], item["name"]))
    if len(ordered_metrics) > 32:
        raise ConfigError(f"adapter {name} declares more than 32 telemetry metrics")
    for bit, metric in enumerate(ordered_metrics):
        metrics[(metric["resource_kind"], metric["name"])]["bit"] = bit
    return name, {"capabilities": capabilities, "metrics": metrics,
                  "path": str(Path(path).resolve())}


def validate_gateway_xml(gateway):
    schema_path = Path(__file__).resolve().parents[2] / "resources" / "schema" / "gateway.xsd"
    schema = etree.XMLSchema(etree.parse(str(schema_path)))
    xml_parser = etree.XMLParser(resolve_entities=False, no_network=True, load_dtd=False)
    try:
        document = etree.parse(str(gateway), xml_parser)
        schema.assertValid(document)
    except (etree.XMLSyntaxError, etree.DocumentInvalid) as exc:
        raise ConfigError(f"gateway XSD: {exc}") from exc
    return parse(gateway)


def control_catalog(root):
    route_names = set()
    connection_adapters = {}
    stream_adapters = {}
    stream_roles = {}
    for node in root:
        if node.tag == "route":
            route_names.add(identifier(node.get("id", "")))
        elif node.tag in ("connection", "native-connection"):
            connection = identifier(node.get("id", ""))
            adapter = identifier(node.get("adapter", ""))
            connection_adapters[connection] = adapter
            for stream in node.findall("stream"):
                stream_name = identifier(stream.get("name", ""))
                key = f"{connection}::{stream_name}"
                stream_adapters[key] = adapter
                stream_roles[key] = stream.get("role")
    return route_names, connection_adapters, stream_adapters, stream_roles


def compile_control_resources(control_node, route_names, connection_adapters,
                              stream_adapters, stream_roles, adapter_manifests):
    if control_node is None:
        return [], [], 0, 0
    shape(control_node, (), (
        "max-controller-peers", "minimum-telemetry-period-ms"))
    controller_peer_limit = positive(control_node.get("max-controller-peers", "1"))
    if controller_peer_limit > 32:
        raise ConfigError("maximum controller peers must be 1..32")
    manifests = {}
    for manifest_path in adapter_manifests:
        adapter_name, metadata = parse_adapter_manifest(manifest_path)
        if adapter_name in manifests:
            raise ConfigError(f"duplicate adapter manifest {adapter_name!r}")
        manifests[adapter_name] = metadata
    selected = set()
    resources = []
    selections = []
    for child in control_node.findall("resource"):
        if len(child):
            raise ConfigError("control resources must be leaf elements")
        shape(child, ("kind", "ref", "actions"))
        kind, reference = child.get("kind"), child.get("ref")
        if kind not in CONTROL_ACTIONS:
            raise ConfigError(f"unsupported control resource kind {kind!r}")
        actions = child.get("actions").split()
        if not actions or len(actions) != len(set(actions)) or \
                any(action not in CONTROL_ACTIONS[kind] for action in actions):
            raise ConfigError(f"invalid actions for control resource {kind}:{reference}")
        adapter_name = None
        if kind == "route":
            if reference not in route_names:
                raise ConfigError(f"unresolved control route {reference!r}")
            if set(actions) != {"pause", "resume"}:
                raise ConfigError("routes support exactly pause and resume")
        elif kind == "connection":
            adapter_name = connection_adapters.get(reference)
            if not adapter_name:
                raise ConfigError(f"unresolved control connection {reference!r}")
        else:
            if reference not in stream_adapters:
                raise ConfigError(f"unresolved control stream {reference!r}")
            required_role = "reader" if kind == "input" else "writer"
            if stream_roles[reference] != required_role:
                raise ConfigError(f"control {kind} must select a {required_role} stream")
            adapter_name = stream_adapters[reference]
        if adapter_name:
            manifest = manifests.get(adapter_name)
            capability = manifest and manifest["capabilities"].get(kind)
            if not capability:
                raise ConfigError(
                    f"missing linked {kind} capability manifest for adapter {adapter_name!r}")
            if not set(actions).issubset(capability["actions"]):
                raise ConfigError(f"adapter {adapter_name!r} does not support {kind} actions")
        key = (kind, reference)
        if key in selected:
            raise ConfigError(f"duplicate control resource {kind}:{reference}")
        selected.add(key)
        name_parts = reference.split("::")
        if len(name_parts) > 2 or any(
                not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", part)
                for part in name_parts):
            raise ConfigError(f"unsafe control resource reference {reference!r}")
        enum_name = "RESOURCE_" + kind.upper() + "_" + "_".join(
            part.upper() for part in name_parts)
        if not re.fullmatch(r"[A-Z_][A-Z0-9_]*", enum_name):
            raise ConfigError(f"unsafe normalized control resource {enum_name!r}")
        action_mask = sum(1 << CONTROL_ACTIONS[kind][action] for action in actions)
        resources.append({
            "kind": kind,
            "ref": reference,
            "adapter": adapter_name or "core",
            "enum": enum_name,
            "command_capabilities": action_mask,
            "telemetry_capabilities": 0,
        })
    if not resources:
        raise ConfigError("empty control section")
    enum_names = [resource["enum"] for resource in resources]
    if len(enum_names) != len(set(enum_names)):
        raise ConfigError("control resources collide after IDL name normalization")
    resources.sort(key=lambda resource: (resource["kind"], resource["ref"]))
    resource_ids = {(resource["kind"], resource["ref"]): index
                    for index, resource in enumerate(resources)}
    telemetry_by_name = {}
    telemetry_pairs = set()
    for child in control_node.findall("metric"):
        if len(child):
            raise ConfigError("control telemetry metrics must be leaf elements")
        shape(child, ("resource-kind", "resource", "name"))
        kind = child.get("resource-kind")
        reference = child.get("resource")
        metric_name = identifier(child.get("name"))
        key = (kind, reference)
        if key not in resource_ids or kind not in ("connection", "input", "output"):
            raise ConfigError(f"telemetry metric resource is not selected: {kind}:{reference}")
        resource = resources[resource_ids[key]]
        adapter_name = resource["adapter"]
        manifest = manifests.get(adapter_name)
        metadata = manifest and manifest["metrics"].get((kind, metric_name))
        if not metadata:
            raise ConfigError(
                f"missing linked telemetry metric {metric_name!r} for {adapter_name!r}")
        pair = (kind, reference, metric_name)
        if pair in telemetry_pairs:
            raise ConfigError(f"duplicate telemetry selection {pair}")
        telemetry_pairs.add(pair)
        enum_name = "TELEMETRY_" + metric_name.upper()
        if not re.fullmatch(r"[A-Z_][A-Z0-9_]*", enum_name):
            raise ConfigError(f"unsafe normalized telemetry kind {enum_name!r}")
        prior = telemetry_by_name.get(metric_name)
        signature = (metadata["scalar"], metadata["unit"])
        if prior and prior["signature"] != signature:
            raise ConfigError(
                f"telemetry kind {metric_name!r} has conflicting scalar or unit metadata")
        telemetry_by_name[metric_name] = {
            "enum": enum_name,
            "signature": signature,
            "scalar": metadata["scalar"],
            "unit": metadata["unit"],
        }
        selections.append({
            "resource_kind": kind,
            "resource_ref": reference,
            "resource_id": resource_ids[key],
            "adapter": adapter_name,
            "name": metric_name,
            "adapter_metric_bit": metadata["bit"],
            "scalar": metadata["scalar"],
            "unit": metadata["unit"],
            "enum": enum_name,
        })
    kind_names = sorted({item["enum"] for item in telemetry_by_name.values()})
    if len(kind_names) > 32:
        raise ConfigError("at most 32 telemetry kinds may be selected per service")
    kind_ids = {name: index for index, name in enumerate(kind_names)}
    for resource in resources:
        resource["telemetry_capabilities"] = 0
    selections.sort(key=lambda item:
                    (item["resource_id"], kind_ids[item["enum"]]))
    for metric_id, selection in enumerate(selections):
        selection["id"] = metric_id
        selection["telemetry_kind"] = kind_ids[selection["enum"]]
        resources[selection["resource_id"]]["telemetry_capabilities"] |= (
            1 << selection["telemetry_kind"])
    minimum_period = control_node.get("minimum-telemetry-period-ms", "0")
    if selections:
        minimum_period = positive(minimum_period)
    elif minimum_period != "0":
        raise ConfigError("telemetry period ceiling requires a selected telemetry metric")
    return resources, selections, int(minimum_period), controller_peer_limit


def compile_config(gateway, dds=None, inventory=None, remote_control=False,
                   adapter_manifests=()):
    root = validate_gateway_xml(gateway)
    if root.tag != "gateway":
        raise ConfigError("root must be gateway")
    shape(root, ("dds", "route-budget", "sample-budget"), ("diagnostic-period-steps",))
    positive(root.get("route-budget"))
    positive(root.get("sample-budget"))
    period = root.get("diagnostic-period-steps", "0")
    if period != "0":
        positive(period)
    dds_path = Path(dds) if dds else Path(gateway).parent / root.get("dds")
    model = parse(dds_path)
    if model.tag != "dds":
        raise ConfigError("DDS root must be dds")
    for tag in ("domain_library", "domain_participant_library", "qos_library"):
        named_children(model, tag)
    participants = {}
    domains = {}
    for library in model.findall("domain_library"):
        for domain in named_children(library, "domain").values():
            key = library.get("name") + "::" + domain.get("name")
            if key in domains:
                raise ConfigError("duplicate DDS domain")
            domains[key] = domain
    for library in model.findall("domain_participant_library"):
        for participant in named_children(library, "domain_participant").values():
            key = library.get("name") + "::" + participant.get("name")
            if key in participants:
                raise ConfigError("duplicate DDS participant")
            participants[key] = participant
    bindings, connections, streams, routes, ids, natives = {}, [], {}, [], set(), []
    route_names, connection_adapters, stream_adapters, stream_roles = set(), {}, {}, {}
    phase = 0
    native_limits = {}
    for node in root:
        expected_phase = {"binding": 0, "connection": 1,
                          "native-connection": 1, "route": 2, "control": 3}.get(node.tag)
        if expected_phase is None or expected_phase < phase:
            raise ConfigError(f"unknown/out-of-order element {node.tag}")
        phase = expected_phase
        if node.tag == "control":
            continue
        name = identifier(node.get("id", ""))
        if name in ids:
            raise ConfigError(f"duplicate id {name}")
        ids.add(name)
        if node.tag == "binding":
            binding_generation_attributes = (
                "schema-version", "representation-name", "native-type",
                "native-header", "support-header", "conversion",
                "dds-to-native", "native-to-dds", "register-keys",
                "register-key-value", "sample-copy", "supports-timestamp",
            )
            shape(node, ("id", "symbol", "type", "schema", "fingerprint"),
                  binding_generation_attributes)
            if len(node):
                raise ConfigError("binding children forbidden")
            identifier(node.get("symbol"))
            if not re.fullmatch(r"[0-9a-f]{64}", node.get("fingerprint")):
                raise ConfigError("fingerprint must be SHA256")
            generation_attributes = set(binding_generation_attributes) - {
                "dds-to-native", "native-to-dds", "register-keys",
                "register-key-value", "sample-copy", "supports-timestamp"}
            if generation_attributes.intersection(node.attrib) and \
                    not generation_attributes.issubset(node.attrib):
                raise ConfigError(
                    f"binding {name} must provide a complete generated DDS binding declaration")
            if node.get("native-type"):
                identifier(node.get("native-type"))
            if node.get("conversion") not in (None, "fieldwise", "callbacks"):
                raise ConfigError(f"binding {name} has unsupported conversion mode")
            converter_attributes = {"dds-to-native", "native-to-dds"}
            if node.get("conversion") == "callbacks" and \
                    not converter_attributes.issubset(node.attrib):
                raise ConfigError(
                    f"binding {name} callback conversion needs DDS/native converter names")
            if node.get("conversion") == "fieldwise" and \
                    converter_attributes.intersection(node.attrib):
                raise ConfigError(
                    f"binding {name} fieldwise conversion cannot specify converter callbacks")
            if bool(node.get("dds-to-native")) != bool(node.get("native-to-dds")):
                raise ConfigError(f"binding {name} must specify both converter callbacks")
            if node.get("dds-to-native"):
                identifier(node.get("dds-to-native"))
            if node.get("native-to-dds"):
                identifier(node.get("native-to-dds"))
            if node.get("register-keys"):
                identifier(node.get("register-keys"))
            if node.get("register-keys") and node.get("register-key-value"):
                raise ConfigError(f"binding {name} has conflicting key registration policies")
            if node.get("sample-copy"):
                identifier(node.get("sample-copy"))
            if node.get("supports-timestamp", "false") not in ("true", "false", "1", "0"):
                raise ConfigError(f"binding {name} has invalid supports-timestamp value")
            bindings[name] = dict(node.attrib)
        elif node.tag == "connection":
            shape(node, ("id", "participant", "adapter"))
            connection_adapters[name] = identifier(node.get("adapter"))
            participant = participants.get(node.get("participant"))
            if participant is None:
                raise ConfigError("unresolved participant")
            domain = domains.get(participant.get("domain_ref"))
            if domain is None:
                raise ConfigError("unresolved DDS domain")
            topics = {name: topic.get("register_type_ref")
                      for name, topic in named_children(domain, "topic").items()}
            types = {name: registration.get("type_ref")
                     for name, registration in named_children(domain, "register_type").items()}
            endpoints = {}
            for parent_tag, tag, role in (("subscriber", "data_reader", "reader"),
                                          ("publisher", "data_writer", "writer")):
                for parent in named_children(participant, parent_tag).values():
                    for endpoint in parent.findall(tag):
                        endpoint_name = parent.get("name") + "::" + endpoint.get("name")
                        if endpoint_name in endpoints:
                            raise ConfigError("duplicate DDS endpoint")
                        endpoints[endpoint_name] = (role, endpoint.get("topic_ref"), endpoint)
            entries = []
            adopted = set()
            for stream in node:
                if stream.tag != "stream" or len(stream):
                    raise ConfigError("connection permits only leaf streams")
                shape(stream, ("name", "endpoint", "binding", "role", "capacity"),
                      ("preserve-source-timestamp",))
                sn = name + "::" + identifier(stream.get("name"))
                if sn in streams:
                    raise ConfigError("duplicate stream")
                binding = bindings.get(stream.get("binding"))
                endpoint = endpoints.get(stream.get("endpoint"))
                if not binding or not endpoint:
                    raise ConfigError("unresolved binding/endpoint")
                if stream.get("endpoint") in adopted:
                    raise ConfigError("endpoint adopted twice")
                adopted.add(stream.get("endpoint"))
                if endpoint[0] != stream.get("role"):
                    raise ConfigError("wrong endpoint role")
                if types.get(topics.get(endpoint[1])) != binding["type"]:
                    raise ConfigError("DDS endpoint type does not match binding")
                endpoint_qos(model, endpoint[2], endpoint[0])
                preserve = stream.get("preserve-source-timestamp", "false")
                if preserve not in ("false", "true", "0", "1"):
                    raise ConfigError("invalid timestamp policy")
                if endpoint[0] == "reader" and preserve in ("true", "1"):
                    raise ConfigError("timestamp policy is writer-only")
                positive(stream.get("capacity"))
                entry = dict(stream.attrib)
                entry["symbol"] = binding["symbol"]
                entries.append(entry)
                streams[sn] = (stream.get("role"), binding["schema"], binding["fingerprint"])
                stream_adapters[sn] = connection_adapters[name]
                stream_roles[sn] = stream.get("role")
            if not entries:
                raise ConfigError("empty connection")
            connections.append((name, node.get("participant"), entries))
        elif node.tag == "native-connection":
            shape(node, ("id", "adapter", "receive-budget", "write-capacity"))
            connection_adapters[name] = identifier(node.get("adapter"))
            if node.get("adapter") != "can":
                raise ConfigError("unregistered native adapter")
            if not len(node):
                raise ConfigError("empty native connection")
            native_limits[name + "_receive_budget"] = positive(node.get("receive-budget"))
            native_limits[name + "_write_capacity"] = positive(node.get("write-capacity"))
            for stream in node:
                shape(stream, ("name", "endpoint", "binding", "role", "capacity"))
                if stream.tag != "stream" or len(stream):
                    raise ConfigError("native connection permits only leaf streams")
                sn = name + "::" + identifier(stream.get("name"))
                identifier(stream.get("endpoint"))
                binding = bindings.get(stream.get("binding"))
                if not binding or sn in streams:
                    raise ConfigError("unresolved binding/duplicate native stream")
                positive(stream.get("capacity"))
                streams[sn] = (stream.get("role"), binding["schema"], binding["fingerprint"])
                stream_adapters[sn] = connection_adapters[name]
                stream_roles[sn] = stream.get("role")
                natives.append(dict(stream.attrib, connection=name))
        else:
            shape(node, ("id", "input", "output"))
            if len(node):
                raise ConfigError("route children forbidden")
            source, dest = streams.get(node.get("input")), streams.get(node.get("output"))
            if not source or not dest or source[0] != "reader" or dest[0] != "writer":
                raise ConfigError("unresolved/wrong-role route")
            if source[1:] != dest[1:]:
                raise ConfigError("incompatible route schema/fingerprint")
            routes.append(dict(node.attrib))
            route_names.add(name)
    if not bindings or not connections:
        raise ConfigError("bindings and connections required")
    control_node = root.find("control")
    if control_node is not None and not remote_control:
        raise ConfigError("remote control section requires PGW_ENABLE_REMOTE_CONTROL")
    (control_resources, control_metrics, telemetry_minimum_period_ms,
     controller_peer_limit) = compile_control_resources(
        control_node, route_names, connection_adapters, stream_adapters,
        stream_roles, adapter_manifests)
    if inventory:
        manifest = json.loads(Path(inventory).read_text())
        for binding in bindings.values():
            if binding["schema"] == manifest["schema"] and binding["fingerprint"] != manifest["fingerprint"]:
                raise ConfigError("stale schema fingerprint")
    return (bindings, connections, routes, {
        "route_budget": int(root.get("route-budget")),
        "sample_budget": int(root.get("sample-budget")),
        "diagnostic_period_steps": int(period), **native_limits}, natives,
            control_resources, control_metrics, telemetry_minimum_period_ms,
            controller_peer_limit)


def emit(config, output):
    (bindings, connections, routes, settings, natives, control_resources,
     control_metrics, telemetry_minimum_period_ms, controller_peer_limit) = config
    lines = ['#include "pgw/dds/connext_micro.h"', '#include "pgw/compiled_config.h"']
    for name, value in settings.items():
        lines.append(f"const unsigned pgw_config_{name} = {value}u;")
    if control_resources:
        lines.append("const unsigned pgw_control_max_controller_peers = "
                     f"{controller_peer_limit}u;")
    if control_resources:
        lines.append("const PGW_CompiledControlResource pgw_control_resources[] = {")
        for index, resource in enumerate(control_resources):
            lines.append("    {%du, %s, %s, %s, %su, %su}," % (
                index, json.dumps(resource["kind"]), json.dumps(resource["ref"]),
                json.dumps(resource["adapter"]),
                resource["command_capabilities"], resource["telemetry_capabilities"]))
        lines.append("};")
        lines.append("const size_t pgw_control_resource_count = "
                     "sizeof(pgw_control_resources) / sizeof(pgw_control_resources[0]);")
    if control_metrics:
        scalar_ids = {name: index for index, name in enumerate(CONTROL_SCALAR_TYPES)}
        lines.append("const PGW_CompiledControlTelemetry pgw_control_telemetry_metrics[] = {")
        for metric in control_metrics:
            lines.append("    {%du, %du, %du, %du, %s, %s, %s, %s, %s, %du}," % (
                metric["id"], metric["resource_id"], metric["telemetry_kind"],
                metric["adapter_metric_bit"], json.dumps(metric["resource_kind"]),
                json.dumps(metric["resource_ref"]), json.dumps(metric["name"]),
                json.dumps(metric["unit"]), json.dumps(metric["adapter"]),
                scalar_ids[metric["scalar"]]))
        lines.append("};")
        lines.append("const size_t pgw_control_telemetry_metric_count = "
                     "sizeof(pgw_control_telemetry_metrics) / "
                     "sizeof(pgw_control_telemetry_metrics[0]);")
        lines.append("const unsigned pgw_control_telemetry_minimum_period_ms = "
                     f"{telemetry_minimum_period_ms}u;")
    for symbol in sorted({b["symbol"] for b in bindings.values()}):
        lines.append(f"extern const PGW_DDSBinding {symbol};")
    for name, participant, entries in connections:
        lines.append(f"static PGW_DDSEndpointConfig {name}_endpoints[] = {{")
        for entry in entries:
            quote = json.dumps
            lines.append("    {%s, %s, &%s, %s, %s, %s}," % (
                quote(entry["name"]), quote(entry["endpoint"]), entry["symbol"],
                entry["capacity"], "true" if entry["role"] == "reader" else "false",
                "true" if entry.get("preserve-source-timestamp") in ("true", "1") else "false"))
        lines.extend([
            "};",
            f"const PGW_DDSConfig pgw_config_{name} = {{",
            f"    .participant_name = {json.dumps(participant)},",
            "    .endpoints = REDA_DEFINE_SEQUENCE_INITIALIZER_W_LOAN("
            f"{name}_endpoints, {len(entries)}, {len(entries)}, "
            "PGW_DDSEndpointConfigElement),",
            "    .endpoints_initialized = true,",
            "};",
        ])
    lines.append("static PGW_CompiledRoute pgw_routes[] = {")
    for i, route in enumerate(routes):
        input_connection, input_stream = route["input"].split("::")
        output_connection, output_stream = route["output"].split("::")
        lines.append("    {%du, %s, %s, %s, %s, %s}," % (
            i + 1, *(json.dumps(v) for v in (route["id"], input_connection,
                      input_stream, output_connection, output_stream))))
    if not routes:
        lines.append("    {0, NULL, NULL, NULL, NULL, NULL}")
    lines.append("};")
    if routes:
        lines.append("const PGW_CompiledRouteSeq pgw_config_routes = "
                     "REDA_DEFINE_SEQUENCE_INITIALIZER_W_LOAN(pgw_routes, "
                     f"{len(routes)}, {len(routes)}, PGW_CompiledRouteElement);")
    else:
        lines.append("const PGW_CompiledRouteSeq pgw_config_routes = "
                     "REDA_DEFINE_SEQUENCE_INITIALIZER(PGW_CompiledRouteElement);")
    lines.append("static PGW_CompiledNativeStream pgw_native_streams[] = {")
    for stream in natives:
        lines.append("    {%s, %s, %s, %s, %su, %s}," % (
            *(json.dumps(stream[k]) for k in ("connection", "name", "endpoint", "binding")),
            stream["capacity"], "true" if stream["role"] == "reader" else "false"))
    if not natives:
        lines.append("    {NULL, NULL, NULL, NULL, 0, false}")
    lines.append("};")
    if natives:
        lines.append("const PGW_CompiledNativeStreamSeq pgw_config_native_streams = "
                     "REDA_DEFINE_SEQUENCE_INITIALIZER_W_LOAN(pgw_native_streams, "
                     f"{len(natives)}, {len(natives)}, PGW_CompiledNativeStreamElement);")
    else:
        lines.append("const PGW_CompiledNativeStreamSeq pgw_config_native_streams = "
                     "REDA_DEFINE_SEQUENCE_INITIALIZER(PGW_CompiledNativeStreamElement);")
    Path(output).write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gateway", required=True)
    parser.add_argument("--dds")
    parser.add_argument("--inventory")
    parser.add_argument("--output", required=True)
    parser.add_argument("--remote-control", action="store_true")
    parser.add_argument("--adapter-manifest", action="append", default=[])
    args = parser.parse_args()
    try:
        emit(compile_config(args.gateway, args.dds, args.inventory,
                            args.remote_control, args.adapter_manifest), args.output)
    except (ConfigError, OSError, KeyError, json.JSONDecodeError) as exc:
        parser.exit(1, f"gateway configuration rejected: {exc}\n")


if __name__ == "__main__":
    main()

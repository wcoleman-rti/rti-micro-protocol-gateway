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


def compile_config(gateway, dds=None, inventory=None):
    schema_path = Path(__file__).resolve().parents[2] / "resources" / "gateway.xsd"
    schema = etree.XMLSchema(etree.parse(str(schema_path)))
    xml_parser = etree.XMLParser(resolve_entities=False, no_network=True, load_dtd=False)
    try:
        document = etree.parse(str(gateway), xml_parser)
        schema.assertValid(document)
    except (etree.XMLSyntaxError, etree.DocumentInvalid) as exc:
        raise ConfigError(f"gateway XSD: {exc}") from exc
    root = parse(gateway)
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
    phase = 0
    native_limits = {}
    for node in root:
        expected_phase = {"binding": 0, "connection": 1,
                          "native-connection": 1, "route": 2}.get(node.tag)
        if expected_phase is None or expected_phase < phase:
            raise ConfigError(f"unknown/out-of-order element {node.tag}")
        phase = expected_phase
        name = identifier(node.get("id", ""))
        if name in ids:
            raise ConfigError(f"duplicate id {name}")
        ids.add(name)
        if node.tag == "binding":
            shape(node, ("id", "symbol", "type", "schema", "fingerprint"))
            if len(node):
                raise ConfigError("binding children forbidden")
            identifier(node.get("symbol"))
            if not re.fullmatch(r"[0-9a-f]{64}", node.get("fingerprint")):
                raise ConfigError("fingerprint must be SHA256")
            bindings[name] = dict(node.attrib)
        elif node.tag == "connection":
            shape(node, ("id", "participant"))
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
            if not entries:
                raise ConfigError("empty connection")
            connections.append((name, node.get("participant"), entries))
        elif node.tag == "native-connection":
            shape(node, ("id", "adapter", "receive-budget", "write-capacity"))
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
    if not bindings or not connections:
        raise ConfigError("bindings and connections required")
    if inventory:
        manifest = json.loads(Path(inventory).read_text())
        for binding in bindings.values():
            if binding["schema"] == manifest["schema"] and binding["fingerprint"] != manifest["fingerprint"]:
                raise ConfigError("stale schema fingerprint")
    return bindings, connections, routes, {
        "route_budget": int(root.get("route-budget")),
        "sample_budget": int(root.get("sample-budget")),
        "diagnostic_period_steps": int(period), **native_limits}, natives


def emit(config, output):
    bindings, connections, routes, settings, natives = config
    lines = ['#include "pgw/dds/connext_micro.h"', '#include "pgw/compiled_config.h"']
    for name, value in settings.items():
        lines.append(f"const unsigned pgw_config_{name} = {value}u;")
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
    args = parser.parse_args()
    try:
        emit(compile_config(args.gateway, args.dds, args.inventory), args.output)
    except (ConfigError, OSError, KeyError, json.JSONDecodeError) as exc:
        parser.exit(1, f"gateway configuration rejected: {exc}\n")


if __name__ == "__main__":
    main()

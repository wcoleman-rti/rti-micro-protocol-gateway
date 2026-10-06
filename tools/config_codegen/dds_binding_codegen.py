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

"""Generate PGW's typed Connext Micro endpoint bridge from RTI type XML."""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
TYPE_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*\Z")
FINGERPRINT = re.compile(r"[0-9a-f]{64}\Z")
INCLUDE_PATH = re.compile(r"[A-Za-z0-9_./-]+\Z")
REQUIRED_ATTRIBUTES = (
    "id", "symbol", "type", "schema", "schema-version", "fingerprint",
    "representation-name", "native-type", "native-header", "support-header",
    "conversion",
)
FIELDWISE_TYPES = {
    "int32": ("int32_t", "DDS_Long"),
    "uint32": ("uint32_t", "DDS_UnsignedLong"),
    "int64": ("int64_t", "DDS_LongLong"),
    "uint64": ("uint64_t", "DDS_UnsignedLongLong"),
    "float32": ("float", "float"),
    "float64": ("double", "double"),
}


class BindingGenerationError(ValueError):
    pass


def _qualified_type(node, modules):
    name = node.get("name")
    if not name or not IDENTIFIER.fullmatch(name):
        return None
    return "::".join((*modules, name))


def load_struct_types(path):
    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as exc:
        raise BindingGenerationError(f"cannot parse RTI type XML {path}: {exc}") from exc
    types = root.find("types")
    if types is None:
        raise BindingGenerationError("RTI XML must contain a top-level <types> element")
    found = {}
    c_names = {}

    def visit(parent, modules):
        for node in parent:
            if node.tag == "module":
                module_name = node.get("name")
                if not module_name or not IDENTIFIER.fullmatch(module_name):
                    raise BindingGenerationError("RTI XML contains an invalid module name")
                visit(node, (*modules, module_name))
            elif node.tag == "struct":
                qualified = _qualified_type(node, modules)
                if qualified:
                    c_name = "_".join((*modules, node.get("name")))
                    if qualified in found or c_name in c_names:
                        raise BindingGenerationError(
                            f"duplicate or colliding RTI type {qualified!r}")
                    found[qualified] = {
                        "c_name": c_name,
                        "members": [
                            {
                                "name": member.get("name"),
                                "type": member.get("type"),
                                "array_dimensions": member.get("arrayDimensions"),
                                "key": member.get("key") in ("true", "1"),
                            }
                            for member in node.findall("member")
                        ],
                    }
                    c_names[c_name] = qualified
            elif node.tag in ("enum", "union", "typedef", "bitset", "bitmask"):
                continue
            else:
                raise BindingGenerationError(
                    f"unsupported RTI XML type declaration {node.tag!r}")

    visit(types, ())
    return found


def _include(path, label):
    if not path or not INCLUDE_PATH.fullmatch(path) or ".." in Path(path).parts:
        raise BindingGenerationError(f"invalid {label} include path {path!r}")
    return path


def _identifier(value, label):
    if not value or not IDENTIFIER.fullmatch(value):
        raise BindingGenerationError(f"invalid {label} identifier {value!r}")
    return value


def load_bindings(path, struct_types):
    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as exc:
        raise BindingGenerationError(f"cannot parse gateway XML {path}: {exc}") from exc
    if root.tag != "gateway":
        raise BindingGenerationError("gateway XML root must be <gateway>")

    bindings = []
    ids = set()
    symbols = set()
    for node in root:
        if node.tag != "binding":
            continue
        missing = [name for name in REQUIRED_ATTRIBUTES if not node.get(name)]
        if missing:
            raise BindingGenerationError(
                f"binding {node.get('id')!r} lacks generation attributes: "
                + ", ".join(missing))
        if len(node):
            raise BindingGenerationError("binding declarations must be leaves")
        item = dict(node.attrib)
        item["id"] = _identifier(item["id"], "binding id")
        item["symbol"] = _identifier(item["symbol"], "binding symbol")
        item["native-type"] = _identifier(item["native-type"], "native type")
        if item["conversion"] not in ("callbacks", "fieldwise"):
            raise BindingGenerationError(
                f"unsupported conversion mode {item['conversion']!r}")
        if item["conversion"] == "callbacks":
            if not item.get("dds-to-native") or not item.get("native-to-dds"):
                raise BindingGenerationError(
                    f"binding {item['id']!r} callback conversion requires both converter names")
            item["dds-to-native"] = _identifier(
                item["dds-to-native"], "DDS-to-native callback")
            item["native-to-dds"] = _identifier(
                item["native-to-dds"], "native-to-DDS callback")
        elif item.get("dds-to-native") or item.get("native-to-dds"):
            raise BindingGenerationError(
                f"binding {item['id']!r} fieldwise conversion cannot specify callbacks")
        if item.get("sample-copy"):
            item["sample-copy"] = _identifier(item["sample-copy"], "native sample copy callback")
        item["type"] = item["type"].strip()
        if not TYPE_NAME.fullmatch(item["type"]):
            raise BindingGenerationError(f"invalid DDS type name {item['type']!r}")
        if item["type"] not in struct_types:
            raise BindingGenerationError(
                f"binding {item['id']!r} names a missing/non-struct DDS type "
                f"{item['type']!r}")
        if not item["schema"] or not IDENTIFIER.fullmatch(item["schema"].replace(".", "_")):
            raise BindingGenerationError(f"invalid schema name {item['schema']!r}")
        if not item["representation-name"] or not re.fullmatch(
                r"[A-Za-z_][A-Za-z0-9_.-]*", item["representation-name"]):
            raise BindingGenerationError(
                f"invalid representation name {item['representation-name']!r}")
        if not item["schema-version"].isdigit() or int(item["schema-version"]) < 1:
            raise BindingGenerationError(
                f"invalid schema version {item['schema-version']!r}")
        if not FINGERPRINT.fullmatch(item["fingerprint"]):
            raise BindingGenerationError(
                f"invalid schema fingerprint for binding {item['id']!r}")
        item["native-header"] = _include(item["native-header"], "native header")
        item["support-header"] = _include(item["support-header"], "type support header")
        item["c-type"] = struct_types[item["type"]]["c_name"]
        item["members"] = struct_types[item["type"]]["members"]
        if item["conversion"] == "fieldwise":
            if not item["members"]:
                raise BindingGenerationError(
                    f"binding {item['id']!r} fieldwise conversion needs scalar members")
            for member in item["members"]:
                if not member["name"] or not IDENTIFIER.fullmatch(member["name"]) or \
                        member["type"] not in FIELDWISE_TYPES or member["array_dimensions"]:
                    raise BindingGenerationError(
                        f"binding {item['id']!r} has unsupported fieldwise member "
                        f"{member['name']!r} ({member['type']!r})")
            item["dds-to-native-function"] = f"pgw_binding_{item['id']}_from_dds"
            item["native-to-dds-function"] = f"pgw_binding_{item['id']}_to_dds"
        else:
            item["dds-to-native-function"] = item["dds-to-native"]
            item["native-to-dds-function"] = item["native-to-dds"]
        if item["id"] in ids or item["symbol"] in symbols:
            raise BindingGenerationError(
                f"duplicate binding id or symbol {item['id']!r}/{item['symbol']!r}")
        ids.add(item["id"])
        symbols.add(item["symbol"])
        if item.get("register-keys"):
            item["register-keys"] = _identifier(
                item["register-keys"], "key registration callback")
        if item.get("register-keys") and item.get("register-key-value"):
            raise BindingGenerationError(
                f"binding {item['id']!r} has conflicting key registration policies")
        if item.get("register-key-value") is not None:
            key_members = [member for member in struct_types[item["type"]]["members"]
                           if member["key"]]
            if len(key_members) != 1:
                raise BindingGenerationError(
                    f"binding {item['id']!r} automatic key registration requires one scalar key")
            key_member = key_members[0]
            key_type = key_member["type"]
            if key_type not in ("int32", "uint32", "int64", "uint64"):
                raise BindingGenerationError(
                    f"binding {item['id']!r} automatic key type {key_type!r} is unsupported")
            try:
                key_value = int(item["register-key-value"], 0)
            except ValueError:
                try:
                    key_value = int(item["register-key-value"], 10)
                except ValueError as exc:
                    raise BindingGenerationError(
                        f"binding {item['id']!r} has invalid register-key-value") from exc
            bounds = {
                "int32": (-(1 << 31), (1 << 31) - 1),
                "uint32": (0, (1 << 32) - 1),
                "int64": (-(1 << 63), (1 << 63) - 1),
                "uint64": (0, (1 << 64) - 1),
            }[key_type]
            if not bounds[0] <= key_value <= bounds[1]:
                raise BindingGenerationError(
                    f"binding {item['id']!r} key value is outside {key_type} range")
            suffix = "u" if key_type == "uint32" else \
                "ull" if key_type == "uint64" else "ll" if key_type == "int64" else ""
            literal = str(key_value)
            if key_value < 0 and suffix in ("ll", "ull"):
                literal = f"({key_value}{suffix})"
            else:
                literal += suffix
            item["auto-key-field"] = key_member["name"]
            item["auto-key-literal"] = literal
        if item.get("supports-timestamp", "false") not in ("true", "false", "1", "0"):
            raise BindingGenerationError(
                f"invalid supports-timestamp value for binding {item['id']!r}")
        bindings.append(item)
    if not bindings:
        raise BindingGenerationError("gateway XML contains no DDS bindings")

    type_contracts = {}
    for item in bindings:
        contract = (item["native-type"], item["native-header"], item["support-header"],
                    item["conversion"], tuple(
                        (member["name"], member["type"])
                        for member in item["members"]))
        prior = type_contracts.setdefault(item["type"], contract)
        if prior != contract:
            raise BindingGenerationError(
                f"DDS type {item['type']!r} has conflicting generated binding contracts")
    return sorted(bindings, key=lambda item: item["id"])


def _callback_declarations(bindings):
    declarations = set()
    for item in bindings:
        if item["conversion"] == "callbacks":
            declarations.add(
                f"extern PGW_Status {item['dds-to-native']}(const void *, void *, size_t);")
            declarations.add(
                f"extern DDS_ReturnCode_t {item['native-to-dds']}(const void *, void *);")
        if item.get("register-keys"):
            declarations.add(
                f"extern PGW_Status {item['register-keys']}(void *, DDS_DataWriter *);")
        if item.get("sample-copy"):
            declarations.add(
                f"extern PGW_Status {item['sample-copy']}(const PGW_Sample *, void *, size_t);")
    return "\n".join(sorted(declarations))


def generate(types_xml, gateway_xml, output, header=None):
    struct_types = load_struct_types(types_xml)
    bindings = load_bindings(gateway_xml, struct_types)

    fieldwise_types = {}
    for item in bindings:
        if item["conversion"] != "fieldwise":
            continue
        members = tuple((member["name"], member["type"]) for member in item["members"])
        prior = fieldwise_types.setdefault(item["native-type"], members)
        if prior != members:
            raise BindingGenerationError(
                f"native type {item['native-type']!r} has conflicting generated layouts")

    support_headers = sorted({item["support-header"] for item in bindings})
    native_headers = sorted({item["native-header"] for item in bindings})
    lines = [
        "/* Generated by dds_binding_codegen.py; do not edit. */",
        '#include "pgw/dds/connext_micro.h"',
        '#include "dds_type_bindings.h"',
        "#include <limits.h>",
        "#include <stdint.h>",
        "#include <string.h>",
        *[f'#include "{header}"' for header in support_headers],
        *[f"#include <{header}>" for header in native_headers],
        "",
        _callback_declarations(bindings),
        "",
    ]

    for item in bindings:
        if item["conversion"] != "fieldwise":
            continue
        c_type = item["c-type"]
        native_type = item["native-type"]
        from_fn = item["dds-to-native-function"]
        to_fn = item["native-to-dds-function"]
        for member in item["members"]:
            c_native, c_dds = FIELDWISE_TYPES[member["type"]]
            lines.extend([
                f"_Static_assert(_Generic((({native_type} *)0)->{member['name']},",
                f"    {c_native}: 1, default: 0),",
                f'    "fieldwise native member type mismatch: {native_type}.{member["name"]}");',
                f"_Static_assert(_Generic((({c_type} *)0)->{member['name']},",
                f"    {c_dds}: 1, default: 0),",
                f'    "fieldwise DDS member type mismatch: {c_type}.{member["name"]}");',
            ])
        lines.extend([
            f"static PGW_Status {from_fn}(const void *opaque, void *out, size_t size)",
            "{",
            f"    if (!opaque || !out || size != sizeof({native_type})) return PGW_INVALID;",
            f"    const {c_type} *wire = opaque;",
            f"    {native_type} *value = out;",
        ])
        lines.extend(
            f"    value->{member['name']} = wire->{member['name']};"
            for member in item["members"])
        lines.extend([
            "    return PGW_OK;",
            "}",
            f"static DDS_ReturnCode_t {to_fn}(const void *opaque, void *out)",
            "{",
            "    if (!opaque || !out) return DDS_RETCODE_BAD_PARAMETER;",
            f"    const {native_type} *value = opaque;",
            f"    {c_type} *wire = out;",
        ])
        lines.extend(
            f"    wire->{member['name']} = value->{member['name']};"
            for member in item["members"])
        lines.extend([
            "    return DDS_RETCODE_OK;",
            "}",
            "",
        ])

    type_bindings = {}
    for item in bindings:
        type_bindings.setdefault(item["type"], item)
    for type_name, item in sorted(type_bindings.items()):
        c_type = item["c-type"]
        state = f"PGW_DDSBindingState_{c_type}"
        lines.extend([
            f"typedef struct {state} {{",
            f"    struct {c_type}Seq data;",
            "    struct DDS_SampleInfoSeq info;",
            f"    {c_type} scratch;",
            f"}} {state};",
            "",
            f"static PGW_Status pgw_{c_type}_initialize(PGW_Arena *arena,",
            "    size_t capacity, void **out)",
            "{",
            f"    {state} *state;",
            "    PGW_Status status;",
            "    if (!arena || !out || !capacity || capacity > INT_MAX)",
            "        return PGW_INVALID;",
            f"    status = PGW_Arena_allocate(arena, sizeof(*state), _Alignof({state}),",
            "        (void **)&state);",
            "    if (status != PGW_OK) return status;",
            "    memset(state, 0, sizeof(*state));",
            f"    if (!{c_type}Seq_initialize(&state->data) ||",
            "        !DDS_SampleInfoSeq_initialize(&state->info) ||",
            f"        !{c_type}_initialize(&state->scratch)) return PGW_FATAL;",
            "    *out = state;",
            "    return PGW_OK;",
            "}",
            f"static DDS_ReturnCode_t pgw_{c_type}_take(void *opaque,",
            "    DDS_DataReader *reader, size_t count)",
            "{",
            f"    {state} *state = opaque;",
            "    if (!state || !reader || count > INT_MAX)",
            "        return DDS_RETCODE_BAD_PARAMETER;",
            f"    return {c_type}DataReader_take({c_type}DataReader_narrow(reader),",
            "        &state->data, &state->info, (DDS_Long)count, DDS_ANY_SAMPLE_STATE,",
            "        DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);",
            "}",
            f"static size_t pgw_{c_type}_length(void *opaque)",
            "{",
            f"    return (size_t){c_type}Seq_get_length(&(({state} *)opaque)->data);",
            "}",
            f"static const void *pgw_{c_type}_data(void *opaque, size_t index)",
            "{",
            "    if (index > INT_MAX) return NULL;",
            f"    return {c_type}Seq_get_reference(&(({state} *)opaque)->data,",
            "        (DDS_Long)index);",
            "}",
            f"static const struct DDS_SampleInfo *pgw_{c_type}_info(",
            "    void *opaque, size_t index)",
            "{",
            "    if (index > INT_MAX) return NULL;",
            "    return DDS_SampleInfoSeq_get_reference(",
            f"        &(({state} *)opaque)->info, (DDS_Long)index);",
            "}",
            f"static DDS_ReturnCode_t pgw_{c_type}_return_loan(void *opaque,",
            "    DDS_DataReader *reader)",
            "{",
            f"    {state} *state = opaque;",
            "    if (!state || !reader) return DDS_RETCODE_BAD_PARAMETER;",
            f"    return {c_type}DataReader_return_loan(",
            f"        {c_type}DataReader_narrow(reader), &state->data, &state->info);",
            "}",
            "",
        ])

    representation_ids = {}
    for item in bindings:
        key = (
            item["native-type"], item["native-header"], item["representation-name"],
            item["schema"], item["schema-version"], item["fingerprint"],
            item.get("sample-copy"),
        )
        representation_ids.setdefault(key, f"pgw_representation_{len(representation_ids)}")
        item["representation-var"] = representation_ids[key]
    for key, rep_name in representation_ids.items():
        (native_type, _, representation_name, schema_name, schema_version,
         fingerprint, sample_copy) = key
        schema_var = rep_name.replace("representation", "schema")
        access_var = rep_name.replace("representation", "access")
        access_reference = "NULL"
        if sample_copy:
            copy_wrapper = rep_name.replace("representation", "copy_sample")
            lines.extend([
                f"static PGW_Status {copy_wrapper}(const PGW_Sample *sample,",
                "    void *out, size_t size)",
                "{",
                f"    return {sample_copy}(sample, out, size);",
                "}",
                f"static const PGW_SampleAccessI {access_var} = {{",
                f"    PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), {copy_wrapper}, NULL",
                "};",
                "",
            ])
            access_reference = f"&{access_var}"
        lines.extend([
            f"static const PGW_Schema {schema_var} = {{",
            f"    {json.dumps(schema_name)}, {schema_version}u, {json.dumps(fingerprint)}",
            "};",
            f"static const PGW_Representation {rep_name} = {{",
            f"    &{schema_var}, {json.dumps(representation_name)},",
            f"    sizeof({native_type}), _Alignof({native_type}), {access_reference}",
            "};",
            "",
        ])

    for item in bindings:
        c_type = item["c-type"]
        state = f"PGW_DDSBindingState_{c_type}"
        suffix = item["id"]
        copy_fn = f"pgw_binding_{suffix}_copy_native"
        write_fn = f"pgw_binding_{suffix}_write"
        lines.extend([
            f"static PGW_Status {copy_fn}(const void *opaque, void *out, size_t size)",
            "{",
            f"    if (!opaque || !out || size != sizeof({item['native-type']}))",
            "        return PGW_INVALID;",
            f"    return {item['dds-to-native-function']}(opaque, out, size);",
            "}",
            f"static DDS_ReturnCode_t {write_fn}(void *opaque,",
            "    DDS_DataWriter *writer, const void *native,",
            "    const struct DDS_Time_t *time)",
            "{",
            f"    {state} *state = opaque;",
            "    DDS_ReturnCode_t status;",
            "    if (!state || !writer || !native)",
            "        return DDS_RETCODE_BAD_PARAMETER;",
            f"    status = {item['native-to-dds-function']}(native, &state->scratch);",
            "    if (status != DDS_RETCODE_OK) return status;",
        ])
        if item.get("supports-timestamp") in ("true", "1"):
            lines.extend([
                "    if (time)",
                f"        return {c_type}DataWriter_write_w_timestamp(",
                f"            {c_type}DataWriter_narrow(writer), &state->scratch,",
                "            &DDS_HANDLE_NIL, time);",
            ])
        else:
            lines.extend([
                "    if (time) return DDS_RETCODE_UNSUPPORTED;",
            ])
        lines.extend([
            f"    return {c_type}DataWriter_write(",
            f"        {c_type}DataWriter_narrow(writer), &state->scratch,",
            "        &DDS_HANDLE_NIL);",
            "}",
        ])
        if item.get("register-keys") or item.get("register-key-value") is not None:
            lines.extend([
                f"static PGW_Status pgw_binding_{suffix}_register_keys(",
                "    void *opaque, DDS_DataWriter *writer)",
                "{",
                f"    {state} *state = opaque;",
                "    if (!state || !writer) return PGW_INVALID;",
            ])
            if item.get("register-keys"):
                lines.extend([
                    f"    return {item['register-keys']}(&state->scratch, writer);",
                    "}",
                ])
            else:
                key_c_type = item["c-type"]
                lines.extend([
                    f"    DDS_InstanceHandle_t handle;",
                    f"    state->scratch.{item['auto-key-field']} = "
                    f"{item['auto-key-literal']};",
                ])
                if item.get("supports-timestamp") in ("true", "1"):
                    lines.extend([
                        "    struct DDS_Time_t epoch = {0, 0};",
                        f"    handle = {key_c_type}DataWriter_register_instance_w_timestamp(",
                        f"        {key_c_type}DataWriter_narrow(writer), &state->scratch, &epoch);",
                    ])
                else:
                    lines.extend([
                        f"    handle = {key_c_type}DataWriter_register_instance(",
                        f"        {key_c_type}DataWriter_narrow(writer), &state->scratch);",
                    ])
                lines.extend([
                    "    return DDS_InstanceHandle_equals(&handle, &DDS_HANDLE_NIL)",
                    "        ? PGW_FATAL : PGW_OK;",
                    "}",
                ])
        lines.extend([
            f"const PGW_DDSBinding {item['symbol']} = {{",
            f"    &{item['representation-var']},",
            f"    {json.dumps(item['type'].split('::')[-1])},",
            f"    sizeof({item['native-type']}), pgw_{c_type}_initialize,",
            f"    pgw_{c_type}_take, pgw_{c_type}_length, pgw_{c_type}_data,",
            f"    pgw_{c_type}_info, pgw_{c_type}_return_loan, {copy_fn}, {write_fn},",
            f"    {'pgw_binding_' + suffix + '_register_keys' if item.get('register-keys') or item.get('register-key-value') is not None else 'NULL'}",
            "};",
            "",
        ])

    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    header_destination = Path(header) if header else destination.with_name(
        "dds_type_bindings.h")
    header_lines = [
        "/* Generated by dds_binding_codegen.py; do not edit. */",
        "#ifndef PGW_DDS_TYPE_BINDINGS_H",
        "#define PGW_DDS_TYPE_BINDINGS_H",
        "#include <stdbool.h>",
        "#include <stdint.h>",
        "",
    ]
    for native_type, members in sorted(fieldwise_types.items()):
        header_lines.append(f"typedef struct {native_type} {{")
        for member_name, member_type in members:
            c_native, _ = FIELDWISE_TYPES[member_type]
            header_lines.append(f"    {c_native} {member_name};")
        header_lines.extend([f"}} {native_type};", ""])
    header_lines.append("#endif")
    header_destination.parent.mkdir(parents=True, exist_ok=True)
    header_destination.write_text("\n".join(header_lines) + "\n",
                                  encoding="utf-8", newline="\n")
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--types-xml", required=True)
    parser.add_argument("--gateway", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--header")
    args = parser.parse_args()
    try:
        generate(args.types_xml, args.gateway, args.output, args.header)
    except (BindingGenerationError, OSError) as exc:
        parser.exit(1, f"DDS binding generation failed: {exc}\n")


if __name__ == "__main__":
    main()

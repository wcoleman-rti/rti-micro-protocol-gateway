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

"""Strict, deterministic DBC-to-native-signal generator."""
import argparse
from decimal import Decimal
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import re
import sys

import cantools
from cantools.database.can.formats.dbc import Parser

VERSION = "1"
PARSER_VERSION = "40.7.1"
I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1


def fail(context, reason):
    raise ValueError(f"{context}: {reason}")


def c_string(value):
    return '"' + "".join(f"\\{byte:03o}" for byte in (value or "").encode("utf-8")) + '"'


def integer(value):
    if value == I64_MIN:
        return "INT64_MIN"
    return f"INT64_C({value})" if value >= 0 else f"(-INT64_C({-value}))"


def numeric(value):
    return repr(float(value))


def fraction(value):
    decimal = Decimal(str(value))
    if not decimal.is_finite():
        raise ValueError("non-finite numeric constant")
    return Fraction(decimal)


def generate(dbc_path, mapping_path, output):
    if cantools.__version__ != PARSER_VERSION:
        fail("cantools", f"requires {PARSER_VERSION}, got {cantools.__version__}")
    dbc_text = dbc_path.read_text(encoding="utf-8")
    # Operational extensions outside this intentionally bounded subset.
    for keyword in ("SIG_VALTYPE_", "SG_MUL_VAL_", "SIG_TYPE_REF_", "SIG_GROUP_"):
        match = re.search(r"^\s*" + keyword + r"\s+\d[^\n]*", dbc_text, re.MULTILINE)
        if match:
            fail("DBC", f"unsupported operational construct: {match.group().strip()[:160]}")
    if re.search(r"^\s*BO_\s+\d+\s+VECTOR__INDEPENDENT_SIG_MSG\s*:", dbc_text, re.MULTILINE):
        fail("DBC", "unassigned independent signals unsupported")
    db = cantools.database.load_string(dbc_text, database_format="dbc", strict=True)
    # The pinned parser's tokens retain decimal text before conversion to float.
    numeric_tokens = {(m[2], s[1][0]): (s[10], s[12], s[15], s[17])
                      for m in Parser().parse(dbc_text).get("BO_", []) for s in m[6]}
    mapping = json.loads(mapping_path.read_text(encoding="utf-8"))
    if not isinstance(mapping, dict):
        fail("mapping", "root must be an object")
    if set(mapping) != {"schema", "version", "signals"}:
        fail("mapping", "expected schema, version, signals only")
    if not isinstance(mapping["signals"], list):
        fail("mapping", "signals must be an array")
    if not isinstance(mapping["schema"], str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.]*", mapping["schema"]):
        fail("mapping", "invalid schema name")
    if type(mapping["version"]) is not int or not 1 <= mapping["version"] <= 0xffffffff:
        fail("mapping", "version must be positive uint32")
    entries, ids = {}, set()
    for entry in mapping["signals"]:
        if not isinstance(entry, dict):
            fail("mapping", "each signal mapping must be an object")
        if set(entry) - {"message", "signal", "id", "category", "kind"} or not {"message", "signal", "id", "category"} <= set(entry):
            fail("mapping", "invalid signal fields")
        key = (entry["message"], entry["signal"])
        if not all(isinstance(v, str) and v for v in key):
            fail("mapping", "message and signal names must be nonempty strings")
        if key in entries or type(entry["id"]) is not int or not 0 < entry["id"] <= 0xffffffff or entry["id"] in ids:
            fail(str(key), "duplicate/invalid explicit signal ID")
        if not isinstance(entry["category"], str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", entry["category"]):
            fail(str(key), "invalid category identifier")
        entries[key] = entry
        ids.add(entry["id"])
    messages, signals, used, frame_keys, message_names = [], [], set(), set(), set()
    for message in sorted(db.messages, key=lambda m: (m.frame_id, m.is_extended_frame, m.name)):
        ctx = message.name
        if message.name in message_names:
            fail(ctx, "duplicate message name")
        message_names.add(message.name)
        if message.protocol or message.is_container:
            fail(ctx, "higher-level protocols/containers unsupported")
        frame_format_attribute = message.dbc.attributes.get("VFrameFormat") if message.dbc else None
        if frame_format_attribute:
            choices = frame_format_attribute.definition.choices
            frame_format = choices[frame_format_attribute.value]
            if frame_format not in ("StandardCAN", "ExtendedCAN", "StandardCAN_FD", "ExtendedCAN_FD"):
                fail(ctx, f"unsupported frame format {frame_format}")
            if frame_format.startswith("Extended") != message.is_extended_frame:
                fail(ctx, "VFrameFormat conflicts with identifier format")
        allowed_lengths = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64) if message.is_fd else range(9)
        if message.length not in allowed_lengths or message.length == 0:
            fail(ctx, "invalid Classic/FD payload length")
        if not 0 <= message.frame_id <= (0x1fffffff if message.is_extended_frame else 0x7ff):
            fail(ctx, "invalid frame identifier")
        frame_key = (message.frame_id, message.is_extended_frame)
        if frame_key in frame_keys:
            fail(ctx, "duplicate frame identifier")
        frame_keys.add(frame_key)
        selectors = [s for s in message.signals if s.is_multiplexer]
        if len(selectors) > 1:
            fail(ctx, "extended/nested multiplexing unsupported")
        names = {s.name: s for s in message.signals}
        if len(names) != len(message.signals):
            fail(ctx, "duplicate signal name")
        local = []
        for signal in sorted(message.signals, key=lambda s: s.name):
            context = f"{ctx}.{signal.name}"
            key = (ctx, signal.name)
            if key not in entries:
                fail(context, "missing explicit ID/category")
            entry = entries[key]
            used.add(key)
            if signal.is_float:
                fail(context, "floating-point DBC signals unsupported")
            if not 1 <= signal.length <= (64 if signal.is_signed else 63):
                fail(context, "raw integer cannot fit int64")
            lo = -(1 << (signal.length - 1)) if signal.is_signed else 0
            hi = (1 << (signal.length - (1 if signal.is_signed else 0))) - 1
            try:
                if key not in numeric_tokens:
                    fail(context, "long-symbol renaming is unsupported")
                scale_text, offset_text, minimum_text, maximum_text = numeric_tokens[key]
                scale, offset = fraction(scale_text), fraction(offset_text)
                scale_float, offset_float = float(scale), float(offset)
            except (ValueError, OverflowError) as error:
                fail(context, str(error))
            if not math.isfinite(scale_float) or not math.isfinite(offset_float) or scale_float == 0:
                fail(context, "scale/offset must be finite and scale nonzero")
            physical = sorted((lo * scale + offset, hi * scale + offset))
            kind = entry.get("kind", "int64" if scale.denominator == offset.denominator == 1 else "double")
            if kind not in ("boolean", "int64", "double"):
                fail(context, "unsupported value kind")
            if kind == "boolean":
                if signal.length != 1 or signal.is_signed or scale != 1 or offset != 0:
                    fail(context, "boolean requires unsigned one-bit identity semantics")
                if signal.choices and set(signal.choices) != {0, 1}:
                    fail(context, "boolean choices must be 0/1")
            elif kind == "int64":
                if scale.denominator != 1 or offset.denominator != 1:
                    fail(context, "int64 requires integral engineering values")
                intermediates = [lo * scale, hi * scale, scale, offset, *physical]
                if any(v < I64_MIN or v > I64_MAX for v in intermediates):
                    fail(context, "engineering integer cannot fit int64")
            else:
                if max(abs(lo), abs(hi)) > 2**53 or max(abs(v) for v in physical) > 2**53:
                    fail(context, "double mapping exceeds supported 53-bit precision")
                if scale.denominator != 1 or offset.denominator != 1:
                    resolution = max(math.ulp(float(v)) for v in physical)
                    if resolution / abs(float(scale)) > 0.25:
                        fail(context, "fractional engineering resolution exceeds inverse precision budget")
                # Reject scales whose adjacent raw values collapse in binary64.
                for raw in (lo, hi - 1):
                    if float(raw * scale + offset) == float((raw + 1) * scale + offset):
                        fail(context, "adjacent engineering values lose double precision")
                for raw in (lo, lo + 1, 0, hi - 1, hi):
                    if not lo <= raw <= hi:
                        continue
                    decoded = float(raw) * float(scale) + float(offset)
                    if abs(Fraction.from_float(decoded) - (raw * scale + offset)) > abs(scale) / 4:
                        fail(context, "engineering conversion exceeds quarter-step precision budget")
                    inverse = (decoded - float(offset)) / float(scale)
                    magnitude = abs(inverse)
                    whole = math.floor(magnitude)
                    rounded = whole + (magnitude - whole >= 0.5)
                    if inverse < 0:
                        rounded = -rounded
                    if rounded != raw:
                        fail(context, "engineering endpoint cannot round-trip with double precision")
            minimum = fraction(minimum_text) if signal.minimum is not None else physical[0]
            maximum = fraction(maximum_text) if signal.maximum is not None else physical[1]
            if minimum > maximum or minimum < physical[0] or maximum > physical[1]:
                fail(context, "declared range outside representable engineering bounds")
            inverse_bounds = sorted(((minimum - offset) / scale, (maximum - offset) / scale))
            if math.ceil(inverse_bounds[0]) > math.floor(inverse_bounds[1]):
                fail(context, "declared range contains no representable raw value")
            if signal.is_multiplexer:
                if signal.multiplexer_signal or signal.is_signed or scale != 1 or offset != 0:
                    fail(context, "selector must be unsigned unscaled simple multiplex")
            mux = signal.multiplexer_signal
            mux_value = -1
            if mux:
                if mux not in names or not names[mux].is_multiplexer or not signal.multiplexer_ids or len(signal.multiplexer_ids) != 1:
                    fail(context, "only one simple multiplex branch per signal supported")
                mux_value = signal.multiplexer_ids[0]
                if not 0 <= mux_value < (1 << names[mux].length):
                    fail(context, "multiplex selector value out of range")
            pos, bits = signal.start, []
            for _ in range(signal.length):
                if not 0 <= pos < message.length * 8:
                    fail(context, "signal extends beyond frame")
                bits.append(pos)
                pos = pos + 1 if signal.byte_order == "little_endian" else (pos + 15 if pos % 8 == 0 else pos - 1)
            for other in local:
                if set(bits) & set(other["_bits"]) and not (mux and other["multiplexer"] == mux and mux_value != other["multiplex_value"]):
                    fail(context, "illegal signal overlap")
            choices = [{"raw": int(raw), "label": str(label)} for raw, label in sorted((signal.choices or {}).items())]
            if any(c["raw"] < lo or c["raw"] > hi for c in choices):
                fail(context, "choice raw value out of range")
            local.append(dict(id=entry["id"], name=signal.name, category=entry["category"],
                message_index=len(messages), start_bit=signal.start, bit_length=signal.length,
                little_endian=signal.byte_order == "little_endian", is_signed=signal.is_signed,
                is_multiplexer=signal.is_multiplexer, multiplexer=mux, multiplex_value=mux_value,
                scale=float(scale), offset=float(offset), minimum=float(minimum), maximum=float(maximum),
                scale_exact=str(scale), offset_exact=str(offset),
                minimum_exact=str(minimum), maximum_exact=str(maximum),
                raw_minimum=lo, raw_maximum=hi, kind=kind, unit=signal.unit or "",
                integer_minimum=math.ceil(minimum) if kind == "int64" else 0,
                integer_maximum=math.floor(maximum) if kind == "int64" else 0,
                integer_scale=int(scale) if kind == "int64" else 0,
                integer_offset=int(offset) if kind == "int64" else 0,
                choices=choices, _bits=bits))
        messages.append(dict(frame_id=message.frame_id, name=message.name, length=message.length,
            extended=message.is_extended_frame, fd=message.is_fd, signal_count=len(local)))
        signals.extend(local)
    if used != set(entries):
        fail("mapping", f"unknown signal entries: {sorted(set(entries) - used)}")
    if not signals:
        fail("mapping", "no signals")
    for s in signals:
        s.pop("_bits")
        s["multiplexer_index"] = next((i for i, other in enumerate(signals)
            if other["message_index"] == s["message_index"] and other["name"] == s["multiplexer"]), -1)
    content = dict(schema=mapping["schema"], version=mapping["version"],
        key_type="uint32", value_arms=["boolean", "int64", "double"], messages=messages, signals=signals)
    canonical = json.dumps(content, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    fingerprint = hashlib.sha256(canonical.encode()).hexdigest()
    manifest = dict(content, fingerprint=fingerprint, generator_version=VERSION, cantools_version=PARSER_VERSION)
    output.mkdir(parents=True, exist_ok=True)
    header = """#ifndef PGW_CODEC_H
#define PGW_CODEC_H
#include <pgw/signal.h>
#ifdef __cplusplus
extern "C" {
#endif
extern const PGW_SignalSchema PGW_codec_schema;
extern const PGW_SignalDescriptor PGW_codec_signals[];
extern const size_t PGW_codec_signal_count;
extern const PGW_MessageDescriptor PGW_codec_messages[];
extern const size_t PGW_codec_message_count;
const PGW_SignalDescriptor *PGW_codec_signal_find(uint32_t id);
const PGW_MessageDescriptor *PGW_codec_message_find(uint32_t id, bool extended);
PGW_CodecStatus PGW_codec_decode(uint32_t id, bool extended, bool fd,
    const uint8_t *data, size_t length, PGW_Signal *output, size_t capacity, size_t *count);
PGW_CodecStatus PGW_codec_patch(const PGW_Signal *signal, uint8_t *baseline, size_t length);
#ifdef __cplusplus
}
#endif
#endif
"""
    categories = sorted({s["category"] for s in signals})
    schema_macros = (
        f"#define PGW_CODEC_SCHEMA_NAME {c_string(mapping['schema'])}\n"
        f"#define PGW_CODEC_SCHEMA_VERSION {mapping['version']}u\n"
        f'#define PGW_CODEC_SCHEMA_FINGERPRINT "{fingerprint}"\n'
        "#define PGW_CODEC_CATEGORIES(X) " + " ".join(f"X({name})" for name in categories) + "\n")
    header = header.replace("#include <pgw/signal.h>\n", "#include <pgw/signal.h>\n" + schema_macros)
    tables = [f'const PGW_SignalSchema PGW_codec_schema = {{{c_string(mapping["schema"])}, {mapping["version"]}u, "{fingerprint}"}};']
    for i, s in enumerate(signals):
        if s["choices"]:
            values = ", ".join("{" + integer(c["raw"]) + ", " + c_string(c["label"]) + "}" for c in s["choices"])
            tables.append(f"static const PGW_SignalChoice choices_{i}[] = {{{values}}};")
    tables.append("const PGW_SignalDescriptor PGW_codec_signals[] = {")
    for i, s in enumerate(signals):
        values = [f'{s["id"]}u', str(s["message_index"]), c_string(s["name"]), c_string(s["category"]), c_string(s["unit"]),
            "PGW_VALUE_" + s["kind"].upper(), str(s["start_bit"]), str(s["bit_length"])]
        values.extend("true" if s[k] else "false" for k in ("little_endian", "is_signed", "is_multiplexer"))
        values.extend([integer(s["multiplex_value"]), str(s["multiplexer_index"])])
        values.extend(numeric(s[k]) for k in ("scale", "offset", "minimum", "maximum"))
        values.extend(integer(s[k]) for k in ("raw_minimum", "raw_maximum"))
        values.extend(integer(s[k]) for k in ("integer_minimum", "integer_maximum"))
        values.extend(integer(s[k]) for k in ("integer_scale", "integer_offset"))
        values.extend([f"choices_{i}" if s["choices"] else "NULL", str(len(s["choices"]))])
        tables.append("    {" + ", ".join(values) + "},")
    tables.extend(["};", f"const size_t PGW_codec_signal_count = {len(signals)};", "const PGW_MessageDescriptor PGW_codec_messages[] = {"])
    for m in messages:
        tables.append("    {" + ", ".join([str(m["frame_id"]) + "u", c_string(m["name"]), str(m["length"]),
            "true" if m["extended"] else "false", "true" if m["fd"] else "false", str(m["signal_count"])]) + "},")
    tables.extend(["};", f"const size_t PGW_codec_message_count = {len(messages)};"])
    source = Path(__file__).with_name("codec.c.in").read_text().replace("/*TABLES*/", "\n".join(tables))
    idl = (Path(__file__).resolve().parents[2] / "resources" / "signal.idl").read_text()
    for name, text in (("pgw_codec.h", header), ("pgw_codec.c", source), ("signals.idl", idl),
                       ("manifest.json", json.dumps(manifest, sort_keys=True, indent=2) + "\n")):
        output.joinpath(name).write_text(text, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dbc", required=True, type=Path)
    parser.add_argument("--mapping", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        generate(args.dbc, args.mapping, args.output)
    except (ValueError, OSError, cantools.database.errors.Error) as error:
        print(f"dbc_codegen: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

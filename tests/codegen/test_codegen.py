"""Requirement-oriented generator tests; all scratch paths remain in this tree."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("dbc_codegen", ROOT / "tools/dbc_codegen/generate.py")
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


class CodegenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = ROOT / "tests/codegen" / f".work-{os.getpid()}"
        cls.work.mkdir()
        cls.env = dict(os.environ, TMPDIR=str(cls.work))
        cls.dbc = ROOT / "examples/can_dds/example.dbc"
        cls.mapping = ROOT / "examples/can_dds/mapping.json"
        cls.generated = cls.work / "generated"
        GEN.generate(cls.dbc, cls.mapping, cls.generated)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work)

    def test_reproducibility_and_schema_golden(self):
        second = self.work / "second"
        GEN.generate(self.dbc, self.mapping, second)
        for name in ("pgw_codec.h", "pgw_codec.c", "signals.idl", "manifest.json"):
            self.assertEqual((self.generated / name).read_bytes(), (second / name).read_bytes())
        manifest = json.loads((self.generated / "manifest.json").read_text())
        self.assertEqual(manifest["fingerprint"],
                         "8775a6849abfb9c7b59c71cde2d2b8bbe63fd0b1734b3ae0364fa4c851541df5")
        self.assertEqual([s["id"] for s in manifest["signals"]], [1003, 1001, 1004, 1002, 2004, 2001, 2003, 2002])
        self.assertTrue(manifest["messages"][1]["extended"])
        self.assertTrue(manifest["messages"][1]["fd"])
        mapping = json.loads(self.mapping.read_text())
        mapping["signals"].reverse()
        reordered = self.work / "reordered.json"
        reordered.write_text(json.dumps(mapping))
        GEN.generate(self.dbc, reordered, second)
        self.assertEqual((self.generated / "manifest.json").read_bytes(), (second / "manifest.json").read_bytes())

    def test_pure_c_golden_decode_patch(self):
        executable = self.work / "codec_test"
        subprocess.run(["cc", "-std=c11", "-pedantic", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "bindings/signal/include"), "-I" + str(self.generated),
                        str(ROOT / "tests/codegen/test_codec.c"), str(self.generated / "pgw_codec.c"),
                        "-lm", "-o", str(executable)], check=True, env=self.env)
        subprocess.run([str(executable)], check=True, env=self.env)

    def generate_custom(self, signal="0|8@1+ (1,0) [0|255]", kind=None, extra="", length=8):
        dbc = self.work / "custom.dbc"
        dbc.write_text(f'VERSION ""\nNS_ :\nBS_:\nBU_: ECU\nBO_ 1 Test: {length} ECU\n'
                       f' SG_ Value : {signal} "" ECU\n{extra}')
        entry = dict(message="Test", signal="Value", id=1, category="test")
        if kind:
            entry["kind"] = kind
        mapping = self.work / "custom.json"
        mapping.write_text(json.dumps(dict(schema="pgw.test", version=1, signals=[entry])))
        GEN.generate(dbc, mapping, self.work / "custom")
        return self.work / "custom"

    def test_int64_endpoints_are_exact(self):
        generated = self.generate_custom("0|64@1- (1,0) [-9223372036854775808|9223372036854775807]")
        class Data(ctypes.Union):
            _fields_ = [("boolean", ctypes.c_bool), ("integer", ctypes.c_int64), ("real", ctypes.c_double)]
        class Value(ctypes.Structure):
            _fields_ = [("kind", ctypes.c_int), ("data", Data)]
        class Signal(ctypes.Structure):
            _fields_ = [("id", ctypes.c_uint32), ("value", Value)]
        library = generated / "codec.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
                        "-I" + str(ROOT / "bindings/signal/include"), str(generated / "pgw_codec.c"),
                        "-lm", "-o", str(library)], check=True, env=self.env)
        codec = ctypes.CDLL(str(library))
        codec.PGW_codec_decode.argtypes = [ctypes.c_uint32, ctypes.c_bool, ctypes.c_bool,
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(Signal),
            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        codec.PGW_codec_patch.argtypes = [ctypes.POINTER(Signal), ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
        for integer in (-(1 << 63), (1 << 63) - 1, -(1 << 53) - 1, (1 << 53) + 1):
            frame = (ctypes.c_uint8 * 8).from_buffer_copy(integer.to_bytes(8, "little", signed=True))
            sample, count = Signal(), ctypes.c_size_t()
            self.assertEqual(codec.PGW_codec_decode(1, False, False, frame, 8, ctypes.byref(sample), 1, ctypes.byref(count)), 0)
            self.assertEqual(sample.value.data.integer, integer)
            sample.value.data.integer = -integer if integer != -(1 << 63) else (1 << 63) - 1
            self.assertEqual(codec.PGW_codec_patch(ctypes.byref(sample), frame, 8), 0)
            self.assertEqual(bytes(frame), sample.value.data.integer.to_bytes(8, "little", signed=True))

    def test_unsupported_features_and_numeric_ranges(self):
        cases = [
            ("0|64@1+ (1,0) [0|18446744073709551615]", None, "", 8),
            ("0|8@1+ (0,0) [0|0]", None, "", 8),
            ("0|8@1+ (1e400,0) [0|0]", None, "", 8),
            ("0|8@1+ (1e-400,0) [0|0]", None, "", 8),
            ("0|8@1+ (1,0) [0|256]", None, "", 8),
            ("0|8@1+ (0.5,0) [0.1|0.2]", None, "", 8),
            ("0|8@1+ (1,0) [0|255]", "boolean", "", 8),
            ("0|8@1+ (0.1,0) [0|25.5]", "int64", "", 8),
            ("0|63@1+ (2,0) [0|18446744073709551614]", None, "", 8),
            ("0|63@1+ (0.1,0) [0|922337203685477580.7]", None, "", 8),
            ("0|32@1+ (1,0) [0|4294967295]", None, "SIG_VALTYPE_ 1 Value : 1;\n", 8),
            ("0|8@1+ (1,0) [0|255]", None, "SG_MUL_VAL_ 1 Value Value 0-2;\n", 8),
            ("0|8@1+ (1,0) [0|255]", None, "", 9),
            ("0|8@1+ (1,0) [0|255]", None,
             'BA_DEF_ BO_ "VFrameFormat" ENUM "StandardCAN","ExtendedCAN";\n'
             'BA_ "VFrameFormat" BO_ 1 1;\n', 8),
            ("63|8@1+ (1,0) [0|255]", None, "", 8),
        ]
        for signal, kind, extra, length in cases:
            with self.subTest(signal=signal, kind=kind, extra=extra, length=length):
                with self.assertRaises((ValueError, GEN.cantools.database.errors.Error)):
                    self.generate_custom(signal, kind, extra, length)

    def test_mapping_identity_validation_and_fingerprint(self):
        original = json.loads(self.mapping.read_text())
        for mutation in ("duplicate_id", "missing", "unknown", "category", "version"):
            mapping = json.loads(json.dumps(original))
            if mutation == "duplicate_id":
                mapping["signals"][1]["id"] = mapping["signals"][0]["id"]
            elif mutation == "missing":
                mapping["signals"].pop()
            elif mutation == "unknown":
                mapping["signals"][0]["signal"] = "Unknown"
            elif mutation == "category":
                mapping["signals"][0]["category"] = "bad/category"
            else:
                mapping["version"] = True
            path = self.work / "invalid.json"
            path.write_text(json.dumps(mapping))
            with self.subTest(mutation=mutation):
                with self.assertRaises(ValueError):
                    GEN.generate(self.dbc, path, self.work / "invalid")
        original["signals"][0]["category"] = "new_category"
        path = self.work / "changed.json"
        path.write_text(json.dumps(original))
        GEN.generate(self.dbc, path, self.work / "changed")
        before = json.loads((self.generated / "manifest.json").read_text())
        after = json.loads((self.work / "changed/manifest.json").read_text())
        self.assertNotEqual(before["fingerprint"], after["fingerprint"])

    def test_negative_scaling(self):
        generated = self.generate_custom("0|8@1- (-2,5) [-249|261]")
        manifest = json.loads((generated / "manifest.json").read_text())
        self.assertEqual(manifest["signals"][0]["integer_minimum"], -249)
        self.assertEqual(manifest["signals"][0]["integer_maximum"], 261)

    def test_integral_scaling_does_not_lose_64_bit_precision(self):
        cases = [
            ("0|1@1+ (9007199254740993,0) [0|9007199254740993]",
             "INT64_C(9007199254740993)", "INT64_C(4503599627370497)", 1),
            ("0|1@1+ (-9223372036854775808,0) [-9223372036854775808|0]",
             "INT64_MIN", "(-INT64_C(4611686018427387904))", 1),
            ("0|63@1- (2,0) [-9223372036854775808|9223372036854775806]",
             "INT64_MIN", "(-INT64_C(3))", -2),
        ]
        for definition, endpoint, rounded_input, rounded_raw in cases:
            with self.subTest(definition=definition):
                generated = self.generate_custom(definition)
                source = self.work / "large_scale.c"
                source.write_text(f'''
#include "pgw_codec.h"
#include <assert.h>
#include <limits.h>
int main(void) {{
    uint8_t frame[8] = {{0}};
    PGW_Signal command = {{1, {{PGW_VALUE_INT64, {{.integer = {endpoint}}}}}}};
    PGW_Signal result;
    size_t count;
    assert(PGW_codec_patch(&command, frame, 8) == PGW_CODEC_OK);
    assert(PGW_codec_decode(1, false, false, frame, 8, &result, 1, &count) == PGW_CODEC_OK);
    assert(result.value.data.integer == command.value.data.integer);
    command.value.data.integer = {rounded_input};
    assert(PGW_codec_patch(&command, frame, 8) == PGW_CODEC_OK);
    assert(PGW_codec_decode(1, false, false, frame, 8, &result, 1, &count) == PGW_CODEC_OK);
    assert(result.value.data.integer == INT64_C({rounded_raw}) * PGW_codec_signals[0].integer_scale);
    return 0;
}}
''')
                executable = self.work / "large_scale_test"
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                                "-I" + str(ROOT / "bindings/signal/include"), "-I" + str(generated),
                                str(source), str(generated / "pgw_codec.c"), "-lm", "-o", str(executable)],
                               check=True, env=self.env)
                subprocess.run([str(executable)], check=True, env=self.env)

    def test_exact_decimal_bounds_are_not_truncated(self):
        # A float parser rounds the upper bound down to 255 and would accept it.
        with self.assertRaisesRegex(ValueError, "declared range"):
            self.generate_custom("0|8@1+ (1,0) [0|255.0000000000000000001]")

    def test_mapping_shape_failures(self):
        for malformed in ([], {"schema": "pgw.test", "version": 1, "signals": {}},
                          {"schema": "pgw.test", "version": 1, "signals": [5]}):
            path = self.work / "malformed.json"
            path.write_text(json.dumps(malformed))
            with self.subTest(mapping=malformed), self.assertRaises(ValueError):
                GEN.generate(self.dbc, path, self.work / "malformed")


if __name__ == "__main__":
    unittest.main()

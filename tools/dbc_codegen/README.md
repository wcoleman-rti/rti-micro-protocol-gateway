<!--
  (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.

  RTI grants Licensee a license to use, modify, compile, and create derivative
  works of the Software. Licensee has the right to distribute object form only
  for use with RTI products. The Software is provided "as is", with no warranty
  of any type, including any warranty for fitness for any purpose. RTI is under no
  obligation to maintain or support the Software. RTI shall not be liable for any
  incidental or consequential damages arising out of the use or inability to use
  the software.
-->

# DBC signal generator

Use the pinned build-time parser in the repository-local environment:

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/dbc_codegen/requirements.txt
.venv/bin/python tools/dbc_codegen/generate.py \
  --dbc examples/can_dds/example.dbc \
  --mapping examples/can_dds/mapping.json \
  --output build/generated/signals
.venv/bin/python -m unittest discover -s tests/codegen -v
```

Python/cantools are host tools, not runtime dependencies. Output is deterministic
UTF-8/LF C11 source/header, keyed fixed-union IDL, and a JSON inventory. The schema
SHA-256 covers the normalized complete mapping and descriptors, not paths,
timestamps, comments, or mapping entry order. Parser/generator versions are
recorded separately. IDs are mandatory positive uint32 values, globally unique;
every DBC signal must have an explicit mapping. Retired IDs must not be reused.
Schema/category/ID changes alter the fingerprint. The generator does not migrate
schemas or automatically allocate IDs.

`PGW::binding_signal` is a header-only native representation target. Include
`<pgw/signal.h>` for `PGW_Signal` and tagged `PGW_Value`. Neither contains adapter
metadata. Each adapter owns its private sample wrapper and conversion hooks.
The IDL uses the separate `PGW_DDS` namespace to avoid collisions with native C
types. The union discriminator, never the current numeric value, selects the
arm: explicit semantic boolean, integral int64 engineering values, or fractional
double engineering values. Choice labels remain descriptor metadata.

Include generated `pgw_codec.h` for these interfaces:

* `PGW_codec_schema`: name, version, hexadecimal SHA-256 fingerprint.
* `PGW_codec_messages`, `PGW_codec_signals`, counts and lookup functions:
  immutable frame, ID/category, units, choices, range and multiplex metadata.
* `PGW_codec_decode(id, extended, fd, bytes, length, output, capacity, &count)`:
  strict frame format/length, active signals only; insufficient capacity returns
  required count without writing partial samples. Output order is message frame
  ID then signal name, **not** numeric signal ID order.
* `PGW_codec_patch(signal, baseline, length)`: caller selects the correct message
  buffer via the signal descriptor's `message_index`. Wrong tags, unknown IDs,
  nonfinite/out-of-range values, inactive multiplex branches and selector
  changes are rejected without mutation. Successful patch changes only that
  signal's bits. Inverse scaling rounds nearest, ties away from zero, including
  negative values. All integer mappings use exact int64 scaling and unsigned
  magnitude inverse rounding, never passing through floating point.

The caller must enforce baseline existence, stage/coalesce command batches,
frame identity, and commit transmit shadows only after successful transmission.
The pure codec neither stores baselines nor manufactures zero/default frames.
Received values outside a DBC's advertised application range can be decoded;
that range is enforced for commands. Private timestamp/validity/transport metadata
does not change the logical signal schema.

Supported: Classic/FD canonical payload sizes (1–8, FD also 12/16/20/24/32/48/64),
standard/extended CAN IDs, Intel/Motorola bit numbering, signed integers,
scale/offset including negative scale, choices, one simple unsigned/unscaled
selector per frame with a single branch ID per multiplexed signal. Unsigned
raw widths above 63 bits, floating-point DBC fields, nested/extended mux,
signal groups/type references, long-symbol renaming, unassigned signals and
higher-level protocols fail explicitly.
Cantools validates syntax/bounds/overlaps; gateway validation independently
checks mapping, branch overlap and union representability. Comments, nodes and
nonoperational descriptive DBC attributes are not runtime features. Exact decimal
tokens are retained as rational manifest fields and used for range validation;
parser conversion to binary64 cannot silently loosen declared bounds.

Integer scale/offset and engineering endpoints/intermediates must fit int64.
Double raw/physical ranges must fit 53-bit bounds, preserve adjacent
endpoint values and round-trip endpoint probes. Fractional mappings also require
endpoint binary64 resolution no larger than one quarter of a raw step. This is a deliberately bounded
subset, not support for arbitrary DBC numeric precision.

CMake consumers add `bindings/signal` then `tools/dbc_codegen` and call
`pgw_generate_dbc(target absolute_dbc absolute_mapping output_directory)`.
Configure top-level `PGW_PYTHON_EXECUTABLE` to the pinned environment interpreter
to select it for all host tools. The component-specific `PGW_DBC_PYTHON` defaults
to the selected `Python3_EXECUTABLE`; it remains available as an explicit
component override. Clear any cached component override when switching the
common interpreter. Configure the local environment explicitly, for example
`-DPGW_PYTHON_EXECUTABLE="$PWD/.venv/bin/python"`.
The independent `Python3_EXECUTABLE` used by other host tools may remain the
system Python. Configuration validates the pinned parser version and reports
the local-environment setup command if dependencies are missing. No automatic
global installation occurs.

Generated C, metadata and IDL are build outputs only, never source-tree
deliverables. Codec tests generate their own build-directory target from the
example DBC/mapping and execute maintained golden-byte vectors against it.
Host tests compare independent generation runs and a fixed schema fingerprint,
not stale generated source files.

## Optional typed Micro binding

`bindings/signal/src/dds_binding.c` is separate from the pure codec and native
header-only target. Compile it alongside generated `signals.c`,
`signalsPlugin.c`, `signalsSupport.c`, and `pgw_codec.c`; link
`PGW::adapter_dds_connext_micro`. Include `<pgw/signal_dds.h>` for the adapter-owned
`PGW_DDSBinding` interface. `PGW_signal_dds_binding` registers all inventory
keys; `PGW_signal_dds_binding_<category>` registers only its category keys, so
category writers can retain category-sized key/history bounds. All share the
same logical schema/fingerprint. DDS entity topology remains MAG-owned.

Typed state and native adapter wrappers are distinct. The binding initializes
sequence descriptors and one reusable typed scratch sample from the caller's
arena, takes/returns typed middleware loans, explicitly copies union arms to
native values, and writes with an optional portable source timestamp supplied
by the adapter. Unknown IDs, wrong per-ID tags and nonfinite double values are
rejected by conversion. Middleware key registration occurs during adapter
initialization, at portable epoch time; transport command range/baseline
validation remains in the codec/adapter. `tests/codegen/test_dds_conversion.c`
checks typed conversion against the installed Micro-generated type support.
Run Micro type generation on the emitted **`signals.idl`**, not directly on
`resources/signal.idl`: a generated header named `signal.h` can shadow the POSIX
system header when its directory is added to compiler include paths.

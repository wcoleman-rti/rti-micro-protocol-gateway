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

"""Flatten local IDL includes in dependency order for standalone generators."""
import argparse
from pathlib import Path
import re
import sys


INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*(?://.*)?$')


class FlattenError(ValueError):
    pass


def flatten(inputs, include_dirs=()):
    directories = tuple(Path(directory).resolve() for directory in include_dirs)
    emitted = set()
    active = []
    output = []

    def resolve(name, parent):
        candidates = (parent / name, *(directory / name for directory in directories))
        for candidate in candidates:
            if candidate.is_file():
                return candidate.resolve()
        raise FlattenError(f"unresolved IDL include {name!r} from {parent}")

    def visit(path):
        source = Path(path).resolve()
        if source in active:
            cycle = " -> ".join(str(item) for item in (*active, source))
            raise FlattenError(f"cyclic IDL include: {cycle}")
        if source in emitted:
            return
        if not source.is_file():
            raise FlattenError(f"IDL input does not exist: {source}")
        active.append(source)
        try:
            for line_number, line in enumerate(source.read_text().splitlines(), 1):
                match = INCLUDE.fullmatch(line)
                if match:
                    dependency = resolve(match.group(1), source.parent)
                    visit(dependency)
                else:
                    output.append(line)
        except UnicodeDecodeError as exc:
            raise FlattenError(f"IDL is not UTF-8: {source}") from exc
        finally:
            active.pop()
        emitted.add(source)
        output.append("")

    for path in inputs:
        visit(path)
    return "\n".join(output).rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("-I", "--include-dir", action="append", default=[])
    args = parser.parse_args()
    try:
        text = flatten(args.inputs, args.include_dir)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    except (FlattenError, OSError) as exc:
        parser.exit(1, f"IDL flattening failed: {exc}\n")


if __name__ == "__main__":
    main()

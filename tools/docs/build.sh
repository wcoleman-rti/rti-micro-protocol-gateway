#!/usr/bin/env bash
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is under no
# obligation to maintain or support the Software. RTI shall not be liable for any
# incidental or consequential damages arising out of the use or inability to use
# the software.

set -eu

rm -rf build/docs-source
mkdir -p build/docs-source
doxygen Doxyfile
mkdir -p build/docs-source/docs \
    build/docs-source/adapters/can \
    build/docs-source/adapters/dds/connext_micro \
    build/docs-source/examples/can_dds \
    build/docs-source/tools/dbc_codegen
for page in docs/*.md; do
    if [ "$page" = "docs/index.md" ]; then
        continue
    fi
    cp "$page" build/docs-source/docs/
done
cp docs/index.md LICENSE build/docs-source/
cp adapters/can/README.md build/docs-source/adapters/can/
cp adapters/dds/connext_micro/README.md \
    build/docs-source/adapters/dds/connext_micro/
cp examples/can_dds/README.md build/docs-source/examples/can_dds/
cp tools/dbc_codegen/README.md build/docs-source/tools/dbc_codegen/
zensical build --clean --strict

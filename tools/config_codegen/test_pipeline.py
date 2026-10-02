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

"""Gateway-specific MAG topology/warning integration; never creates DDS entities."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("--dds", required=True)
parser.add_argument("--wrapper", required=True)
parser.add_argument("--mag", required=True)
args = parser.parse_args()
directory = Path.cwd() / f"mag-pipeline-{os.getpid()}"
directory.mkdir()
try:
    source = ET.parse(args.dds).getroot()
    def generate(name, root, success=True):
        output = directory / name
        output.mkdir()
        xml = output / "model.xml"
        ET.ElementTree(root).write(xml, encoding="utf-8", xml_declaration=True)
        result = subprocess.run([sys.executable, args.wrapper, args.mag, "-language", "C",
            "-replace", "-d", str(output), "-idlFile", "model.idl", str(xml)],
            text=True, capture_output=True, check=False)
        if (result.returncode == 0) != success:
            raise AssertionError(result.stdout + result.stderr)
        if not success:
            assert "warn" in (result.stdout + result.stderr).lower(), result.stdout + result.stderr
            assert "warnings rejected" in result.stderr, result.stdout + result.stderr
            return None
        header = (output / "modelAppgen.h").read_text()
        return [int(value) for value in re.findall(r"([0-9]+)L, /\* local_writer_allocation", header)]
    baseline = generate("baseline", source)
    assert baseline
    augmented = ET.fromstring(ET.tostring(source))
    publisher = augmented.find("domain_participant_library/domain_participant/publisher")
    extra = ET.fromstring(ET.tostring(publisher[0]))
    extra.set("name", "ExtraStatePowertrain")
    publisher.append(extra)
    larger = generate("additional_endpoint", augmented)
    assert sum(larger) > sum(baseline), (baseline, larger)
    unsupported = ET.fromstring(ET.tostring(source))
    qos = unsupported.find("qos_library/qos_profile[@name='Powertrain']/datawriter_qos")
    qos.append(ET.fromstring("<lifespan><duration><sec>1</sec><nanosec>0</nanosec></duration></lifespan>"))
    generate("unsupported_qos", unsupported, success=False)
    print("PASS: actual MAG topology update and unsupported-policy warnings rejected")
finally:
    shutil.rmtree(directory)

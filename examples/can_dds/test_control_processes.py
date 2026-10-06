#!/usr/bin/env python3
#
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI shall not
# be liable for any incidental or consequential damages arising out of the use
# or inability to use the software.
#

"""Exercise one controller command across separate gateway/controller processes."""
import argparse
from pathlib import Path
import re
import re
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("--gateway", required=True)
parser.add_argument("--controller", required=True)
parser.add_argument("--domain", type=int, required=True)
parser.add_argument("--controller-idl", required=True)
parser.add_argument("--telemetry", action="store_true")
args = parser.parse_args()

idl = Path(args.controller_idl).read_text()
match = re.search(r"enum Resource\s*\{([^}]*)\}", idl)
if not match:
    parser.error("generated controller IDL has no Resource enum")
resources = [item.strip() for item in match.group(1).split(",") if item.strip()]
try:
    resource_id = resources.index("RESOURCE_ROUTE_COMMAND_POWERTRAIN")
except ValueError:
    parser.error("powertrain command route was not generated as a control resource")

gateway_command = [args.gateway, "--memory", "--control-domain", str(args.domain)]
if args.telemetry:
    gateway_command.extend(["--telemetry-period-ms", "100"])
gateway_command.append("100000000")
gateway = subprocess.Popen(
    gateway_command,
    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
try:
    controller_command = [
        args.controller, str(args.domain), str(resource_id), "6"
    ]
    if args.telemetry:
        controller_command.append("--telemetry")
    controller = subprocess.run(
        controller_command,
        capture_output=True, text=True, timeout=15, check=False)
    if controller.returncode:
        raise RuntimeError(
            f"controller failed ({controller.returncode}):\n"
            f"{controller.stdout}{controller.stderr}")
    if "ControlCommand matched readers=1" not in controller.stdout or \
            "result: APPLIED" not in controller.stdout:
        raise RuntimeError(f"controller did not observe the applied command:\n{controller.stdout}")
    if "state: resource=13 status=3" not in controller.stdout:
        raise RuntimeError(f"controller did not observe PAUSED state:\n{controller.stdout}")
    if args.telemetry and "telemetry: resource=0 kind=0" not in controller.stdout:
        raise RuntimeError(f"controller did not observe selected telemetry:\n{controller.stdout}")
    if args.telemetry and "unit=frames value=" not in controller.stdout:
        raise RuntimeError(f"controller telemetry value/unit is incorrect:\n{controller.stdout}")
    if not re.search(
            r"result: APPLIED publication-sequence=[0-9-]+:[0-9]+ handle=[0-9a-f]{32}\b",
            controller.stdout):
        raise RuntimeError(f"result correlation is missing DDS publication metadata:\n"
                           f"{controller.stdout}")
finally:
    gateway.terminate()
    try:
        _, gateway_error = gateway.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        gateway.kill()
        _, gateway_error = gateway.communicate()
    if gateway.returncode not in (0, -15):
        raise RuntimeError(f"gateway failed ({gateway.returncode}):\n{gateway_error}")
print("PASS: runtime-domain controller paused route and received result/state")

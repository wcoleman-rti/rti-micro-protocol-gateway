"""Owned companion/gateway processes, actual DDS, memory CAN; no shared CAN traffic."""
import argparse
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("--gateway", required=True)
parser.add_argument("--companion", required=True)
args = parser.parse_args()
gateway = subprocess.Popen([args.gateway, "--memory", "1800"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
try:
    time.sleep(0.1)
    companion = subprocess.run([args.companion, "123.4", "100"],
                               capture_output=True, text=True, timeout=15, check=False)
    stdout, stderr = gateway.communicate(timeout=15)
    if companion.returncode or gateway.returncode:
        raise AssertionError(f"gateway({gateway.returncode}):\n{stdout}\n{stderr}\n"
                             f"companion({companion.returncode}):\n{companion.stdout}\n{companion.stderr}")
    assert "state key=1001 kind=2 value=1000" in companion.stdout, companion.stdout
    assert "CAN memory sent id=256 bytes=d20400000101abcd" in stdout, stdout
    print("PASS: separate real DDS companion and gateway processes; exact patched memory-CAN bytes")
finally:
    if gateway.poll() is None:
        gateway.kill()
        gateway.communicate()

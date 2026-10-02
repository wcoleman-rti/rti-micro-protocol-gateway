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

"""Refuse existing DDS sockets; serialize cooperating tests without shared traffic."""
import argparse
import fcntl
from pathlib import Path
import socket
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--domain", type=int, required=True)
parser.add_argument("command", nargs=argparse.REMAINDER)
args = parser.parse_args()
if not 1 <= args.domain <= 232:
    parser.error("unsafe domain")
if not args.command:
    parser.error("command required")
lock_directory = Path(__file__).resolve().parents[2] / "build-dds-domain-locks"
lock_directory.mkdir(exist_ok=True)
with (lock_directory / f"domain-{args.domain}.lock").open("a") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    base = 7400 + 250 * args.domain
    reservations = []
    try:
        for port in range(base, base + 250):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            reservations.append(sock)
            sock.bind(("0.0.0.0", port))
    except OSError as exc:
        print(f"SKIP: DDS domain {args.domain} already has sockets: {exc}", file=sys.stderr)
        sys.exit(77)
    finally:
        for sock in reservations:
            sock.close()
    sys.exit(subprocess.run(args.command, check=False).returncode)

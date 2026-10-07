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

"""Persist benchmark measurements and provenance; never changes host setup."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import signal
import subprocess
import threading


def source_digest(root):
    digest = hashlib.sha256()
    excluded = {".git", ".venv", "build", "results", "__pycache__"}
    for path in sorted(root.rglob("*")):
        if not path.is_file() or any(part in excluded or part.startswith("build-")
                                     for part in path.relative_to(root).parts):
            continue
        digest.update(str(path.relative_to(root)).encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()

def git_provenance(root):
    git = shutil.which("git")
    if not git:
        return {"available": False, "reason": "git not installed; source digest recorded"}
    revision = subprocess.run([git, "-C", str(root), "rev-parse", "HEAD"],
                              text=True, capture_output=True)
    if revision.returncode:
        return {"available": False, "reason": "no Git revision; source digest recorded"}
    status = subprocess.run([git, "--no-pager", "-C", str(root), "status", "--porcelain"],
                            text=True, capture_output=True, check=True)
    return {"available": True, "revision": revision.stdout.strip(), "dirty": bool(status.stdout)}


def input_fingerprints(root):
    fingerprints = {}
    for folder in ("schemas", "config", "examples"):
        for path in sorted((root / folder).rglob("*")):
            if path.is_file() and (
                    path.suffix in {".idl", ".xml", ".xsd", ".dbc", ".json"} or
                    path.name.endswith((".idl.in", ".xml.in", ".json.in"))):
                fingerprints[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return fingerprints


def workload_configuration(measurement):
    keys = {"workload", "batches", "timing", "allocation_coverage", "schema_fingerprint",
            "metadata_capture", "metadata_preservation", "configuration",
            "backpressure_period_batches", "sample_rate_basis"}
    signature = {key: value for key, value in measurement.items()
                 if key in keys or key.endswith(("_capacity", "_budget"))}
    latency = measurement.get("dispatch_latency_ns", {})
    signature["timing_boundary"] = latency.get("boundary")
    signature["histogram"] = latency.get("histogram")
    return signature


def sample_rss(pid, stop, samples):
    try:
        with pathlib.Path(f"/proc/{pid}/status").open() as status:
            while not stop.is_set():
                status.seek(0)
                for line in status:
                    if line.startswith("VmRSS:"):
                        samples.append(int(line.split()[1]))
                stop.wait(0.01)
    except (FileNotFoundError, ProcessLookupError):
        return


def terminate_owned_group(process):
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.communicate()


def profile_process(perf, command, timeout):
    process = subprocess.Popen(
        [perf, "stat", "-x,", "-e", "task-clock,cycles,instructions", "--"] + command,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        terminate_owned_group(process)
        return {"status": "unavailable", "reason": "owned-process perf run timed out"}
    finally:
        if process.poll() is None:
            terminate_owned_group(process)
    return {
        "status": "captured" if process.returncode == 0 else "unavailable",
        "returncode": process.returncode, "statistics_or_error": stderr,
        "benchmark_stdout": stdout,
        "scope": "extra run of owned benchmark process only; no global perf settings changed",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--batches", type=int, default=100000)
    parser.add_argument("--timing", choices=("0", "1"), default="1")
    parser.add_argument("--metadata", choices=("0", "1"),
                        help="adapter workload's optional third argument; see workload-specific policy")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=300,
                        help="per-process timeout in seconds; only owned child processes are terminated")
    parser.add_argument("--results", type=pathlib.Path, default=pathlib.Path("results"))
    parser.add_argument("--compare", type=pathlib.Path)
    parser.add_argument("--perf", action="store_true",
                        help="extra owned-process perf stat run; permission failures are reported")
    args = parser.parse_args()
    if not 1 <= args.batches <= 100000000 or not 1 <= args.repeat <= 100:
        parser.error("batches/repeat outside bounded supported range")
    if not 1 <= args.timeout <= 3600:
        parser.error("timeout must be between 1 and 3600 seconds")
    root = pathlib.Path(__file__).resolve().parents[2]
    executable = args.executable.resolve(strict=True)
    cache = executable.parent
    while cache != cache.parent and not (cache / "CMakeCache.txt").exists():
        cache = cache.parent
    build_configuration = {}
    if (cache / "CMakeCache.txt").exists():
        for line in (cache / "CMakeCache.txt").read_text().splitlines():
            if line.startswith(("PGW_", "CMAKE_C_COMPILER:", "CMAKE_C_FLAGS", "CMAKE_BUILD_TYPE:",
                                "RTIConnextMicroDDS_", "RTI_MICRO_")) and "=" in line:
                key, value = line.split("=", 1)
                build_configuration[key] = value
    compiler = build_configuration.get("CMAKE_C_COMPILER:FILEPATH")
    if compiler:
        build_configuration["compiler_version"] = subprocess.check_output(
            [compiler, "--version"], text=True).splitlines()[0]
    provenance = cache / "provenance.json"
    if not provenance.exists():
        for configuration in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"):
            if configuration in executable.relative_to(cache).parts:
                provenance = cache / configuration / "provenance.json"
                break
    toolchain = (json.loads(provenance.read_text()) if provenance.exists() else
                 {"available": False, "reason": "CMake build provenance not found"})
    measurements = []
    command = [str(executable), str(args.batches), args.timing]
    if args.metadata is not None:
        command.append(args.metadata)
    for _ in range(args.repeat):
        process = subprocess.Popen(command,
                                   text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   start_new_session=True)
        rss_samples = []
        stop = threading.Event()
        sampler = threading.Thread(target=sample_rss, args=(process.pid, stop, rss_samples))
        sampler.start()
        try:
            stdout, stderr = process.communicate(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            terminate_owned_group(process)
            raise
        finally:
            if process.poll() is None:
                terminate_owned_group(process)
            stop.set()
            sampler.join()
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, process.args, stdout, stderr)
        measurement = json.loads(stdout)
        measurement["external_rss"] = {
            "interval_ms": 10, "samples": len(rss_samples),
            "peak_kib": max(rss_samples) if rss_samples else None,
            "scope": "sampled process resident set; not component attribution",
        }
        measurements.append(measurement)
    result = {
        "manifest_version": 1,
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source_digest": source_digest(root),
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "host": platform.platform(),
        "machine": platform.machine(),
        "build_configuration": build_configuration,
        "toolchain": toolchain,
        "version_control": git_provenance(root),
        "input_fingerprints": input_fingerprints(root),
        "seed": 0, "warmup_batches": 0,
        "metadata_requested": args.metadata,
        "process_timeout_seconds": args.timeout,
        "measurements": measurements,
    }
    if args.perf:
        perf = shutil.which("perf")
        if perf is None:
            result["external_profile"] = {"status": "unavailable", "reason": "perf is not installed"}
        else:
            result["external_profile"] = profile_process(perf, command, args.timeout)
    if args.compare:
        baseline = json.loads(args.compare.read_text())
        before = baseline["measurements"][0]
        after = measurements[0]
        comparable = (workload_configuration(before) == workload_configuration(after) and
                      baseline["build_configuration"] == build_configuration and
                      baseline.get("input_fingerprints") == result["input_fingerprints"] and
                      baseline.get("toolchain") == result["toolchain"] and
                      baseline.get("metadata_requested") == result["metadata_requested"] and
                      baseline["machine"] == result["machine"] and
                      baseline["host"] == result["host"])
        result["comparison"] = {"comparable": comparable}
        if comparable:
            old_rate = sum(x["samples_per_second"] for x in baseline["measurements"]) / len(baseline["measurements"])
            new_rate = sum(x["samples_per_second"] for x in measurements) / len(measurements)
            result["comparison"]["throughput_percent_delta"] = 100 * (new_rate / old_rate - 1)
        else:
            result["comparison"]["reason"] = "workload, input fingerprints, instrumentation, toolchain, build options or host differ"
    args.results.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    label = executable.stem.removeprefix("pgw_").removesuffix("_benchmark")
    label = re.sub(r"[^A-Za-z0-9_-]", "-", label)
    output = args.results / f"{label}-{stamp}.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(output.resolve())


if __name__ == "__main__":
    main()

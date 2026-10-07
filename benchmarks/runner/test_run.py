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

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


class CaptureRequirements(unittest.TestCase):
    def test_timeout_reaps_owned_process_and_does_not_persist_success(self):
        runner = pathlib.Path(__file__).with_name("run.py")
        with tempfile.TemporaryDirectory(prefix="pgw-timeout-") as directory:
            root = pathlib.Path(directory)
            pid_file = root / "pid"
            executable = root / "blocked-workload"
            executable.write_text(
                f"#!{sys.executable}\n"
                "import os, pathlib, time\n"
                f"pathlib.Path({str(pid_file)!r}).write_text(str(os.getpid()))\n"
                "time.sleep(60)\n")
            executable.chmod(0o700)
            completed = subprocess.run(
                [sys.executable, str(runner), str(executable), "--batches", "1",
                 "--repeat", "1", "--timeout", "1", "--results", str(root / "results")],
                text=True, capture_output=True, timeout=10)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("TimeoutExpired", completed.stderr)
            self.assertFalse((root / "results").exists())
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pid_file.read_text()), 0)

    def test_persistence_comparison_and_instrumentation_scope(self):
        executable = os.environ["PGW_BENCHMARK"]
        runner = pathlib.Path(__file__).with_name("run.py")
        with tempfile.TemporaryDirectory(prefix="pgw-benchmark-") as directory:
            command = [sys.executable, str(runner), executable, "--batches", "1000",
                       "--repeat", "2", "--results", directory]
            first = pathlib.Path(subprocess.check_output(command, text=True).strip())
            baseline = json.loads(first.read_text())
            self.assertEqual(len(baseline["measurements"]), 2)
            self.assertEqual(len(baseline["source_digest"]), 64)
            self.assertTrue(baseline["build_configuration"])
            for measurement in baseline["measurements"]:
                self.assertEqual(measurement["samples"], 4000)
                self.assertEqual(measurement["gateway_runtime_allocations"], 0)
                self.assertEqual(measurement["dispatch_latency_ns"]["count"], 1000)
                self.assertIn("external_rss", measurement)
            second = pathlib.Path(subprocess.check_output(
                command + ["--compare", str(first)], text=True).strip())
            self.assertTrue(json.loads(second.read_text())["comparison"]["comparable"])
            third = pathlib.Path(subprocess.check_output(
                command + ["--timing", "0", "--compare", str(first)], text=True).strip())
            result = json.loads(third.read_text())
            self.assertFalse(result["comparison"]["comparable"])
            self.assertEqual(result["measurements"][0]["dispatch_latency_ns"]["count"], 0)


if __name__ == "__main__":
    unittest.main()

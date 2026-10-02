"""Verify launcher environment and diagnostics, not vendor generator behavior."""
import os
import re
import subprocess
import sys
import unittest

WRAPPER, JRE, STRICT, ALLOW = sys.argv[1:]
sys.argv = sys.argv[:1]


def launch(code):
    return subprocess.run(
        [sys.executable, WRAPPER, sys.executable, "-c", code],
        env=dict(os.environ, JREHOME="incorrect-inherited-runtime"),
        capture_output=True, text=True,
    )


class LauncherContract(unittest.TestCase):
    def test_persistent_jre_and_output(self):
        result = launch("import os; print(os.environ['JREHOME'])")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), JRE)

    def test_nonzero_exit_propagates(self):
        result = launch("import sys; print('failure', file=sys.stderr); sys.exit(7)")
        self.assertEqual(result.returncode, 7)
        self.assertIn("failure", result.stderr)

    def test_zero_status_error_is_rejected(self):
        result = launch("print('ERROR simulated generator diagnostic')")
        self.assertEqual(result.returncode, 1)

    def test_warning_policy(self):
        warning = "WARN simulated generator warning"
        result = launch(f"print({warning!r})")
        rejected = STRICT in ("ON", "TRUE", "1") and (
            not ALLOW or not re.search(ALLOW, warning))
        self.assertEqual(result.returncode, 1 if rejected else 0)
        self.assertIn(warning, result.stdout)


if __name__ == "__main__":
    unittest.main()

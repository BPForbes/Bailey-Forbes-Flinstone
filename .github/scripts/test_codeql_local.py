"""Tests for scripts/codeql_local.sh (no CodeQL CLI needed).

Run: python3 -m unittest discover -s .github/scripts
"""

import os
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SCRIPT = os.path.join(ROOT, "scripts", "codeql_local.sh")


def run(args, env_extra=None):
    env = {"PATH": "/usr/bin:/bin", "HOME": os.environ.get("HOME", "/tmp")}
    env.update(env_extra or {})
    return subprocess.run(["bash", SCRIPT] + args, capture_output=True, text=True, env=env)


class LocalRunner(unittest.TestCase):
    def test_dry_run_prints_the_same_analysis_the_workflow_runs(self):
        r = run(["--dry-run", "--out", "/tmp/cq-unit"])
        self.assertEqual(r.returncode, 0, r.stderr)
        for lang in ("c-cpp", "javascript-typescript", "python", "actions"):
            self.assertIn(f"--language={lang}", r.stdout)
            self.assertIn(f"--sarif-category=/language:{lang}", r.stdout)
        self.assertIn("--codescanning-config=.github/codeql/codeql-config.yml", r.stdout)
        self.assertIn("cpp-security-and-quality.qls", r.stdout)
        self.assertIn("codeql_report.py", r.stdout)
        # C is traced through the project's own build, after the Rust library is built
        self.assertLess(r.stdout.index("cargo build"), r.stdout.index("--language=c-cpp"))
        self.assertIn("make\\ -B\\ ARCH=x86_64_gas", r.stdout)

    def test_a_single_language_can_be_chosen(self):
        r = run(["--dry-run", "--lang", "python", "--out", "/tmp/cq-unit"])
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("--language=python", r.stdout)
        self.assertNotIn("--language=c-cpp", r.stdout)
        self.assertNotIn("cargo build", r.stdout)

    def test_it_says_assembly_is_not_analysed(self):
        r = run(["--dry-run", "--lang", "actions"])
        self.assertIn("assembly", r.stdout)
        self.assertIn("not analysed", r.stdout)

    def test_without_the_cli_it_is_not_run_and_never_a_pass(self):
        r = run(["--lang", "python"])
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("NOT RUN", r.stderr)

    def test_bad_arguments_are_refused(self):
        for args in (["--lang", "cobol"], ["--bogus"], ["--lang"], ["--out"]):
            r = run(args)
            self.assertEqual(r.returncode, 2, (args, r.stderr))

    def test_the_cli_path_can_be_given(self):
        r = run(["--dry-run", "--lang", "actions"], {"CODEQL": "/opt/codeql/codeql"})
        self.assertIn("/opt/codeql/codeql database create", r.stdout)


if __name__ == "__main__":
    unittest.main()

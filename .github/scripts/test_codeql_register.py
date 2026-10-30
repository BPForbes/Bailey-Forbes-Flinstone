"""Tests for the accepted-findings register in codeql_report.py.

Run: python3 -m unittest discover -s .github/scripts
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "codeql_report.py")

RULE = {
    "id": "cpp/world-writable-file-creation",
    "helpUri": "https://codeql.github.com/codeql-query-help/cpp/cpp-world-writable-file-creation/",
    "fullDescription": {"text": "Creating a file with the default mode can make it writable by others."},
    "help": {"text": "## Recommendation\nCreate the file with an explicit owner-only mode.\n"},
    "properties": {"security-severity": "7.8", "tags": ["security", "external/cwe/cwe-732"]},
}


def result(uri, line=10):
    return {
        "ruleId": RULE["id"],
        "level": "error",
        "message": {"text": "created with default permissions"},
        "locations": [{"physicalLocation": {"artifactLocation": {"uri": uri}, "region": {"startLine": line}}}],
    }


def entry(**kw):
    e = {
        "id": "CQ-001",
        "rule": RULE["id"],
        "file": "userland/shell/audit_log.c",
        "count": 1,
        "disposition": "tracked-fix",
        "justification": "The audit log is created with the default mode; a follow-up opens it 0600.",
        "owner": "BPForbes",
        "added": "2026-10-30",
        "review_by": "2027-01-28",
    }
    e.update(kw)
    return e


def run(results, entries=None, today="2026-11-01", raw_register=None):
    sarif = {"runs": [{"tool": {"driver": {"name": "CodeQL"}, "extensions": [{"rules": [RULE]}]},
                       "results": results}]}
    with tempfile.TemporaryDirectory() as d:
        sarif_path = os.path.join(d, "t.sarif")
        with open(sarif_path, "w", encoding="utf-8") as f:
            json.dump(sarif, f)
        args = [sys.executable, SCRIPT, "--repo-root", d, "--today", today]
        if entries is not None or raw_register is not None:
            reg = os.path.join(d, "reg.json")
            with open(reg, "w", encoding="utf-8") as f:
                f.write(raw_register if raw_register is not None
                        else json.dumps({"schema": 1, "entries": entries}))
            args += ["--accepted", reg]
        return subprocess.run(args + [sarif_path], capture_output=True, text=True)


class Register(unittest.TestCase):
    def test_a_covered_finding_is_listed_with_its_reason_and_does_not_fail(self):
        r = run([result("userland/shell/audit_log.c")], [entry()])
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("1 accepted through the register", r.stdout)
        self.assertIn("Accepted findings (1)", r.stdout)
        self.assertIn("tracked-fix (CQ-001, owner BPForbes, review by 2027-01-28)", r.stdout)
        self.assertIn("SOC 2: CC6.1 Logical access", r.stdout)  # CWE-732: incorrect permissions
        self.assertIn("audit log is created with the default mode", r.stdout)

    def test_an_instance_beyond_the_count_is_a_new_finding_and_fails(self):
        r = run([result("userland/shell/audit_log.c", 10), result("userland/shell/audit_log.c", 99)], [entry()])
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("1 accepted through the register", r.stdout)
        self.assertIn("Failing the check:\n", r.stdout)
        self.assertIn("audit_log.c:99", r.stdout)

    def test_a_different_file_or_rule_is_not_covered(self):
        self.assertEqual(run([result("userland/shell/util.c")], [entry()]).returncode, 1)
        other = dict(entry(), rule="cpp/path-injection")
        self.assertEqual(run([result("userland/shell/audit_log.c")], [other]).returncode, 1)

    def test_an_entry_past_its_review_date_fails_even_with_no_finding(self):
        r = run([], [entry(review_by="2026-10-31")], today="2026-11-01")
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("past their review date", r.stdout)
        self.assertIn("CQ-001", r.stdout)

    def test_an_overdue_entry_no_longer_covers_its_finding(self):
        r = run([result("userland/shell/audit_log.c")], [entry(review_by="2026-10-31")], today="2026-11-01")
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertNotIn("Accepted findings (1)", r.stdout)

    def test_spare_capacity_is_reported_so_the_register_can_be_tightened(self):
        r = run([], [entry(count=3)])
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("unused capacity", r.stdout)
        self.assertIn("allows 3 more than were found", r.stdout)

    def test_findings_in_test_code_never_need_an_entry(self):
        r = run([result("tests/test_audit.c")], [entry()])
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("Findings in test code (1)", r.stdout)

    def test_an_invalid_register_is_refused_and_every_problem_is_named(self):
        bad = [
            entry(justification="too short"),
            entry(id="CQ-002", disposition="ignore"),
            entry(id="CQ-003", count=0),
            entry(id="CQ-004", review_by="soon"),
            entry(id="CQ-001"),
        ]
        r = run([], bad)
        self.assertEqual(r.returncode, 2, r.stdout)
        for want in ("justification must say why", "disposition must be one of", "count must be",
                     "review_by must be YYYY-MM-DD", "duplicate id"):
            self.assertIn(want, r.stderr)

    def test_a_register_without_a_schema_is_refused(self):
        r = run([], raw_register=json.dumps({"entries": []}))
        self.assertEqual(r.returncode, 2, r.stdout)

    def test_a_missing_register_changes_nothing(self):
        r = run([result("userland/shell/audit_log.c")])
        self.assertEqual(r.returncode, 1, r.stdout)


if __name__ == "__main__":
    unittest.main()

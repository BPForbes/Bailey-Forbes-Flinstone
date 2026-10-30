"""Tests for codeql_report.py. Run: python3 -m unittest discover -s .github/scripts"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "codeql_report.py")

RULE_HIGH = {
    "id": "cpp/hardcoded-key",
    "helpUri": "https://codeql.github.com/codeql-query-help/cpp/cpp-hardcoded-key/",
    "fullDescription": {"text": "Using a hard-coded cryptographic value makes it easy to recover protected data."},
    "help": {"text": "## Recommendation\nGenerate the key randomly.\n## References\n"},
    "properties": {"security-severity": "9.8", "tags": ["security", "external/cwe/cwe-798"]},
}
RULE_OVERRUN = {
    "id": "cpp/overrunning-write",
    "helpUri": "https://codeql.github.com/codeql-query-help/cpp/cpp-overrunning-write/",
    "fullDescription": {"text": "Buffer write operations that do not control the length of data written may overflow."},
    "help": {"text": "## Recommendation\nUse a bounded copy.\n## References\n"},
    "properties": {"security-severity": "9.3", "tags": ["security", "external/cwe/cwe-120"]},
}
RULE_QUALITY = {"id": "cpp/unused-local-variable", "properties": {"tags": ["maintainability"]}}


def result(rule_id, uri, line=1, level="error"):
    return {
        "ruleId": rule_id,
        "level": level,
        "message": {"text": f"{rule_id} here"},
        "locations": [{"physicalLocation": {"artifactLocation": {"uri": uri}, "region": {"startLine": line}}}],
    }


def run_report(rules, results, rules_in="extensions"):
    tool = {"driver": {"name": "CodeQL"}}
    if rules_in == "extensions":
        # How CodeQL really writes them: under the query pack, not the driver.
        tool["extensions"] = [{"name": "codeql/cpp-queries", "rules": rules}]
    else:
        tool["driver"]["rules"] = rules
    sarif = {"runs": [{"tool": tool, "results": results}]}
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "t.sarif")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(sarif, f)
        return subprocess.run(
            [sys.executable, SCRIPT, "--repo-root", d, path], capture_output=True, text=True
        )


class Report(unittest.TestCase):
    def test_high_security_finding_in_shipped_code_fails_with_all_five_parts(self):
        for where in ("extensions", "driver"):
            r = run_report([RULE_HIGH], [result(RULE_HIGH["id"], "kernel/core/identity/user_db.c")], where)
            self.assertEqual(r.returncode, 1, (where, r.stdout))
            out = r.stdout
            self.assertIn("Source: CodeQL `cpp/hardcoded-key`", out)
            self.assertIn("https://cwe.mitre.org/data/definitions/798.html", out)
            self.assertIn("Source quote: > Using a hard-coded cryptographic value", out)
            self.assertIn("SOC 2: C1.1 Confidential information", out)
            self.assertIn("Fix: Generate the key randomly.", out)
            self.assertIn("Failing the check:\n- cpp/hardcoded-key at kernel/core/identity/user_db.c:1", out)
            self.assertLess(out.index("GATE **"), out.index("SOC 2:"))

    def test_the_same_finding_in_test_code_is_listed_but_does_not_fail(self):
        r = run_report([RULE_HIGH], [result(RULE_HIGH["id"], "tests/test_user_db.c")])
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("Findings in test code (1), listed but not failing the check", r.stdout)
        self.assertIn("- critical: cpp/hardcoded-key at tests/test_user_db.c:1 (test code)", r.stdout)
        self.assertNotIn("Source quote", r.stdout, "test findings are one line, not a full block")

    def test_quality_finding_is_not_a_soc2_finding_and_does_not_fail(self):
        r = run_report([RULE_QUALITY], [result(RULE_QUALITY["id"], "kernel/core/vfs/fs.c", level="note")])
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("SOC 2: none (correctness)", r.stdout)

    def test_workflow_rules_map_to_change_management(self):
        rule = {
            "id": "actions/code-injection/critical",
            "fullDescription": {"text": "User input may inject code."},
            "properties": {"security-severity": "9.0", "tags": ["security", "external/cwe/cwe-094"]},
        }
        r = run_report([rule], [result(rule["id"], ".github/workflows/x.yml")])
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("SOC 2: CC8.1 Change management", r.stdout)

    def test_a_path_finding_shows_its_source_to_sink_flow(self):
        rule = dict(RULE_HIGH, id="cpp/cleartext-storage-file")
        res = result(rule["id"], "kernel/core/vfs/kq_delivery.c", line=437)
        step = lambda line, text: {"location": {
            "physicalLocation": {"artifactLocation": {"uri": "kernel/core/vfs/kq_delivery.c"}, "region": {"startLine": line}},
            "message": {"text": text}}}
        res["codeFlows"] = [{"threadFlows": [{"locations": [step(430, "cert"), step(437, "cert.serial")]}]}]
        r = run_report([rule], [res])
        self.assertIn(
            "Flow: kernel/core/vfs/kq_delivery.c:430 (cert) -> kernel/core/vfs/kq_delivery.c:437 (cert.serial)",
            r.stdout,
        )

    def test_a_c_buffer_overrun_maps_to_processing_integrity(self):
        r = run_report([RULE_OVERRUN], [result(RULE_OVERRUN["id"], "kernel/core/net/net_codec.h")])
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("SOC 2: PI1.2 Processing integrity", r.stdout)
        self.assertIn("https://cwe.mitre.org/data/definitions/120.html", r.stdout)

    def test_flinstone_test_files_are_test_code(self):
        # tests/ and test_*.c are tests wherever they sit; a similarly named shipped file is not
        for uri, failing in (
            ("tests/test_kq_checkpoint.c", 0),
            ("kernel/core/vfs/test_helper_fixture.c", 0),
            ("scripts/test_browser_boot.cjs", 0),
            ("kernel/core/vfs/latest.c", 1),
            ("kernel/core/vfs/fs_provider.c", 1),
        ):
            r = run_report([RULE_OVERRUN], [result(RULE_OVERRUN["id"], uri)])
            self.assertEqual(r.returncode, failing, (uri, r.stdout))

    def test_c_secret_wipe_and_link_following_have_a_criterion(self):
        wipe = dict(RULE_OVERRUN, id="cpp/memset-may-be-deleted",
                    properties={"security-severity": "7.8", "tags": ["security", "external/cwe/cwe-14"]})
        link = dict(RULE_OVERRUN, id="cpp/path-injection",
                    properties={"security-severity": "7.5", "tags": ["security", "external/cwe/cwe-59"]})
        r = run_report([wipe, link], [result(wipe["id"], "kernel/core/identity/user_db.c"),
                                      result(link["id"], "kernel/core/vfs/server_shared_fs.c")])
        self.assertIn("SOC 2: C1.1 Confidential information", r.stdout)
        self.assertIn("SOC 2: CC6.6 System boundaries", r.stdout)

    def test_no_results_passes(self):
        self.assertEqual(run_report([RULE_HIGH], []).returncode, 0)


if __name__ == "__main__":
    unittest.main()

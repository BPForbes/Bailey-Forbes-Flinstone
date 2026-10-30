#!/usr/bin/env python3
"""Report CodeQL findings the way the review rules in AGENTS.md ask for them.

Used by .github/workflows/codeql.yml and by scripts/codeql_local.sh (the Security seat's local
run), so a finding reads the same wherever it was found.

Reads one or more SARIF files written by `github/codeql-action/analyze` and
prints, for every finding, the same five parts a reviewer must give: the source
(the CodeQL query help and the CWE), a verbatim quote of what that source says,
the offending lines quoted from the code, the SOC 2 Trust Services Criterion it
affects with a one-sentence reason, and the fix. The same text goes to the job
summary.

A finding that fails the check may be recorded in the accepted-findings register
(.github/codeql/accepted-findings.json) instead: every entry names the rule and file, how many
instances it covers, a disposition (false-positive, accepted-risk or tracked-fix), a written
justification, an owner, the date it was added and a review_by date. A covered finding is listed
under "Accepted findings" with its SOC 2 criterion and does not fail the check; an instance beyond
the entry's count is a new finding and fails; an entry past its review_by date fails the check
until someone reviews it again. Nothing is dismissed silently: the register is a reviewed file.

Exit status: 1 when a finding in shipped code is high or critical (a
`security-severity` of 7.0 or more, or a security-tagged result at level
"error"); 0 otherwise. Findings in test code and lower severities are listed
but do not fail the check.

Usage: codeql_report.py [--repo-root DIR] FILE.sarif [FILE.sarif ...]
"""

import argparse
import datetime
import json
import os
import re
import sys

# CWE number -> (SOC 2 criterion, why a finding of this class weakens it). Incorrect permissions
# (CWE-276, CWE-732: a file left writable or readable by others, an audit log included) are
# logical access, CC6.1, not the software-supply-chain row.
# Criteria are from the AICPA Trust Services Criteria (TSP section 100); the
# CWE numbers are from https://cwe.mitre.org/. A new mapping needs a one-line
# reason, as the review rules in AGENTS.md require for any SOC 2 claim.
# Grouped by the control the weakness undermines. A security finding whose CWE
# is not listed falls back to CC7.1 so it is still triaged against a criterion.
GROUPS = [
    (
        "CC6.1 Logical access",
        "lets someone act without the authentication or authorization check that should have stopped them",
        {276, 284, 285, 287, 306, 307, 346, 352, 384, 613, 639, 732, 862, 863, 942, 1275},
    ),
    (
        "CC6.6 System boundaries / PI1.2 Processing inputs",
        "lets unvalidated outside input change how the system behaves or what it stores",
        {20, 22, 23, 36, 59, 73, 74, 77, 78, 79, 80, 88, 89, 90, 94, 95, 116, 134, 502,
         601, 611, 915, 918, 943, 1321, 1333},
    ),
    (
        "CC6.7 Transmission and removal of information",
        "weakens the protection of data in transit or at rest, or sends it where it should not go",
        {295, 297, 310, 311, 319, 326, 327, 328, 329, 330, 338, 347, 757, 759, 760, 916, 1204},
    ),
    (
        "C1.1 Confidential information",
        "exposes or hardcodes confidential information such as a key, token or personal data",
        {14, 200, 201, 209, 226, 244, 259, 312, 313, 315, 321, 359, 497, 522, 523, 532, 798},
    ),
    (
        "CC7.2 Monitoring",
        "lets security-relevant events be forged, lost or recorded with secrets",
        {117, 778},
    ),
    (
        "A1.2 Availability",
        "lets a single request exhaust time, memory or storage and deny service",
        {400, 674, 730, 770, 776, 834, 835, 606, 405},
    ),
    (
        "PI1.2 Processing integrity",
        "lets data be processed incompletely or corrupted (memory, arithmetic or race errors)",
        {119, 120, 125, 170, 190, 191, 242, 252, 362, 367, 369, 415, 416, 457, 467, 468,
         476, 676, 787, 789},
    ),
    (
        "CC6.8 Unauthorized or malicious software / CC8.1 Change management",
        "lets untrusted input or an unreviewed component run in the build or delivery pipeline",
        {250, 377, 379, 426, 427, 494, 829, 1104, 1357},
    ),
]
CWE_TO_GROUP = {cwe: g for g in GROUPS for cwe in g[2]}
FALLBACK = (
    "CC7.1 Vulnerability detection",
    "is a vulnerability class this report has not mapped to a more specific control; triage it against the criterion that fits",
)
ACTIONS_FALLBACK = (
    "CC8.1 Change management / CC6.8 Unauthorized or malicious software",
    "weakens the controls that decide what code and automation may change or run",
)
TEST_PATH = re.compile(
    r"(^|/)(tests?/|__tests__/|test_[^/]*\.(c|h|cpp|py|cjs|mjs|js)$|.*\.(test|spec)\.[jt]sx?$)"
)


def cwes(tags):
    return sorted(int(m.group(1)) for t in tags for m in [re.search(r"cwe-0*(\d+)$", t)] if m)


def criterion(rule_id, tags, is_security):
    """(criterion, reason), or None when no SOC 2 criterion applies."""
    if not is_security:
        return None
    # A workflow finding is about the delivery pipeline whatever its CWE says.
    if rule_id.startswith("actions/"):
        return ACTIONS_FALLBACK
    for n in cwes(tags):
        if n in CWE_TO_GROUP:
            g = CWE_TO_GROUP[n]
            return g[0], g[1]
    return FALLBACK


def severity(rule, result):
    props = rule.get("properties", {})
    raw = props.get("security-severity")
    try:
        score = float(raw) if raw is not None else None
    except ValueError:
        score = None
    tags = props.get("tags", [])
    is_security = "security" in tags or score is not None
    level = result.get("level") or rule.get("defaultConfiguration", {}).get("level", "warning")
    if score is not None:
        name = "critical" if score >= 9.0 else "high" if score >= 7.0 else "medium" if score >= 4.0 else "low"
    elif is_security and level == "error":
        name, score = "high", 7.0
    else:
        name = {"error": "major", "warning": "minor", "note": "minor"}.get(level, "minor")
    return name, score, is_security


def quote_lines(root, uri, region, snippet):
    start = region.get("startLine")
    end = region.get("endLine", start)
    if snippet:
        return start, end, snippet.rstrip("\n")
    path = os.path.join(root, uri)
    if start and os.path.isfile(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
        return start, end, "\n".join(lines[start - 1:end])
    return start, end, "(source lines not available in the report)"


def first_sentence(text, limit=1200):
    text = re.sub(r"\s+", " ", text or "").strip()
    return text[:limit] + (" ..." if len(text) > limit else "")


def recommendation(rule):
    text = rule.get("help", {}).get("text", "") or ""
    m = re.search(r"## Recommendation\s*(.+?)(\n## |\Z)", text, re.S)
    return first_sentence(m.group(1)) if m else "Follow the recommendation in the query help linked above."


def all_rules(run):
    """Every rule in the run, by id.

    CodeQL writes the rules of its query packs under `tool.extensions`, not
    `tool.driver.rules`, so both are read. Without that, a result has no
    severity, no security tag and no help text, and nothing would ever fail.
    """
    tool = run.get("tool", {})
    rules = {}
    for component in [tool.get("driver", {})] + tool.get("extensions", []):
        for r in component.get("rules", []) or []:
            rules.setdefault(r["id"], r)
    return rules


def rule_prefixes(path):
    """The query languages in this SARIF (the part of each rule id before the first slash).

    The register covers every language; a job only reports the capacity of entries for the
    languages it analysed, so the C/C++ job does not list the workflow entries as unused.
    """
    with open(path, encoding="utf-8") as f:
        sarif = json.load(f)
    seen = set()
    for run in sarif.get("runs", []):
        for rule_id in all_rules(run):
            seen.add(rule_id.split("/")[0])
        for res in run.get("results", []):
            seen.add((res.get("ruleId") or "?").split("/")[0])
    return seen


def findings(path):
    with open(path, encoding="utf-8") as f:
        sarif = json.load(f)
    for run in sarif.get("runs", []):
        rules = all_rules(run)
        for res in run.get("results", []):
            if res.get("suppressions"):
                continue
            rule_id = res.get("ruleId") or res.get("rule", {}).get("id", "?")
            yield rules.get(rule_id, {"id": rule_id}), res


def flow(res):
    """For a path query, where the flagged data comes from and every step it
    takes to the sink, as "uri:line (what)" strings. Empty for other queries."""
    steps = []
    for code_flow in res.get("codeFlows") or []:
        for thread in code_flow.get("threadFlows") or []:
            for step in thread.get("locations") or []:
                loc = step.get("location", {})
                phys = loc.get("physicalLocation", {})
                uri = phys.get("artifactLocation", {}).get("uri", "?")
                line = phys.get("region", {}).get("startLine", "?")
                what = loc.get("message", {}).get("text", "")
                steps.append(f"{uri}:{line}" + (f" ({what})" if what else ""))
            if steps:
                return steps
    return steps


def render(root, rule, res):
    rule_id = rule.get("id", "?")
    props = rule.get("properties", {})
    tags = props.get("tags", [])
    sev, score, is_security = severity(rule, res)
    loc = (res.get("locations") or [{}])[0].get("physicalLocation", {})
    uri = loc.get("artifactLocation", {}).get("uri", "?")
    region = loc.get("region", {})
    start, end, code = quote_lines(root, uri, region, (loc.get("contextRegion") or region).get("snippet", {}).get("text"))
    mapped = criterion(rule_id, tags, is_security)
    soc2 = (f"SOC 2: {mapped[0]} -- this finding {mapped[1]}." if mapped
            else "SOC 2: none (correctness)")
    cwe_links = [f"https://cwe.mitre.org/data/definitions/{n}.html" for n in cwes(tags)]
    source = rule.get("helpUri") or f"CodeQL query {rule_id}"
    if cwe_links:
        source += " ; " + " ; ".join(cwe_links)
    quote = first_sentence(rule.get("fullDescription", {}).get("text") or rule.get("shortDescription", {}).get("text"))
    where = f"{uri}:{start}" + (f"-{end}" if end and end != start else "")
    score_txt = f" (security-severity {score})" if score is not None else ""
    return sev, uri, "\n".join([
        f"**{sev}: {res.get('message', {}).get('text', rule_id)}**",
        f"Source: CodeQL `{rule_id}` {source}",
        f"Source quote: > {quote} (CodeQL query help, bundled with the analysis run){score_txt}",
        f"Quote (`{where}`):",
        "```",
        code,
        "```",
    ] + ([f"Flow: " + " -> ".join(flow(res))] if flow(res) else []) + [
        soc2,
        f"Fix: {recommendation(rule)}",
        "",
    ])


DISPOSITIONS = ("false-positive", "accepted-risk", "tracked-fix")
REQUIRED = ("id", "rule", "file", "count", "disposition", "justification", "owner", "added", "review_by")
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def load_register(path):
    """The accepted-findings entries, or exit 2 with every problem named. Missing file: none."""
    if not path or not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    problems, seen = [], set()
    entries = data.get("entries", []) if isinstance(data, dict) else None
    if entries is None or data.get("schema") != 1:
        problems.append("the register must be an object with schema 1 and an entries list")
        entries = []
    for i, e in enumerate(entries):
        where = f"entry {i + 1} ({e.get('id', '?') if isinstance(e, dict) else '?'})"
        if not isinstance(e, dict):
            problems.append(f"{where}: not an object")
            continue
        for key in REQUIRED:
            if key not in e or e[key] in ("", None):
                problems.append(f"{where}: missing {key}")
        if e.get("id") in seen:
            problems.append(f"{where}: duplicate id")
        seen.add(e.get("id"))
        if e.get("disposition") not in DISPOSITIONS:
            problems.append(f"{where}: disposition must be one of {', '.join(DISPOSITIONS)}")
        if not isinstance(e.get("count"), int) or e.get("count", 0) < 1:
            problems.append(f"{where}: count must be a whole number of 1 or more")
        for key in ("added", "review_by"):
            if not DATE.match(str(e.get(key, ""))):
                problems.append(f"{where}: {key} must be YYYY-MM-DD")
        if len(str(e.get("justification", ""))) < 20:
            problems.append(f"{where}: justification must say why, in a sentence")
    if problems:
        print("accepted-findings register is invalid:\n- " + "\n- ".join(problems), file=sys.stderr)
        sys.exit(2)
    return entries


class Register:
    """Matches gating findings to entries; an entry covers at most `count` instances."""

    def __init__(self, entries, today):
        self.entries = entries
        self.left = {e["id"]: e["count"] for e in entries}
        self.used = {e["id"]: 0 for e in entries}
        self.expired = [e for e in entries if e["review_by"] < today]
        self.live = [e for e in entries if e["review_by"] >= today]

    def take(self, rule_id, uri):
        for e in self.live:
            if e["rule"] == rule_id and e["file"] == uri and self.left[e["id"]] > 0:
                self.left[e["id"]] -= 1
                self.used[e["id"]] += 1
                return e
        return None

    def unused(self):
        return [e for e in self.live if self.left[e["id"]] > 0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--accepted", default=None,
                    help="accepted-findings register (default: .github/codeql/accepted-findings.json)")
    ap.add_argument("--today", default=None, help="YYYY-MM-DD, for tests")
    ap.add_argument("sarif", nargs="+")
    args = ap.parse_args()
    today = args.today or datetime.date.today().isoformat()
    reg_path = args.accepted or os.path.join(args.repo_root, ".github", "codeql", "accepted-findings.json")
    register = Register(load_register(reg_path), today)

    failing, listed, out, gates, test_lines, accepted = 0, 0, [], [], [], []
    languages = set()
    for path in args.sarif:
        languages |= rule_prefixes(path)
    for path in args.sarif:
        for rule, res in findings(path):
            sev, uri, text = render(args.repo_root, rule, res)
            is_security = severity(rule, res)[2]
            in_tests = bool(TEST_PATH.search(uri))
            gate = sev in ("high", "critical") and is_security and not in_tests
            listed += 1
            entry = register.take(rule.get("id", "?"), uri) if gate else None
            if entry:
                loc = (res.get("locations") or [{}])[0].get("physicalLocation", {})
                line = loc.get("region", {}).get("startLine", "?")
                mapped = criterion(rule.get("id", "?"), rule.get("properties", {}).get("tags", []), True)
                accepted.append(
                    f"- {sev}: {rule.get('id', '?')} at {uri}:{line} -- {entry['disposition']} "
                    f"({entry['id']}, owner {entry['owner']}, review by {entry['review_by']}); "
                    f"SOC 2: {mapped[0] if mapped else 'none'}; {entry['justification']}"
                )
                continue
            failing += gate
            if gate:
                loc = (res.get("locations") or [{}])[0].get("physicalLocation", {})
                line = loc.get("region", {}).get("startLine", "?")
                gates.append(f"- {rule.get('id', '?')} at {uri}:{line}")
            if in_tests and not gate:
                # Test fixtures use made-up secrets on purpose. One line each,
                # so the findings in shipped code stay readable.
                loc = (res.get("locations") or [{}])[0].get("physicalLocation", {})
                line = loc.get("region", {}).get("startLine", "?")
                test_lines.append(f"- {sev}: {rule.get('id', '?')} at {uri}:{line} (test code)")
                continue
            entry = ("GATE " if gate else "") + text
            # Findings that fail the check come first, so they are never lost
            # in a long list of lower-severity findings.
            (out.insert(0, entry) if gate else out.append(entry))

    overdue = [
        f"- {e['id']} ({e['rule']} in {e['file']}) was due for review on {e['review_by']}"
        for e in register.expired
    ]
    failing += len(overdue)
    header = (
        f"CodeQL: {listed} finding(s), {failing} failing the check (high or critical, shipped code, "
        f"or an overdue register entry), {len(accepted)} accepted through the register."
    )
    if gates:
        header += "\nFailing the check:\n" + "\n".join(gates)
    if overdue:
        header += "\nAccepted-findings entries past their review date (re-review them, then move review_by):\n"
        header += "\n".join(overdue)
    body = header + "\n\n" + "\n".join(out)
    if test_lines:
        body += (
            f"\nFindings in test code ({len(test_lines)}), listed but not failing the check:\n"
            + "\n".join(test_lines)
            + "\n"
        )
    if accepted:
        body += (
            f"\nAccepted findings ({len(accepted)}), recorded in the register, not failing the check:\n"
            + "\n".join(accepted)
            + "\n"
        )
    spare = [
        f"- {e['id']} ({e['rule']} in {e['file']}) allows {register.left[e['id']]} more than were found"
        for e in register.unused()
        if e["rule"].split("/")[0] in languages
    ]
    if spare:
        body += (
            "\nRegister entries with unused capacity (tighten count, or delete the entry once fixed):\n"
            + "\n".join(spare)
            + "\n"
        )
    print(body)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("### CodeQL findings (SOC 2 mapped)\n\n" + body + "\n")
    return 1 if failing else 0


if __name__ == "__main__":
    sys.exit(main())

# CodeQL as a SOC 2 control

CodeQL static analysis is a control for vulnerability detection and change management in this
repository. This page says what runs, how a finding is handled, and what evidence exists, so an
auditor can follow a finding from detection to disposition.

## What runs

| Item | Where |
|------|-------|
| Workflow | `.github/workflows/codeql.yml`: pull requests to `develop` and `main`, pushes to them, weekly, and on demand |
| Languages | C and C++ (traced through `make ARCH=x86_64_gas`), JavaScript, Python, GitHub Actions workflows |
| Queries | the `security-and-quality` suite (`.github/codeql/codeql-config.yml`) |
| Report | `.github/scripts/codeql_report.py`: each finding with its source, a quote of that source, the offending lines, its SOC 2 criterion and the fix |
| Gate | the `codeql gate` check, one stable name for all languages; GitHub's own `CodeQL` check adds PR annotations for new alerts |
| Local run | `scripts/codeql_local.sh`: the same analysis and report for the Security seat (exit 2 means NOT RUN) |
| Evidence kept | the full SARIF of every run, 90 days (`codeql-sarif-<language>-<sha>` artifacts), and the GitHub code-scanning alerts |

**Not covered.** CodeQL has no extractor for assembly (GAS `.s`/`.S`, NASM `.asm`) or for shell
scripts, and it cannot judge design or a missing check. Those stay with human review and the
Security seat's SOC 2 walk (`.cursor/skills/devops-multi-agent-team/references/soc2-review.md`).

## How a finding is handled

1. **Detected.** A high or critical security finding in shipped code (not under `tests/` or in
   `test_*` files) fails the `codeql gate` check. Lower severities and findings in tests are listed,
   not gated.
2. **Mapped.** The report assigns a Trust Services Criterion by weakness class, for example a file
   left writable by others is CC6.1, an unvalidated path is CC6.6, an unchecked write or race is PI1.2,
   a workflow that runs untrusted code or poisons a cache is CC8.1 / CC6.8. A class the table does not
   know falls back to CC7.1 and is triaged against the criterion that fits.
3. **Dispositioned.** Either the code is fixed, or the finding is recorded in
   `.github/codeql/accepted-findings.json` with:
   - `rule` and `file`, and `count`, the most instances the entry covers (a further instance is a new
     finding and fails the gate);
   - `disposition`: `false-positive` (the code is safe and why), `accepted-risk` (the behaviour is
     intended and why), or `tracked-fix` (a real weakness with a follow-up);
   - a written `justification`, an `owner`, the date `added`, and a `review_by` date.
4. **Re-reviewed.** Past `review_by` the entry stops covering its finding and the check fails until
   someone reviews it and moves the date (90 days is the cadence used so far).
5. **Changed under the full gate.** The register is under `.github/`, which no fix exemption covers
   (`qa-publish-policy.md`): a change to it takes the Security (SOC 2) review and QA.

Nothing is dismissed silently, and nothing is dismissed in the GitHub Security tab on a machine's
authority: dismissals there, if made, need the same justification and an owner.

## Evidence by criterion

| Criterion | Control | Evidence |
|-----------|---------|----------|
| CC7.1 Vulnerability detection | CodeQL on every PR, push and weekly | workflow runs, SARIF artifacts, code-scanning alerts |
| CC7.2 Monitoring | findings in the audit-relevant classes (permissions, logging, secrets) are mapped and gated | the report's `SOC 2:` line per finding |
| CC8.1 Change management | the workflows themselves are scanned; the register and the gate change only through reviewed commits | `.github/` history, the review thread's `## SOC 2 review` |
| CC6.8 Unauthorized or malicious software | unpinned actions, untrusted checkout and cache poisoning are queried | `actions/*` findings and their dispositions |
| CC3.2 / CC4.1 Risk assessment and monitoring | every accepted finding has an owner and a review date that fails the check when missed | the register, `test_codeql_register.py` |

## What the operator still provides

- Branch protection that requires `codeql gate` (and GitHub's `CodeQL` check) on `develop` and `main`.
- Someone who reads the weekly run and the `Accepted findings` list in the job summary.
- Triage of alerts in the Security tab, with a reason for any dismissal.

## Known limits

- The first adoption found 28 high C/C++ findings and 13 high workflow findings in existing code;
  those are in the register as `tracked-fix`, `accepted-risk` or `false-positive`, each with a
  reason, and none is hidden. Fixing the `tracked-fix` entries is open work.
- A baseline by rule and file is coarser than a per-line baseline: it cannot notice a finding that
  moves within a file, only one that is added.
- The C/C++ analysis builds the x86_64 GAS shell only. The bare-metal, ARM and wasm lab builds, and
  code compiled only under other flags, are not traced.

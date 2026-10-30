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

**Host Wi-Fi tools are compiled out of the analysed build.** The Wi-Fi host layer can run the host's
`wpa_cli`, `nmcli`, `iw` and the Flinstone helper through the host shell. That code is built only with
`make WIFI_HOST_COMMANDS=1` (`FL_WIFI_HOST_COMMANDS`); the default build CodeQL traces has no `popen` or
`system` call in it, so those paths are not analysed and have no register entries. A build made with the
flag has not been scanned: before shipping one, run `WIFI_HOST_COMMANDS=1 scripts/codeql_local.sh --lang c-cpp`
(the script has no option for it; its `make` inherits the exported variable).

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

## Working with CodeQL

**Run it locally** (needs the CodeQL CLI; without it the script exits 2, which means NOT RUN):

```
scripts/codeql_local.sh                 # all four languages
scripts/codeql_local.sh --lang c-cpp    # one language
scripts/codeql_local.sh --dry-run       # print the commands only
```

Databases and SARIF go under `$TMPDIR/codeql-flinstone` (change with `--out`); they are never committed.
`make test_codeql_scripts` tests the report script, the register and the runner without the CLI.

**The gate is red. What now?**
1. Open the failed job's log, or the job summary. Findings that fail the check are listed first, marked
   `GATE`, each with its rule, quoted lines, SOC 2 criterion and fix. The full SARIF is in the
   `codeql-sarif-<language>-<sha>` artifact.
2. Is it in code you changed? Fix it. A fix is the normal answer; the register is for what cannot or
   should not be fixed in this change.
3. If it is a false positive or a deliberate behaviour, add a register entry (below) with a reason a
   reviewer can check against the code.
4. If it is real but belongs to a follow-up, add a `tracked-fix` entry and open the follow-up.
5. "Past their review date" in the header means an entry is overdue: review it, then move `review_by`.

**A register entry** (`.github/codeql/accepted-findings.json`):

```json
{
  "id": "CQ-019",
  "rule": "cpp/world-writable-file-creation",
  "file": "userland/shell/audit_log.c",
  "count": 1,
  "disposition": "tracked-fix",
  "justification": "Why this is safe, intended, or what the follow-up does. At least a sentence.",
  "owner": "BPForbes",
  "added": "2026-10-30",
  "review_by": "2027-01-28",
  "pr": "#388"
}
```

- `count` is the most instances the entry covers; lower it when you fix some, delete the entry when
  you fix all (the report lists entries with unused capacity so the register shrinks).
- `rule` is the CodeQL rule id exactly as the report prints it.
- Pick `review_by` about 90 days out. `false-positive` and `accepted-risk` entries need the strongest
  reasons, since nothing will change in the code.

**Update the pinned actions.** The workflow pins each action to a commit SHA; Dependabot keeps them
current (`.github/dependabot.yml`). A new SHA must come from the action's own repository.

**Why C and C++ are built before analysis.** CodeQL watches the compiler. The workflow builds
KeyQuorum's Rust library first, so only this project's C is traced, then runs `make ARCH=x86_64_gas`
under the tracer. Code that `make` does not compile (other architectures, the wasm lab) is not analysed.

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

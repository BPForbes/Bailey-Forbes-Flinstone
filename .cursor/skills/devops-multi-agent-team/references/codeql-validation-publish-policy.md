# QA bug hunt, Validation, and Publish Policy

This repo is C/C++ with an ASM layer (`make`; CI in
`.github/workflows/c-cpp.yml`). It has no CodeQL workflow, so **in
this repo QA's last gate is a final bug hunt, not CodeQL**. The
CodeQL sections below apply only if a CodeQL workflow is added
later; until then do not report CodeQL as a gate (report it as
`NOT APPLICABLE`, never `PASS`).

## Core rule

**Only QA may give the OK to push.** Review Satisfied, Security
Clear, compilation, tests and linters do not authorize one.

**QA final pass (required before PASS).** After the required make
targets pass, QA looks over the change one last time from two
angles and tries to break it:

- **Test perspective:** read the diff and the tests together. Which
  behaviour is changed but not asserted? Write and run throwaway
  probes (outside the repo or deleted before reporting) for edge
  cases: empty, NULL, oversize, boundary, error returns, repeated
  and concurrent calls, sanitizer builds, old-vs-new differential on
  every changed call site.
- **Live-environment perspective:** run the real thing, not only
  unit tests: build the shipped targets, run the shell or host
  binary through the changed commands end to end (for example the
  KeyQuorum delivery, send and open flow, or the browser lab where
  it can be built), with real files, odd paths, special files and
  failure injection (full disk, missing permissions, a file that
  changes mid-read). Report anything that cannot be run here (for
  example `emcc`) as `NOT RUN`, never as a pass.
- **Bug hunt:** assume the Coder and Reviewers missed something and
  look for it: resource leaks, wrong errno or return mapping, secrets
  left in memory, a caller that relied on the old behaviour, docs or
  wiring that no longer match.

A finding is sent back, not fixed by QA: open
`triage-<id>.md` ([triage-template.md](triage-template.md)) with the
reproducer, hand it to the Coder who owns the change (Research
joins them), and QA re-runs the final pass on the rewrite. PASS only
when the required targets pass, the final pass finds nothing open,
and every `NOT RUN` is named in the report.

## Fast validation (before full CodeQL)

Repo scripts only. Do not invent targets.

- `make` and the test targets listed in `AGENTS.md` (Build targets,
  Test targets, Useful validation sequence).

## Local CodeQL (match GitHub Actions)

Match the workflow query suite. Report if local parity is
impossible. Language-only changes run that language; cross-stack
runs all three. Unaffected targets may be `NOT APPLICABLE` unless
CI requires all three. Mandatory for auth, secrets, crypto, HTTP,
SQL/ORM, uploads, command execution, serialization, XSS/SSRF, and
security configuration.

**C/C++** — create `.codeql-db-cpp --language=cpp --command="make"`;
analyze `codeql/cpp-queries` → `codeql-cpp.sarif`. Inspect the SARIF.
Delete `.codeql-db-*` and SARIF files before reporting.

## Findings

For each finding introduced or materially affected: rule, file,
line; fix; re-run build/tests and CodeQL; inspect SARIF.
**NEW** must resolve; **EXISTING** report only; **MODIFIED /
RE-EXPOSED** resolve when this change contributes. Full CodeQL
after a security-sensitive or substantial set, and always before
the publish gate.

## Publish

**Prohibited** when FAIL, NOT RUN when required, INCOMPLETE,
SARIF not reviewed, a new finding unresolved, or required
build/tests FAIL.

**Allowed:** `make` and tests PASS; CodeQL PASS (or NOT RUN with
the human's agreement); SARIF reviewed; new unresolved = 0;
`git status --short` clean of files you created; exactly one new
`version/entries/*.ver` and no `version/locked/` edits.

## Definition of done

Report build and tests plus C/C++ CodeQL (PASS / FAIL / FINDINGS / NOT RUN / NOT APPLICABLE); new
unresolved N; publish gate PASS / BLOCKED. Never call the change
CodeQL-clean unless analysis ran and SARIF was reviewed. Never
publish while BLOCKED.

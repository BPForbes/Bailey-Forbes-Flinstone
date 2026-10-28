# QA bug hunt, Validation, and Publish Policy

This repo is C/C++ with an ASM layer (`make`; CI in
`.github/workflows/c-cpp.yml`). **There is no CodeQL gate here.**
Security analysis is the Security seat's SOC 2 review
([soc2-review.md](soc2-review.md)); QA's last gate is a final bug
hunt.

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

## Fast validation

Repo scripts only. Do not invent targets.

- `make` and the test targets listed in `AGENTS.md` (Build targets,
  Test targets, Useful validation sequence), with sanitizers where the
  Makefile provides them.

## Findings

For each finding introduced or materially affected: rule, file, line;
fix; re-run build and tests. **NEW** must resolve; **EXISTING** report
only; **MODIFIED / RE-EXPOSED** resolve when this change contributes.

## Publish

**Prohibited** when a required build or test FAILs, the final bug hunt
is INCOMPLETE, a Security SOC 2 blocker or high finding is open, or a
new finding is unresolved.

**Allowed:** `make` and tests PASS; the Security seat's SOC 2 review is
Clear (any waived finding written down with its justification); QA's
final pass finds nothing open; `git status --short` is clean of files
you created; the PR carries exactly one PR-owned `.ver` under
`version/entries/` (including a CI-relocated row under
`version/entries/preproduction */`) and no
`version/locked/` edits.

## Definition of done

Report build and tests, the Security SOC 2 verdict, the final-pass
result, every `NOT RUN`, new unresolved N, and publish gate
PASS / BLOCKED. Never publish while BLOCKED.

# CodeQL, Validation, and Publish Policy

This repo is C/C++ with an ASM layer (`make`; CI in
`.github/workflows/c-cpp.yml`). It has no CodeQL workflow or
`.github/codeql/` config yet. Run CodeQL for `c-cpp` locally when
the CLI is available, and where CI or a reviewer requires it treat
it as a gate before publish. If the CLI is absent, report
CodeQL as NOT RUN (never claim PASS) and let the Orchestrator ask
the human whether the build and tests alone suffice.

## Core rule

**Only QA may give the OK to push.** Anyone who changes product,
pipeline, or infra code must run applicable CodeQL. That developer
run does not authorize a push. Review Satisfied, Security Clear,
compilation, tests, and linters do not substitute for CodeQL.

“CodeQL is satisfied”: applicable database created; analysis
completed; SARIF inspected; no unresolved finding introduced or
materially affected by this change remains; no query, path, or
check was weakened to obtain a pass.

If CodeQL cannot run when required: do not claim it passed and do
not publish. Do not weaken `.github/codeql/codeql-config.yml` /
`.github/workflows/codeql.yml`. Fix the code.

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

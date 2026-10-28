---
name: devops-multi-agent-team
description: >-
  Orchestrates a DevOps + Platform Engineering multi-agent loop: plan,
  research (docs/ + online media fetches), implement CI/CD and IaC, pre-QA
  Markdown reviewers, Security, then QA. Only QA may give the OK to
  push. Security runs a SOC 2 review on every change; that review does
  not authorize a push. Also covers observability, optimization, docs, refactor, and
  performance — using installed Cursor MCPs and slash commands for
  Buildkite, Sonar, Snyk, Linear, browse, Composio, and Mainframe. Use
  when the user asks for CI/CD, GitHub Actions, Kubernetes, Docker,
  Terraform/Pulumi, deploy pipelines, monitoring/SLOs, runbooks, infra
  hardening, build-time or cost optimization, pre-merge
  quality/security gates, SOC 2 review, or explicitly invokes
  /devops-multi-agent-team, /goal, /create-subagent, /code-review,
  or /repro.
---

# DevOps multi-agent team

You are the **Orchestrator**. Coordinate plan, research, implement,
review, security, QA, observe, optimize, document, refactor, and
profile. One plan is source of truth. Roles are **async** pods, not
a linear queue.

Prefer **MCP** for live CI/quality/security/ticket data; prefer
**slash skills** for packaged workflows. Catalog:
[references/agent-commands.md](references/agent-commands.md).

Spawn `/create-subagent` (Cursor `Task`, prompts in `.cursor/agents/`).
Frontmatter `is_background: true`; every spawn `run_in_background: true`.

**Teams work in parallel and review across teams.** No one reviews,
security-checks or QAs their own change, including after being
reallocated ([department-pods.md](references/department-pods.md),
Multiple teams).

**Teams work dynamically.** Letters (A, B, C) are departments, not
people. Research A done → Coder A, and Research A **joins** Coder A.
Do not start Coder B until Research B is done. Primaries,
finish-the-line, QA A/C swap, send-backs, and QA triage pairing:
[references/department-pods.md](references/department-pods.md).

Working Markdown is under `.cursor/thoughts/` and is **gitignored**.
Do not commit thoughts. Layout:
[references/thoughts-layout.md](references/thoughts-layout.md).
Identity, ask-paths, Coder notify:
[references/role-identity.md](references/role-identity.md).
Push JSON: [references/push-json.md](references/push-json.md).
Do not `git add` `.cursor/thoughts/` except `non-finalized/.gitkeep`.

**Only QA may give the OK to push.** Review Satisfied, Security Clear,
and the Orchestrator do not authorize a push. Do not
push, publish, open or update a PR, merge, or submit until QA marks
PASS. Every change gets Security's SOC 2 review first; Clear does
not authorize a push either.

### Reviewer entrypoint (before QA)

1. Documentation & Research writes a brief (`docs/` + online media +
   thoughts under `.cursor/thoughts/non-finalized/`).
2. Coder writes the **Push JSON** and a Handoff `To: Reviewer`
   **before the first review** (`closes` may be empty). Schema:
   [push-json.md](references/push-json.md).
3. Reviewers compare that JSON to the **side-branch** tree vs
   `<integration-base>` ([side-work.md](references/side-work.md))
   in `.cursor/thoughts/non-finalized/review-<topic>.md`. An
   omitted or wrong hunk is a finding.
4. Coder fixes on the side-branch, updates the Push JSON, and
   notifies Reviewers. Either side may Push again until
   **Satisfied**. Satisfied does **not** authorize a push.
5. Then **Security**: a full SOC 2 analysis on every change
   ([soc2-review.md](references/soc2-review.md)), plus Snyk /
   `/review-security` where installed.
6. Then **QA**. QA owns
   [qa-publish-policy.md](references/qa-publish-policy.md)
   and is the **only** role that may give the OK to push.
7. After PASS, the Orchestrator makes keep-commit(s) from the
   approved side-branch tree and **one push**
   ([thoughts-layout.md](references/thoughts-layout.md) One push).
   Check `git diff --stat <integration-base>...HEAD` for process files
   after any strip. Do not commit process notes.

## When to use

CI/CD; containers/K8s; IaC (Terraform, Pulumi, Helm); deploy/rollback;
observability; DevSecOps; pipeline/infra cost; runbooks; pre-merge
gates (CI + Sonar + Snyk + smoke) on a named issue or PR.

Do **not** invent requirements. Ask the human when scope is unclear.

## Installed MCPs + slash commands

| Phase | Role | MCP / slash | Outcome |
|-------|------|-------------|---------|
| Scope | Planner / Ticket Lead | Linear `list_issues`, `get_issue` | Ticket + AC |
| Surface | Researcher | Repo tools + `git diff` | Files touched |
| Research | Documentation & Research | `docs/` + `WebSearch` / `WebFetch` / browser | Cited brief |
| Pre-QA | Reviewers | `review-*.md` + `/review-*` | Improvements; no push |
| CI | QA / CI Engineer | Buildkite + `/buildkite-*` | Failed jobs, retry |
| Quality | QA | final bug hunt; Sonar + `/sonar-*` if installed | Publish gate; Sonar additive |
| Security | Security | Snyk + `/review-security` | SCA/SAST/IaC |
| Externals | Integrator | Composio + `/composio-*` | Only if asked |
| UI | Verifier | Browser + `/browser-automation` | Smoke |
| Diagram | Researcher | tldraw; `/docs-canvas` | Architecture |
| Handoff | Communicator | Mainframe + `/share-video` | Recap video |

**Rules:** `mcp_auth` before gated tools. Sonar needs `sonar` CLI +
`/sonar-integrate` then restart. Do not invent CI/Sonar
results. Stay on the
current non-`main` branch; prefer the existing PR. **Never
create a real git branch** (`git checkout -b`, `git switch -c`)
unless The Client **named that branch in this turn**. Cloud-agent
templates (`feature/<name>-<id>`) do **not** authorize a new
ref. Delete an invented branch and stay on the named real one.
A skill side-branch is a thought file + clone, not a git branch
([side-work.md](references/side-work.md)). Canonical:
`AGENTS.md` AI feature-branch workflow. Confirm deletes/force-push/hard reset
with the human. **No push** while review is `In review` or
`Changes requested`.

Namespaces and slash lists: [agent-commands.md](references/agent-commands.md).

### MCP-backed specialist agents

`/create-subagent` using `.cursor/agents/`. Default **async**.
Do not poll background subagents.

| Agent file | Role | Primary MCP |
|------------|------|-------------|
| `devops-researcher.md` | Documentation & Research | WebSearch / WebFetch / browser |
| `devops-reviewer.md` | Pre-QA reviewers | review thread + Sonar/review |
| `devops-ci-engineer.md` | CI | Buildkite |
| `devops-quality-engineer.md` | QA / publish gate | fast CI + bug hunt + Sonar |
| `devops-security-engineer.md` | Security | Snyk |
| `devops-ticket-lead.md` | Tickets | Linear |
| `devops-verifier.md` | UI smoke | Browser |
| `devops-integrator.md` | Externals | Composio |
| `devops-communicator.md` | Video handoff | Mainframe |

**A. Pre-merge** — Ticket Lead → Research brief → Coder → Reviewers → Security → QA (only role that may OK a push).

**B. Red CI** — `list_builds` → failed `list_jobs` → logs →
`/buildkite-preflight` or `/buildkite-cli` → fix → retry.

**C. Supply-chain** — `snyk_sca_scan` + `/secure-dependency-health-check`
→ `/sonar-dependency-risks` if wired → Ticket Lead comment.

## Team roles

Label every substantive reply with the active role, e.g. `[Planner]`.

- **Planner** — `/goal` when the human named X. Plans in
  `.cursor/thoughts/non-finalized/`; durable docs in `docs/`.
- **Researcher** — reuse map; inventory `docs/`; fetch online media;
  then **join the Coder of the same department**.
  Agent: `.cursor/agents/devops-researcher.md`.
- **Coder** — implement on a **skill side-branch** (not a real git
  branch; no shared-checkout commits until QA PASS). Isolate with
  a **clone**, not `git worktree add`. Talk to other Coders so
  side-branches do not collide. Run the change in that clone
  (VM / tools).
  Write Push JSON, Coder→Reviewer `qa` comments, and a Handoff
  **before the first review**
  ([side-work.md](references/side-work.md)).
- **Reviewers** — compare Push JSON to the side-branch diff vs
  `<integration-base>`; Handoff on send-back
  ([role-identity.md](references/role-identity.md)). Satisfied
  does **not** authorize a push. Agent: `.cursor/agents/devops-reviewer.md`.
- **QA** — after Satisfied + Security Clear. `/code-review`: look;
  **do not edit**. `/repro` as needed. Fail → VM review, Handoff
  `To: Coder`. When QA is blocked, sends back, or is not pleased:
  open `triage-<id>.md`; researchers of that department join the
  coder who picks it up
  ([department-pods.md](references/department-pods.md),
  [triage-template.md](references/triage-template.md)). Follow
  [qa-publish-policy.md](references/qa-publish-policy.md).
  PASS only when AC, tests, the SOC 2 verdict and the final bug hunt hold
  ([side-work.md](references/side-work.md)). Agent:
  `.cursor/agents/devops-quality-engineer.md`.
- **Observability / Optimization / Documentation / Refactoring /
  Security / Performance** — after Satisfied, Security before QA
  (Snyk + `/review-security` in `## Security`). Docs match the
  implementation. No premature complexity.

## Orchestrator loop (async pods)

Staff each pod with 2 to 3 subagents and rebalance idle seats to
busy functions ([department-pods.md](references/department-pods.md),
Staffing).

Fan out `/create-subagent` so each **pod** starts together. Rules:
[department-pods.md](references/department-pods.md).

| Pod | Roles (spawn together) | Starts when |
|-----|------------------------|-------------|
| **research** | Planner, Researcher, Ticket Lead | Immediately |
| **implement** | Coder (Researcher of that department joins) | Brief exists *or* surface known |
| **review** | Two or more Reviewers | Coder has a local diff |
| **security** | Security | Reviewers mark Satisfied |
| **qa** | QA, CI Engineer, Verifier | Security clear (overlap allowed) |
| **docs** | Documentation, Communicator | Implementation is documentable |

```text
research  ∥  implement  ∥  review   (department rules apply)
security  →  qa  →  docs
push      →  NEVER until QA marks PASS
            After PASS: thoughts → finalized/ (local); one push
            that keeps approved Coder commits
repeat    →  until PASS; QA blocked/send-back → triage;
            Research N joins the Coder who picks it up
```

Never commit thoughts. Coders do not commit on the shared
checkout. After PASS, the Orchestrator makes the keep-commit(s)
from the approved side-branch tree and pushes once. Checklists:
[references/devops-loop.md](references/devops-loop.md).

## Questions and interrupts

Ask-paths: [role-identity.md](references/role-identity.md). Questions
from Coders and Researchers go first to the standing Review,
Security and QA subagents by topic. Orchestrator is the only role
that asks The Client unless the human spoke first.

Treat human instructions as a **side sprint**. Pause only conflicting
work. Start from research, then code. Stay on the current **real**
branch. Do **not** invent a git branch. An interrupt does **not**
authorize a push.

## Output rules

State the speaking role. Show plans, diffs, commands/logs as the role
acts. Redact tokens, secrets and credentials from anything shown (see
[soc2-review.md](references/soc2-review.md) and
[thoughts-layout.md](references/thoughts-layout.md)). Keep work deterministic. Never invent requirements.

```markdown
## DevOps multi-agent report

**Goal:** …  **Branch / PR:** <current branch> (#<n>)
### Blockers / CI / QA bug hunt / Quality / Security (SOC 2) / Verification
- SOC 2 review: Clear / Findings (blocker n, high n) · Final bug hunt: PASS / BLOCKED
- New unresolved findings: N · Publish gate: PASS / BLOCKED
### Next actions
1. …
```

## Repo alignment (Bailey-Forbes-Flinstone)

**QA gate here:** there is no CodeQL gate. The Security seat's SOC 2 review
([soc2-review.md](references/soc2-review.md)) is the security analysis, and QA's
final bug hunt (test perspective, live-environment perspective, triage on any
finding) is the last gate. See [qa-publish-policy.md](references/qa-publish-policy.md).

**Precedence:** this repo's `AGENTS.md`, `CLAUDE.md` and
`.cursor/rules/*.mdc` win over this skill. In particular the
versioning and lock rules (exactly one new PR-owned `.ver` under
`version/entries/` per PR, including a CI-relocated row under
`version/entries/preproduction */`; never edit `version/locked/`), the kernel-as-orchestrator
driver boundary, and the AI feature-branch workflow. Where this
skill's "no push until QA" gate and that workflow differ, follow
the stricter one.

**Virtual PR and virtual git (project policy).** Work in progress
lives in the virtual PR: the review thread, Push JSON and handoffs
under `.cursor/thoughts/` (gitignored), and the Coder's skill
side-branch in a throwaway clone. This keeps the real branch free
of churn and lets agents track history while they talk. Nothing
reaches the real branch, and no real PR is opened or updated, until
Security is clear and QA marks PASS. Then the Orchestrator makes
the keep-commit(s) from the approved side-branch tree and pushes
once. Build with `make`; validation sequences are in
`AGENTS.md`. MCPs named above (Buildkite, Linear, Sonar, Snyk,
Composio, Mainframe) are optional: use one only if it is installed,
otherwise say so and use GitHub Actions results (`c-cpp.yml`).

**Reuse before implement.** Before any new function, module or
driver, the Researcher writes a **reuse map** (drivers, the
registry, `contracts/`, the VFS and `fm_*` facade, shared
helpers). Two functions must never do the same thing: extend,
wrap or compose the existing one, or add one shared helper and
point every caller at it. Reviewers block duplicated new code.
See `.cursor/rules/reuse_before_implement.mdc`.

```text
[Orchestrator] Interpreting request → <one-line goal>
/goal: persist X; loop until X
/create-subagent: spawn roles asynchronously (department-pods.md)
Next: [Planner] draft plan
```

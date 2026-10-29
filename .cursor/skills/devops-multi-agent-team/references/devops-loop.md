# DevOps development loop — detailed checklists

Use with the orchestrator loop. Persist `/goal`, review threads, and
`/repro` notes under `.cursor/thoughts/non-finalized/` (local; do
not commit). After QA PASS, move closed thoughts to `finalized/`
(still local). See [thoughts-layout.md](thoughts-layout.md),
[role-identity.md](role-identity.md), and
[department-pods.md](department-pods.md). Spawn roles
asynchronously **in pods**. Commands:
[agent-commands.md](agent-commands.md).

## 1. Planner

Goal and non-goals; environments; components; CI/CD stages and
gates; deploy + rollback; observability; security constraints;
testable acceptance criteria; open questions for the human.

## 2. Researcher / Architect

Inventory pipelines, compose/k8s, IaC, scripts, and docs. Prefer
reuse. Cite why a new tool is needed. Produce: target architecture;
reuse map; risks and rollback. When the brief is done, **join the
Coder of the same department**. On QA triage, join the Coder who
picks the item up ([department-pods.md](department-pods.md)).

## 3. Coder

Implement only what the plan authorizes. **Cut a skill
side-branch** (never a real git branch unless The Client named
that branch in this turn; cloud-agent templates do not
override this); isolate in your own **detached git worktree** off the integration SHA
(no ref, never pushed; stage by explicit path); claim shared files on
`team-board.md` and talk to the owning Coder so paths do not collide
([side-work.md](side-work.md)). Do not commit on the shared
checkout. Keep secrets out of git. Prefer idempotent,
non-interactive scripts. Security's SOC 2 review comes before QA
— and does not authorize a push. Write `push-<seat-id>.json` and
any Coder→Reviewer `qa`
rows **before the first review**. Ask Researcher for a reuse
map before duplicating code.

## 3b. Documentation & Research (online media)

Inventory `docs/` and open thoughts first. Do not dump research
into `docs/`. Fetch online media as needed. Write a brief into the
review thread.

## 3c. Reviewers (entrypoint before QA)

PR-style review. Compare Push JSON to the side-branch tree vs
`<integration-base>` ([side-work.md](side-work.md),
[push-json.md](push-json.md)). Cite research brief, reuse map,
`docs/`, and fetched URLs. Duplicated code → request an import.
Iterate until Satisfied. Satisfied does **not** authorize a
push. Primaries: [department-pods.md](department-pods.md).
Template: [review-thread-template.md](review-thread-template.md).

## 4. Security (after Satisfied)

Snyk / secret scan / `/review-security` on the change surface.
Record verdict in the review thread. Security Clear does **not**
authorize a push.

## 5. QA

Owns [qa-publish-policy.md](qa-publish-policy.md).
`/code-review`: inspect; **do not edit**. `/repro` when needed.
Minimum: repo-appropriate fast validation; the Security seat's SOC 2 verdict; workflow / chart / Terraform lint; secret scan if in CI;
smoke; rollback notes; `git diff --stat <integration-base>...HEAD` shows no process files; exact commands and exit codes; Definition of
Done. **Only QA may give the OK to push.** Fail → Handoff
`To: Coder` from a **VM** review. Open `triage-<seat-id>.md` when
blocked / sent back / not pleased; the department's Researcher joins the Coder
who picks it up ([triage-template.md](triage-template.md)). After
PASS, list thoughts to finalize. Orchestrator cherry-picks the
approved side-branch SHAs, a QA seat that is not an author of the integrated tree re-approves it (authors: the union of the Handoff `Authors:` and the Push JSON `authors` of every cherry-picked SHA, the Orchestrator's conflict fixes included; a seat is refused when its seat id or handle is in that set), then the Orchestrator pushes only after the human authorizes that push.
(Exception to "Only QA may give the OK to push": review-bot feedback fixes outside the exceptions, see qa-publish-policy.md.)

## 6–11. Later passes

- **Optimization** — what dominates wall-clock; parallel jobs;
  oversized non-prod; measurable proposals.
- **Observability** — golden signals or RED/USE; log labels;
  symptom alerts + runbook links; SLI/SLO only if asked.
- **Documentation** — deploy / promote, rollback, failures,
  ownership; match final paths.
- **Refactoring** — naming with existing `deploy/` / workflow
  conventions; extract via existing patterns; no behavior change
  unless the plan says so.
- **Security (detail)** — privileged containers; unpinned
  actions/images; secrets in logs; public endpoints without auth;
  over-broad CI `contents: write`.
- **Performance** — before/after job durations, image size / pull
  time, deploy reconcile, hot app paths only if in scope.

## Interrupt routing

Human interrupts start a **side sprint from research**. Ask-paths:
[role-identity.md](role-identity.md). Stop → halt; leave plan/code
consistent; do not push. Push / open PR → blocked until **QA gives
the OK**.

```text
[Orchestrator] Status
- Done: …  · In progress: … ([Role])
- Blocked / questions: …  · Next: …
```

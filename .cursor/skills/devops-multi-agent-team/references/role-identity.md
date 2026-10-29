# Role identity, questions, and handoffs

Every DevOps role **reads** this file and
[department-pods.md](department-pods.md). Do not paste either
into an agent prompt.

## Ask paths

Anyone may ask anyone when blocked. **Questions go to the standing
Review, Security and QA subagents first**, by topic, before the
Orchestrator or the human:

| Topic | Ask |
|-------|-----|
| Correctness, reuse, style, scope, whether code duplicates something | **Reviewer** seat |
| Secrets, input and path validation, containment, crypto, untrusted data, key handling | **Security** seat |
| Tests, build, CI, the publish gate, what QA will accept | **QA** seat |

| Who has a question | Asks first | Then |
|--------------------|------------|------|
| Coder, Researcher | the Reviewer, Security or QA seat for that topic (SendMessage) | Orchestrator |
| Coder (path overlap) | **the other Coder** | Orchestrator |
| Reviewer, Security, QA | the **Coder** (primary), or each other | Orchestrator |
| CI / Verifier / Ticket Lead | Orchestrator or task opener | none |
| Orchestrator (Team Lead) | the Review, Security and QA seats, then **The Client** (human) | none |

The three seats answer in writing (`## Q&A` in the review thread or a
SendMessage reply) and say plainly when they do not know. An answer is
advice: only QA may OK a push (except review-bot feedback fixes outside the exceptions: references/qa-publish-policy.md), and a seat's answer never authorizes one.

Orchestrator is the only role that asks the human unless the
human spoke first. Coder ↔ Reviewer use thread `## Q&A` and
Push JSON `qa` (same ids).

**Independence.** No role reviews, secures or QAs a change it
authored, merged or fixed, even after reallocation; see
[department-pods.md](department-pods.md), Multiple teams. Every
Handoff to a reviewer names `Authors:`.

## Interrupt, goals, Handoff

Pause only conflicting work. Start from research, then code.
Stay on the current **real** branch; Coders use a skill
**side-branch** ([side-work.md](side-work.md)). **Never**
`git checkout -b` / `git switch -c` unless The Client **named
that branch in this turn**. Cloud-agent templates do not
override this. An interrupt does **not** authorize a push. Write `goal-<role>-<topic>.md` under
`non-finalized/`; move to `finalized/` after QA. See
[thoughts-layout.md](thoughts-layout.md).

```markdown
## Handoff
- From / To / Authors / Pass-along
- Sent back because: <reason, or "n/a">
- Ask: <question, or "n/a">
```

A send-back without **Sent back because** is incomplete.

## Coder notify + QA triage

Coder writes `push-<topic>.json` and a Handoff `To: Reviewer`
**before the first review** ([push-json.md](push-json.md)).
Coders may open `qa` rows to Reviewers for clarification.
Update on every rewrite. Reviewers compare it to the
side-branch diff vs `<integration-base>` ([side-work.md](side-work.md)).
Satisfied does **not** authorize a git push.

Quality or bug-standard fail → VM review, Handoff `To: Coder`.
QA blocked / send-back / not pleased → `triage-<id>.md`;
Research *N* **joins the Coder who picks it up**
([department-pods.md](department-pods.md),
[triage-template.md](triage-template.md)). Same Q&A + `qa` ids.
`"files": {}` if the tree is unchanged.

Process output never lands. Product, pipeline, infra, tests,
and durable `docs/` land as keep-commits. Probe undo: [thoughts-layout.md](thoughts-layout.md).

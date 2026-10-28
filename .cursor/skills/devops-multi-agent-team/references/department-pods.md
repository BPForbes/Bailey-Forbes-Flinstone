# Department pods

Letters are **departments / subject matters**, not people. Teams work
**dynamically**: research can run with coding; coding can run with
review. Do not run roles as a linear 1→N queue.

Read this file. Do not paste it into agent prompts.

| Dept | Owns | Coder seat |
|------|------|------------|
| **A** | skill `SKILL.md`, `references/*`, `.cursor/agents/*` | Coder A |
| **B** | `AGENTS.md`, `CLAUDE.md`, PR description text | Coder B |

## Research → Coder

- Research **A** done → Coder **A**. Research A **joins Coder A**.
- Research **B** not done → **do not** run Coder B.
- Same for every letter: no Coder until that department’s brief
  exists (or the Orchestrator already knows the surface).

## Reviewer primary

Coder B submits → Reviewer A and Reviewer B may both review it.
If Coder A then submits, Reviewer A **prioritizes** Coder A.
If Coder B is still writing and Coder A has not submitted,
Reviewer A reviews other ready code.

Cross-review until a coder of the **same department** submits.
Each reviewer has a primary. If they cannot review that goal,
they review other ready code.

## Finish the line, then pass notes

Mid-review, if a primary arrives: finish the current **line of a
file**, then pass notes to the reviewer whose primary it is.
The Orchestrator records the handoff. Do not swap silently.

## QA primary

If nothing matches a QA’s primary, they test other ready items.
Mid-test, if their primary becomes ready: finish the current
assessment, pass notes, then switch.

## Send-back (review)

**a.** One reviewer on their primary → coder rewrites. That reviewer
joins other topics and **receives context + existing feedback**
before commenting.

**b.** Several reviewers in one session → **label which reviewer**
left each finding. On send-back, priority may shift with goals.

## Send-back (QA triage)

When QA is **blocked**, **sends back**, or is **not pleased**: open
`triage-<id>.md` from [triage-template.md](triage-template.md).
Handoff `To: Coder` from a **VM** review
([role-identity.md](role-identity.md)). Do not wait for a serial
research-then-coder queue.

The Coder who picks up the item owns the rewrite. Research *N* of
that department **joins that Coder immediately** and stays paired
until Reviewers take the rewrite. Same-department Research/Coder
letter rule still applies to *new* work; triage is the exception
that pairs them at once.

Prioritize the assigned item when available. Pass findings when
helping on another item.

## Orchestrator duties

- Spawn `/create-subagent` (`Task`, `run_in_background: true`) so
  pods overlap. Do not poll background subagents.
- Do not start Coder *N* before Research *N* is done (new work).
- Record primaries, finish-the-line handoffs, and QA swaps.
- Gates: no push while review is open; Security before publish;
  **only QA may give the OK to push.** Coders cut a skill
  side-branch (clone, not worktree, **not** a new git branch);
  no shared commits until QA PASS. Record overlapping **files
  owned** so Coders talk before they collide
  ([side-work.md](side-work.md)). Never create a git branch
  unless The Client named that branch in this turn. Cloud-agent
  templates do not override this.

## Staffing (parallel seats, rebalanced)

Run every function at once, each with **2 to 3 subagents**: research,
coding, review, QA, security, and docs. A seat is a background
subagent (`run_in_background: true`); do not poll it.

- The Orchestrator watches the queues and **moves idle seats to the
  function with a backlog**. Example: when research has nothing
  left, move its seats to coding or review; when coding is waiting
  on a brief, move coders to research.
- Keep at least one seat on any function that has ready work, and
  at most three per function. Total seats follow the work, not a
  fixed headcount.
- Moves never break the rules above: a moved seat takes the
  department it joins (research seats join the Coder of that
  department), Reviewers still prioritize their primary, and QA
  stays the only role that may OK a push.
- Seats share the remote's two concurrent git smart-HTTP
  operations: fetch and clone one at a time, and work in local
  side clones.

## Standing advisory seats

Keep one Review, one Security and one QA subagent available from the
start of a task, not only after a diff exists. Before code: they
review the plan (design, threat model, test plan). While Coders work:
they answer questions ([role-identity.md](role-identity.md) Ask
paths). After a Coder hands off: they review that diff. They count
toward the 2 to 3 seats of their function and are the first to be
resumed (SendMessage) rather than respawned.

## Multiple teams, independent review

Treat every task as **several teams working the problem at the same
time**, as if they were separate companies sharing one repo: each
team (A, B, C, ...) has its own research, coding, review, QA and
security seats and its own side clone, and teams meet only through
written handoffs, the review thread and Push JSON.

**No one reviews their own work.** The rule follows the *person*,
not the seat:

- A seat that wrote, edited, merged or fixed any part of a change
  (Coder, or a Researcher who joined a Coder and touched code,
  tests or docs of that change) may not review, security-check,
  QA or approve that change, in any role, ever, even after being
  reallocated to review, security or QA.
- Reallocation moves a seat to *other* work. A seat moved from
  coding to review reviews only changes it did not author or amend;
  a seat that gave design advice on a plan may review the diff but
  says so in the handoff.
- Reviewer, Security and QA must come from a team other than the
  author's, and the three must be three different seats. The
  Orchestrator does not review code it wrote or merged either: it
  merges patches and routes, and the standing seats judge.
- The Orchestrator records authorship in the handoff (`Authors:`
  seat ids) and checks it before sending anything to a reviewer. A
  review by an author is void and is redone by another seat.
- Questions to a standing seat ([role-identity.md](role-identity.md))
  are fine. Asking a seat to *judge* its own change is not.


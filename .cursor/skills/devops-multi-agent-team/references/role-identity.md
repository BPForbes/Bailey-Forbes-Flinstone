# Role identity, questions, and handoffs

Every DevOps role **reads** this file and
[department-pods.md](department-pods.md). Do not paste either
into an agent prompt.

## Seat ids

Every subagent seat (Coder, Researcher, Reviewer, Security, QA,
Prober, Verifier, ...) is told apart by a **seat id**, never by a
capital letter or a number. This section is the one definition;
other files point here.

- **Form:** `<role>-<6 hex chars>`, e.g. `coder-0a0a0a`,
  `reviewer-0b0b0b`, `security-0d0d0d`, `qa-0e0e0e`.
- **Minted by the Orchestrator** when it spawns the seat. It is
  unique within the PR, never reused, and never derived from the
  seat's work. Generate one, then check it against every note and
  the ledger before use:

  ```text
  id="<role>-$(od -An -N3 -tx1 /dev/urandom | tr -d ' \n')"
  grep -rF "$id" .cursor/thoughts   # must print nothing
  ```

  This is the only documented generator. A topic is never
  interpolated into a shell command; it is only appended to a file
  name as a suffix that matches `[a-z0-9-]+`. The check is a
  `case` on the whole value, so a newline cannot slip through (a
  line-based `grep` would pass `a`+newline+`b`), run under
  `LC_ALL=C` so it is the same in every locale. It is a function
  with a subshell body, safe to paste into an interactive shell
  (it returns, it never exits):

  ```text
  check_suffix() ( LC_ALL=C; case "$1" in ''|*[!a-z0-9-]*) return 1 ;; esac; )
  check_suffix "$suffix" || echo reject
  ```

  The value is passed as data, never inside a command string. On a
  hit of the id grep, mint
  again. The grep covers file
  contents in `non-finalized/` and `finalized/`, not only file names.
  The Orchestrator records each new id in `seat-registry.md`
  (`non-finalized/`, a fixed name, not `<kind>-<seat-id>`): one row
  per seat with id, role, the label or topic it was spawned for, its
  handle (the agent or session identity the host reports), and (if replaced) the id that replaced it. Ids stay in the
  ledger after the seat ends, which is what makes "never reused"
  checkable.
- **Ids are labels, not credentials.** One agent could appear under
  two ids, so the registry records each seat's handle, and a handle
  is never reused for a second id in the same PR. The independence
  check compares handles as well as ids: a seat whose handle matches
  an author's handle is not independent, whatever its id.
- **Used in:** the spawn description, the Handoff `Authors:` and
  `Reviewer:` lines, file names `<kind>-<seat-id>.md` (for example
  `handoff-coder-0a0a0a.md`, `review-reviewer-0b0b0b.md`,
  `security-<seat-id>.md`, `qa-<seat-id>.md`, `triage-<seat-id>.md`,
  `push-<seat-id>.json`; add `-<topic>` when a seat writes several
  of a kind, or one item per file when it works several), review-thread sign-off rows, Q&A `from`/`to`, and Push
  JSON `seat`, `authors` and `qa` rows ([push-json.md](push-json.md)).
- **`Authors:`** lists every seat that wrote, amended, merged or
  fixed any part of the change (a seat that fixes a review finding
  is added). The independence check reads the **union** of the
  Handoff `Authors:` line and the Push JSON `authors`; an id in
  either excludes that seat from reviewing, security-checking or
  QAing the change.
- **Standing seats** (Reviewer, Security, QA) keep **one id for the
  PR**. They are resumed (SendMessage), not re-minted, unless
  replaced; a replacement gets a new id and the Handoff says whom
  it replaces.
- **Capital letters never name a seat, team or author.** Examples
  and prompts say a role plus its id.

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

**Independence.** No seat reviews, security-checks or QAs a change
whose `Authors:` (the union above) lists its id: it authored,
amended, merged or fixed it. The check is by id (and handle, see Seat ids), and a reallocated
seat keeps its id; see
[department-pods.md](department-pods.md), Multiple teams. Every
Handoff to a reviewer names `Authors:` (seat ids).

## Interrupt, goals, Handoff

Pause only conflicting work. Start from research, then code.
Stay on the current **real** branch; Coders use a skill
**side-branch** ([side-work.md](side-work.md)). **Never**
`git checkout -b` / `git switch -c` unless The Client **named
that branch in this turn**. Cloud-agent templates do not
override this. An interrupt does **not** authorize a push. Write `goal-<seat-id>.md` (your id) under
`non-finalized/`; move to `finalized/` after QA. See
[thoughts-layout.md](thoughts-layout.md).

```markdown
## Handoff
- From / To / Authors / Pass-along (seat ids, or a role when no seat is chosen yet)
- Sent back because: <reason, or "n/a">
- Ask: <question, or "n/a">
```

A send-back without **Sent back because** is incomplete.

## Coder notify + QA triage

Coder writes `push-<seat-id>.json` and a Handoff `To: Reviewer`
**before the first review** ([push-json.md](push-json.md)).
Coders may open `qa` rows to Reviewers for clarification.
Update on every rewrite. Reviewers compare it to the
side-branch diff vs `<integration-base>` ([side-work.md](side-work.md)).
Satisfied does **not** authorize a git push.

Quality or bug-standard fail → VM review, Handoff `To: Coder`.
QA blocked / send-back / not pleased → `triage-<seat-id>.md`;
the department's Researcher **joins the Coder who picks it up**
([department-pods.md](department-pods.md),
[triage-template.md](triage-template.md)). Same Q&A + `qa` ids.
`"files": {}` if the tree is unchanged.

Process output never lands. Product, pipeline, infra, tests,
and durable `docs/` land as keep-commits. Probe undo: [thoughts-layout.md](thoughts-layout.md).

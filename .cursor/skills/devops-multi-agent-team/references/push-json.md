# Push JSON (notification index, not the review)

Coder and Reviewer bounce a **Push JSON** until both are happy.
Companion to the review thread and Handoff — not a replacement.
Reviewers **always** compare it to the side-branch tree vs
`<integration-base>` ([side-work.md](side-work.md)). An omitted
or wrong hunk is a finding. Do not mark Satisfied from the JSON
alone.

`.cursor/thoughts/non-finalized/` is **gitignored**. Do not
`git add` Push JSON.

| File | Who | When |
|------|-----|------|
| `push-<seat-id>.json` | the Coder seat | Latest mock (`push-<seat-id>-<topic>.json` when the seat works several items). Overwrite in place. Either side may update it; `seat` names who last did. |
| `push-<seat-id>-r<N>.json` | optional | Keep a round if bounce needs history. |

## First handoff (required)

- Coder writes `push-<seat-id>.json` **before the first review**,
  with a Handoff `To: Reviewer`. `closes` may be `[]`.
- Reviewers do not start without that file. Update it on every
  rewrite. Either side may Push when they have something new.
- Open `qa` rows must be answered or withdrawn before Satisfied.
  Coders may open rows (`from`: the Coder's seat id) to ask Reviewers for
  clarification. Satisfied does **not** authorize a git push.

## Q&A

Same ids in the thread `## Q&A` table and the JSON `qa` array.
`status` is `open`, `answered`, or `withdrawn`. A Q&A-only bounce
may set `"files": {}`. Do not invent a commit to record an answer.
Each `qa` row: `id`, `from`, `to`, `ask`, `answer`, `status`;
`from` and `to` are seat ids ([role-identity.md](role-identity.md),
Seat ids), or a role when the addressee is not chosen yet.

## Shape

Valid JSON. File paths are object keys. Hunks are
`{ op, lines, why }`. Each file ends with `delta` `+N/-M`.
`op` is `-` (old), `+` (new), or `~` (same span, behavior change).
`lines` is a single line or `start-end`. Index the side-branch
tree vs `<integration-base>`, not `git show HEAD` alone.

Top-level keys: `topic`, `round`, `seat` (id of the seat that
wrote this version), `authors` (seat ids of everyone who wrote
the change), `to` (seat id or role), `notifies` (seat ids or
roles), `closes`, `qa`, `files`. Example head:
`"seat": "coder-0a0a0a", "authors": ["coder-0a0a0a"],
"to": "reviewer-0b0b0b"`. Reviewers use `authors`, unioned with the Handoff `Authors:`
line ([role-identity.md](role-identity.md), Seat ids), for the
independence check. `seat` replaces the old `from`. Hunks index the side-branch
working tree vs `<integration-base>` when there is no shared
commit yet ([side-work.md](side-work.md)). Example hunk:
`{ "op": "+", "lines": "12-20", "why": "…" }` then `"delta": "+9/-2"`.

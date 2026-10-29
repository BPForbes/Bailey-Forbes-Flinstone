# Side-work and comments

**Rule: one detached git worktree per Coder.** Owner confirmed
per-Coder detached worktrees as the rule in a session answer on
2026-10-29. A throwaway `git clone --no-hardlinks` stays acceptable
for throwaway probes only (as before). To revert the rule, change this
paragraph and the matching AGENTS.md line (the clone text is in git
history, a236151).

Coders do not put commits on the shared checkout until **QA PASS**.
The Orchestrator then integrates the approved SHAs by cherry-pick, and the Orchestrator pushes only after the human authorizes that push
([thoughts-layout.md](thoughts-layout.md)).

## Side-branch (skill-tracked, backed by a detached worktree)

A **side-branch** is a name the skill tracks. It is **not** a git
ref and it is **not** pushed. **Never** create a pushed or PR git
branch unless The Client **named that branch in this turn**.
Cloud-agent `feature/*-<id>` templates do **not** override this.
Isolation is a **detached git worktree** off the integration SHA:
no branch or ref is created, so there is nothing to push, collide
on or delete. The Coder's work is handed over **by commit SHA**.

1. Write `side-<dept>.md` under the shared checkout's
   `.cursor/thoughts/non-finalized/` (absolute path; thoughts are
   gitignored and absent from a fresh worktree, so Coders write and
   claim there, never inside the worktree): `name`
   (`side/<dept>-<topic>`, a label only), `worktree` (the path
   below), `base` (the integration **SHA**), **files owned**.
2. The **Orchestrator** creates each worktree (a Coder only at its
   direct instruction). `<dept>`, `<topic>` and `<seat-id>` must pass
   `check_suffix` ([role-identity.md](role-identity.md), Seat ids:
   only `[a-z0-9-]`), be at most 40 characters and not start with
   `-`; text copied from an issue, ticket or comment is never put in
   a path, ref or command. Quote every argument and make the
   directory with `mktemp -d` (mode 0700), never a predictable
   `/tmp` name:

   ```text
   case "$integration_sha" in ''|*[!0-9a-f]*) exit 1;; esac
   [ "${#integration_sha}" -eq 40 ] || exit 1      # exactly 40 hex
   for v in "$dept" "$topic" "$seat"; do
     check_suffix "$v" && [ "${#v}" -le 40 ] && [ "${v#-}" = "$v" ] || exit 1
   done
   wt=$(mktemp -d /tmp/side-XXXXXXXX) || exit 1
   git worktree add --detach -- "$wt/tree" "$integration_sha" || exit 1
   mkdir "$wt/hooks"; printf '#!/bin/sh\nexit 1\n' > "$wt/hooks/pre-push"
   chmod 700 "$wt/hooks/pre-push"
   # Writes the SHARED .git/config (extensions.worktreeConfig=true, and
   # git may set core.repositoryformatversion=1). Do this first, once,
   # Orchestrator only, then take the spawn snapshot below.
   git config extensions.worktreeConfig true
   git -C "$wt/tree" config --worktree push.default nothing
   git -C "$wt/tree" config --worktree remote.pushDefault no_such_remote
   git -C "$wt/tree" config --worktree core.hooksPath "$wt/hooks"
   git -C "$wt/tree" config --worktree --replace-all credential.helper ''
   git -C "$wt/tree" config --worktree --add credential.helper '!echo credential helper refused >&2; exit 1'
   ```

   `push.default` and `remote.pushDefault`, and the pre-push hook, are
   accidental-push protection only: a bare `git push` fails, and an
   explicit `git push origin HEAD:x` is stopped by the hook. The
   worktree `credential.helper` is set with `git config --worktree`.
   The empty value drops helpers from other config; the next value
   exits 1 and writes a refusal to stderr. It stores no token, key,
   or credential. `git -c` and `--no-verify` still bypass the helper
   and the hook (`git -c` overrides the helper and `core.hooksPath`;
   `--no-verify` skips the hook). A Coder spawn forbids both, and
   that forbid is the tool-layer gate. A separate OS account or
   credential principal for Coder seats is an owner decision and is
   not created here. The check which catches a bypass is the
   pre-integration `git ls-remote --heads origin` comparison, and a
   difference stops integration. The settings are the Orchestrator's;
   a Coder never changes them. After the
   `extensions.worktreeConfig` line, record at spawn the shared
   `.git/config` hash, the hooks listing, `git for-each-ref`, `git
   stash list`, the shared checkout's `git rev-parse HEAD` and `git
   status --short`, and `git ls-remote --heads origin` (read-only; it counts toward the
   remote's two-operation limit, so the Orchestrator runs it serially),
   for the verification below.
3. Commit **only** in that worktree, on the detached HEAD. Hand over
   by **commit SHA** in the Handoff `Pass-along`. Never commit on the
   shared checkout. The Orchestrator integrates by cherry-picking the
   approved SHAs onto the local PR branch (department-pods.md, Team
   board), then removes the worktree.
4. **Staging.** Stage by explicit path only (`git add <path>`); never
   `git add -A`, `git add .`, `git commit -a` or `git stash`. Never
   stage `third_party/keyquorum` (a local symlink that shows as a
   type change), any `.ver` file or `userland/shell/version_def.h`.
   Before each commit, after `git add` and before the commit, run
   `git diff --cached --name-only` and `git diff --cached --numstat`.
   Confirm the name-only list is only owned files. Put both outputs
   in the Handoff.
5. **A named ref.** There is no per-seat branch. If a Coder needs a
   named ref it asks the Orchestrator, who may create a local-only
   `local/<item>-<seat-id>-<n>` (`<n>` counts up so two refs never
   collide), never pushed, never a PR branch, deleted after
   integration. Nothing else is a branch.

### What a worktree shares

A clone has its own refs, config, hooks, stash and object store and
no path to the real remote's refs. A worktree shares all of them
with the shared checkout, so these are rules, not conventions:

- **Refs and stash.** A Coder never runs `git push`, `git fetch`,
  `git stash`, `git branch`, `git tag`, `git update-ref`,
  `git reset` outside its own detached HEAD, `git remote` or
  `git config` without `--worktree`. The stash stack and refs are
  shared (`.cursor/commands/{code-review,repro}.md` already say so).
- **Config and hooks.** A Coder never edits `.git/config`,
  `.git/hooks`, `config.worktree` or `core.hooksPath`, never passes
  `--no-verify`, and never uses `git -c <key>=<value>` (any `-c`
  override) on any git command.
- **gc and objects.** A Coder never runs `git gc`, `git prune`,
  `git reflog expire`, `git worktree remove|prune|move` or deletes a
  ref. The worktree's HEAD reflog keeps its commits reachable until
  the Orchestrator has them.
- A throwaway **clone** (`git clone --no-hardlinks . /tmp/probe`)
  stays acceptable for Reviewer, Security and QA throwaway probes.

### Orchestrator verification (before integration)

Before cherry-picking an item the Orchestrator checks, and records
in the Handoff, that: `git for-each-ref` shows no ref added or moved
since spawn, other than the local PR branch the Orchestrator itself
advanced (and none under `refs/remotes` named `local/*`); `git
branch -vv` shows no upstream; the shared `.git/config` hash, the
hooks listing, `git stash list`, the shared checkout's HEAD and
`git status --short` match the spawn record; `git ls-remote --heads
origin` equals the spawn snapshot (no new or moved remote ref; this
comparison is the check that catches a bypass of the helper or the
hook, and a difference stops integration); the worktree's settings
above (`push.default`, `remote.pushDefault`, `core.hooksPath`, and
`credential.helper`) are unchanged; `git diff
--name-only <base>..<sha>` lists only the files owned, none of
`third_party/keyquorum`, `.ver` or `version_def.h`; and the Handoff
`git diff --cached --name-only` outputs agree. Any difference stops
integration and goes to the human.

## Coder <-> Coder (prevent merge conflicts)

Parallel worktrees must not collide when the Orchestrator
integrates. The shared mechanism is the append-only team board
(department-pods.md, Team board and cross-team communication).

- Before editing, read `team-board.md` and every other `side-*.md`
  **files owned** list. **Append a claim row** (seat | item |
  worktree | files and Makefile sections | status) before touching a
  shared file (`Makefile`, `scripts/run_all_tests.sh`, `.gitignore`,
  shared headers).
- If a path is claimed, **talk to the owning Coder** (SendMessage,
  Handoff `To: <coder-id>`, or Push JSON `qa` with `to`:
  `<coder-id>`). Record the handshake in both `side-*.md` files: who
  takes the path, who waits, or how hunks are split. Do not silently
  overwrite.
- Keep Makefile edits as small, separate hunks.
- Prefer department ownership ([department-pods.md](department-pods.md)).
  Cross-department files need an Orchestrator note.
- Before integration, Coders confirm no uncoordinated overlapping
  hunks. If two worktrees touched the same path, the owners resolve it
  in one worktree first; the Orchestrator then cherry-picks in the
  declared merge order, re-runs tests after each, and a QA seat not in
  `Authors:` re-approves the integrated tree. A Coder rebases onto the
  integration tip only when the Orchestrator hands it the new SHA.

## Run the change

In the side **worktree**, use a VM, `computerUse`, `dotnet` / `npm` /
`cargo`, and `/repro` as needed. The shared worktree stays free
for other departments. Probe undo:
[thoughts-layout.md](thoughts-layout.md).

## Coder → Reviewer comments

Coders may open review-thread `## Q&A` rows and Push JSON `qa`
(`from`: `Coder`, `to`: `Reviewer`) to ask for clarification
before or during review. Same ids. Open rows block Satisfied.

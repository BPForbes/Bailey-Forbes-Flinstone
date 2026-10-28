---
name: code-review
description: Look at the change; do not edit. Write findings to .cursor/thoughts/non-finalized/. Primary owner is QA.
---

# /code-review

Also: `/review-bugbot`, `review the diff`, `look but don't edit`.

**Look at the change. Do not edit.**

Primary owner: **QA** (`.cursor/agents/devops-quality-engineer.md`). Reviewers may use the same inspect-only bar.

1. Confirm the Coder Push JSON exists (required before the first
   review). Read it as an index, then **always** read the
   side-branch tree vs `<integration-base>` (see
   `.cursor/skills/devops-multi-agent-team/references/side-work.md`),
   plus tests, logs, and SARIF. Compare the JSON `files` and
   `delta`s to that name list and `--numstat`, not to
   `git show HEAD` alone. An omitted or wrong hunk is a finding.
   Do not mark the review done from the JSON alone. Confirm
   `cr-<topic>.md` on a code change; open CR findings block.
2. Scan the diff for duplicated new code first: a new function,
   module or driver that repeats existing behavior is a **blocking**
   finding. Record file and line and the existing code to reuse or
   extend. Do not mark a review done while one remains.
3. Write findings into `.cursor/thoughts/non-finalized/review-<topic>.md`.
   Line-level feedback may also go in an uncommitted `push-<topic>.json`.
4. **Do not edit** product code, workflows, or docs to "fix" findings while acting as `/code-review`. Hand remediations to the Coder.
5. Prefer proving a finding in a throwaway clone
   (`git clone --no-hardlinks . /tmp/probe`). A probe that must sit in
   this worktree takes a reserved lower-case name — a `_scratch/`
   directory or a `.scratch` infix — and is **deleted** before you
   report; a probe that *edited* a tracked file is undone with
   `git checkout -- <exact path>`, never `git checkout -- .` or
   `git stash`, because the worktree is shared. Finish with
   `git status --short` clean **of files you created**, and list
   anything else by path rather than removing it. Reviewer process
   output never lands on the committed timeline;
   `scripts/check-clean-timeline.sh` enforces it in CI.
6. Do not push. **Only QA may give the OK to push.** Review findings
   and passing tests do not authorize a push.
7. Use any installed `/` skill that fits (`/review-bugbot`, `/review-security`, `/sonar-analyze`).

Catalog: `.cursor/skills/devops-multi-agent-team/references/agent-commands.md`.

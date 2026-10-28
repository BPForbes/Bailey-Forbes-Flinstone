---
name: triage
description: QA opens or updates a triage item for a bug or discovery during command execution.
---

# /triage

Also: `open triage`, `track this bug`, `QA triage`.

**Owner: QA** (`.cursor/agents/devops-quality-engineer.md`).

1. Copy `.cursor/skills/devops-multi-agent-team/references/triage-template.md`
   to `.cursor/thoughts/non-finalized/triage-<id>.md`.
2. Fill **What went wrong**, **Expected**, **Actual**, environment,
   and the command that discovered it. Set **State:** `active`.
3. Use **Q&A** on that file (same table + Push JSON `qa` as the
   review thread). Either side may ask. If there is no tree
   change, leave Push JSON `files` as `{}` and do not commit.
4. Handoff `To: Coder` with **Sent back because**. The Orchestrator
   starts research → coder → reviewer → QA for that id.
5. Do not `git add` the file. Do not push. Any probe the triage run
   creates is process output too: throwaway clone, or a reserved
   lower-case name (`_scratch/`, `.scratch`), deleted before you report.

Catalog: `.cursor/skills/devops-multi-agent-team/references/agent-commands.md`.

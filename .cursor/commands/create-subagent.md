---
name: create-subagent
description: Spawn a DevOps role asynchronously from .cursor/agents/. Do not poll background subagents.
---

# /create-subagent

Also: `create a subagent`, `spawn`, Cursor `Task`.

Spawn specialized roles. They run **asynchronously in pods**, not a linear queue.

1. Use Cursor `Task` with the matching prompt in `.cursor/agents/devops-*.md`.
2. Default `run_in_background: true` and agent `is_background: true`.
   Launch a whole pod in one turn (research, review, qa, …). Each role
   type is async.
3. Do not poll a background subagent. Continue other work or end the turn; the completion notification is enough.
4. The Orchestrator synthesizes. Subagents do not push or open PRs.
   **Only QA may give the OK to push.** Coders / primary developers must
   run applicable CodeQL on their own changes; that run does not
   authorize a push. DO NOT PUSH, PUBLISH, OPEN OR UPDATE A PULL REQUEST,
   MERGE, OR OTHERWISE SUBMIT CODE UNTIL QA MARKS THE PUBLISH GATE PASS.
5. Subagents accept any `/` command or the same words (`/goal`, `/code-review`, `/repro`, `/buildkite-*`, `/sonar-*`, and so on).
6. Working Markdown goes in `.cursor/thoughts/non-finalized/` while open.
   Agent files set `is_background: true`. Always spawn with
   `run_in_background: true`.

| Role | Agent file |
|------|------------|
| Researcher | `devops-researcher.md` |
| Reviewer | `devops-reviewer.md` |
| CI Engineer | `devops-ci-engineer.md` |
| QA | `devops-quality-engineer.md` |
| Security | `devops-security-engineer.md` |
| Ticket Lead | `devops-ticket-lead.md` |
| Verifier | `devops-verifier.md` |
| Integrator | `devops-integrator.md` |
| Communicator | `devops-communicator.md` |

Catalog: `.cursor/skills/devops-multi-agent-team/references/agent-commands.md`.

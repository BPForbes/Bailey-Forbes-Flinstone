---
name: devops
description: Start the DevOps multi-agent team as Orchestrator on a goal (plan, research, implement, review, Security, QA). Only QA may give the OK to push.
argument-hint: <goal>
---

# /devops

Also: `/devops-multi-agent-team`, `run the devops team on X`.

Goal: $ARGUMENTS

(In Cursor the goal is the text typed after `/devops`; `$ARGUMENTS`
is filled in by Claude Code. No goal given: ask The Client for one.)

1. Load the skill, and follow it as the **Orchestrator**:
   - MCP server `devops-multi-agent-team` connected: call its
     `read_doc` tool with `{"kind": "skill"}` (or use its `devops`
     prompt), then read references with `read_doc` as the skill links
     them, and mint seat ids with `mint_seat_id`.
   - Not connected: read
     `.cursor/skills/devops-multi-agent-team/SKILL.md` and the
     `references/` files it links.
2. Open with the skill's banner:
   `[Orchestrator] Interpreting request → <one-line goal>`, then
   `/goal` to persist it and `/create-subagent` to spawn the pods.
3. `AGENTS.md`, `CLAUDE.md` and `.cursor/rules/*.mdc` win over the
   skill. **Only QA may give the OK to push**, and the Orchestrator
   pushes only after the human authorizes that push.

Catalog: `.cursor/skills/devops-multi-agent-team/references/agent-commands.md`.

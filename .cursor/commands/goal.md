---
name: goal
description: Do until you achieve X. Persist the objective and keep looping until it is met.
---

# /goal

Also: `set a goal`, `do until X`, `keep going until X`.

Do until you achieve **X**. Do not stop at a plan or a partial implement.

1. Write the objective to `.cursor/thoughts/non-finalized/goal-<topic>.md`
   (acceptance criteria, non-goals, done-when). Each role may also write
   `goal-<role>-<topic>.md`. Do not `git add` those files. After QA
   PASS, move closed goals to `.cursor/thoughts/finalized/` (local).
2. If this is a long-running human-invoked goal, also use Cursor `CreateGoal` / `UpdateGoal`.
3. The Orchestrator (`/devops-multi-agent-team`) loops the DevOps cycle until X is actually achieved.
4. Spawn roles with `/create-subagent` **asynchronously**. Subagents accept the same `/` command or plain wording.
5. Mark the local goal file (and `UpdateGoal`) complete only when the criteria are met, or the human stops the goal.
6. **Only QA may give the OK to push.** Coders must still run applicable
   CodeQL on their own changes. DO NOT PUSH, PUBLISH, OPEN OR UPDATE A
   PULL REQUEST, MERGE, OR OTHERWISE SUBMIT CODE UNTIL QA MARKS THE
   PUBLISH GATE PASS.

Catalog: `.cursor/skills/devops-multi-agent-team/references/agent-commands.md`.

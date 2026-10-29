# QA triage item

Copy to `.cursor/thoughts/non-finalized/triage-<seat-id>.md`. Same
Handoff + **Q&A** as the review thread. Copy ids into Push JSON
`qa`. `"files"` may be `{}`. Do not `git add`. After QA PASS,
move to `finalized/`.

When QA is **blocked**, **sends back**, or is **not pleased**, QA
opens this file. The department's Researcher **joins the Coder
who picks the item up** and stays until Reviewers take the rewrite
([department-pods.md](department-pods.md)). Not a serial
research-then-coder queue.

```markdown
# Triage: <qa-seat-id>[-<n>]

**State:** active | closed
**Opened by:** <qa-seat-id>
**Discovered during:** <command, `/repro`, VM, SOC 2 review, CI>
**Branch:** <current checkout>
**Review thread:** `.cursor/thoughts/non-finalized/review-<seat-id>.md`

## What went wrong / Expected / Actual
- …

## Environment
- VM / host; commands; exit code

## Active loop
- [ ] Researcher (<researcher-id>) joins Coder (<coder-id>) who picked this up (paired, not serial)
- [ ] Coder rewrite + Push JSON
- [ ] Reviewers compared JSON to the side-branch vs `<integration-base>`
- [ ] Security (after Satisfied)
- [ ] QA re-check on the VM

## Q&A
| Id | From | To | Ask | Answer | Status |
|----|------|----|-----|--------|--------|
| t1 | <qa-id> | <coder-id> | … | | open |

## Handoff
- From: <qa-id> / To: <coder-id> / Authors: <coder-id> / Pass-along /
  Sent back because: <quality / bug standard, or "n/a">
```

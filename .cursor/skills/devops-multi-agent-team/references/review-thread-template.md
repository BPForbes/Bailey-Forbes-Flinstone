# Review thread template

Copy to `.cursor/thoughts/non-finalized/review-<seat-id>.md`.
Local only; do not commit. After QA PASS, move to `finalized/`.
See [thoughts-layout.md](thoughts-layout.md).

```markdown
# Review: <topic>

**Branch:** <shared checkout; do not invent a real git branch>
**Side-branch:** `side/<dept>-<topic>` (`side-<dept>.md`)
**Authors:** <coder-id>[, <researcher-id>]   **Reviewers:** <reviewer-id>[, ...]
**Status:** In review | Changes requested | Satisfied — ready for security | Closed
**Push policy:** Only QA may give the OK to push (review-bot feedback fixes: qa-publish-policy.md).

## Research brief
### Local docs / Online media / Recommendations
- … · | URL | Takeaway |

## Push JSON
- Latest: `.cursor/thoughts/non-finalized/push-<seat-id>.json`
- The Coder (`seat`) writes this **before the first review**. Reviewers compare
  it to the side-branch tree vs `<integration-base>`.

## Change summary (Coder)
- Files / Intent / Notify (Handoff + Push JSON path)

## Review round N (Reviewers)
### Request changes
- [ ] `path` — finding (cite research/doc/URL; label the reviewer id)
### Questions / Suggestions (non-blocking)
- …
### Reviewer sign-off
| Reviewer | Verdict | Notes |
|----------|---------|-------|
| <reviewer-id> | Changes requested / Satisfied | … |

## Q&A (Coder ↔ Reviewer)
| Id | From | To | Ask | Answer | Status |
|----|------|----|-----|--------|--------|
| q1 | <reviewer-id> | <coder-id> | … | | open |
| q2 | <coder-id> | <reviewer-id> | … | | open |

## Coder response (round N)
- Replies + what changed; q-ids closed

## Handoff
- From / To / Pass-along / Sent back because / Ask

## Security (after Satisfied; not for review-bot feedback fixes without an exception, see qa-publish-policy.md)
- Seat: <security-id> · Verdict: Clear / Blocked

## QA handoff
- Seat: <qa-id> · Commands; build and tests; SOC 2 verdict; clean diff (`git diff --stat <base>...HEAD`); publish gate
  PASS / BLOCKED; VM send-back; triage ids; thoughts to finalize
```

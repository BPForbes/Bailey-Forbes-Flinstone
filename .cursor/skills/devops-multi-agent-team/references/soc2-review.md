# SOC 2 security review

Security runs this on **every change** (except review-bot feedback
fixes, see [qa-publish-policy.md](qa-publish-policy.md)) before QA, not only on
"security" changes. Review as if an auditor will read it. The result
is a `## SOC 2 review` section in the review thread (or
`security-<topic>.md`), then Clear or Findings.

## Rules

- Read the code, the diff, and the callers. Verify every claim in the
  current code; do not repeat a finding already fixed.
- No author reviews their own change ([department-pods.md](department-pods.md)).
- Do not weaken a check to pass. A finding that is only a nit is still
  reported, marked minor.
- No secrets, keys, tokens or passphrases in a finding, log or test.

## Every finding carries five parts

1. **Source** - why it is a violation: a rule in `AGENTS.md` /
   `CLAUDE.md` (name the section), a file and line in this repo, or an
   official external source (RFC and section, NIST SP number, CWE or
   CVE id, the AICPA Trust Services Criteria, vendor or language
   documentation). Prefer primary sources. Never "best practice" alone.
2. **Source quote** - the sentence that states the requirement, copied
   verbatim, with its URL or file and line and the date read. If the
   source cannot be retrieved write `source not retrieved` and do not
   rate the finding above minor.
3. **Quote** - the exact offending lines from the diff, with file path
   and line numbers. Do not paraphrase code.
4. **SOC 2** - the criterion (id and name, table below) and one
   sentence on how the change weakens it. If none applies write
   `SOC 2: none (correctness)` and mark it minor.
5. **Fix** - the concrete change, as a diff or replacement lines.

```
**<severity>: <title>**
Source: ...
Source quote: > ... (<url or file:line>, read <date>)
Quote (`path:line`): ```c ... ```
SOC 2: <id> <name> - ...
Fix: ...
```

Severity: **blocker** (secret exposure, bypassed or broken signature,
quorum, custody or approval check, plaintext at rest, authentication
bypass, write outside the intended directory), **high** (a control
missing or weakened, data loss, replay or freshness gap),
**major** (wrong behaviour, missing test for changed behaviour, docs
that contradict code), **minor** (style, naming, comments).

## Criteria to walk for every change

For each row write Met / Gap (finding id) / Not applicable, with the
evidence (file:line, test, command output) that supports it.

| Criterion | Look for |
|-----------|----------|
| CC6.1 Logical access | A check skipped, widened or decided by the caller; scope or identity binding removed; a trust decision taken from a cache or a name the sender chose |
| CC6.2 / CC6.3 Provisioning and removal | Revocation or rotation that leaves old access working |
| CC6.6 System boundaries | New input from the network, a file or a letter without validation, size or time limits; names and paths from an untrusted peer; path traversal and symlinks |
| CC6.7 Transmission and removal of information | Plaintext, keys or passphrases written or sent outside the sealed or owner-only path; secrets left in memory, logs or errors |
| CC6.8 Unauthorized or malicious software | New dependencies, build steps or scripts without audit coverage; code that runs untrusted input |
| CC7.1 Vulnerability detection | Changes that bypass or weaken scanning, sanitizers or tests |
| CC7.2 Monitoring | Security-relevant events (denials, failures to get entropy, tamper) swallowed silently; records that hold secrets |
| CC7.3 / CC7.4 Incident response | Failures that hide their cause, no way to recover or revoke |
| CC8.1 Change management | Behaviour change without a test, wire-format or code numbers renumbered, docs or agent files not updated, CI weakened, more than one `.ver` |
| CC9.1 Risk and vendors | New external services or data flows undocumented |
| C1.1 / C1.2 Confidentiality | Secrets in logs, errors, tests or fixtures; missing wipe; no deletion path for expired data |
| A1.2 Availability and recovery | Unbounded reads or loops, missing size limits or timeouts, retry loops that never end |
| PI1.2 - PI1.5 Processing integrity | Validation, ordering, freshness or idempotence removed; partial writes with no rollback; unchecked return values |

Also record, for the change as a whole: input surfaces it touches,
trust boundaries it crosses, what fails closed and what fails open,
what was **not** verified (for example a build that could not run),
and the residual risk.

## Output

```markdown
## SOC 2 review
Scope: <diff, commit, files>   Authors: <seat ids>   Reviewer: <seat id>
| Criterion | Status | Evidence |
|-----------|--------|----------|
| CC6.1 ... | Met | file:line, test |
### Findings
<five-part findings, most severe first, or "none">
### Not verified / residual risk
...
Verdict: Clear | Findings (blockers: n, high: n)
```

A blocker or high finding sends the change back through
`triage-<id>.md` to the Coder; the review is repeated on the rewrite.
Clear is advice: only QA may OK a push (review-bot feedback fixes:
see qa-publish-policy.md).

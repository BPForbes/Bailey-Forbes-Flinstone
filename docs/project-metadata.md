# Public project metadata (`project-metadata.json`)

Flintstone publishes a small, machine-readable description of itself alongside
the validated browser lab. The portfolio at **bailey-forbes.com** consumes that
file instead of maintaining duplicate static values, so **this repository stays
the source of truth** for language percentages, development timeline, named
releases, build state, and the deployed source revision.

- Generator: [`scripts/generate_project_metadata.py`](../scripts/generate_project_metadata.py)
- Curated named releases: [`metadata/releases.json`](../metadata/releases.json)
- Tests: [`tests/test_generate_project_metadata.py`](../tests/test_generate_project_metadata.py)
- Integration point: [`.github/workflows/browser-kernel-artifact.yml`](../.github/workflows/browser-kernel-artifact.yml)
- Published path: `dist/browser-lab/project-metadata.json`

## Public endpoint

The file is written **into the existing Pages payload** (`dist/browser-lab`)
before `actions/upload-pages-artifact` runs, so it ships with the same atomic
deployment as the browser lab:

```
https://bpforbes.github.io/Bailey-Forbes-Flinstone/project-metadata.json
```

The repository publishes a GitHub Pages *project* site (there is no `CNAME` in
`tools/browser-lab/`), which is why the path carries the
`/Bailey-Forbes-Flinstone/` prefix — the same prefix
`infra/cloudflare/flintstone-lab-worker.js` uses as its `BASE_PATH`. The
Cloudflare child Worker therefore also serves it at:

```
https://flintstone.bailey-forbes.com/project-metadata.json
```

`project-metadata.json` is listed in the Worker's no-store set next to
`build-info.json` and `browser-validation.json`, so consumers never read a
stale revision.

## Deployment flow

Metadata generation is a step inside the **existing** browser-kernel workflow.
It does not add a second Pages deployment and cannot bypass the promotion gate:

```
Merge into main
      ↓
browser-kernel-artifact.yml
      ↓  kernel build + driver/VM/gate tests
      ↓  virtual boot probe (boot_smoke_passed)
      ↓  Wasm lab build, browser boot, packaged-lab and iframe validation
      ↓  GitHub metadata generation   ← scripts/generate_project_metadata.py
      ↓  verify metadata is inside dist/browser-lab
      ↓  Stage Pages payload (dist/browser-lab)
      ↓
GitHub Pages → bailey-forbes.com
```

If metadata generation fails, the step fails, the job fails, and **nothing is
deployed**. Malformed metadata is never published: the generator validates the
document, re-parses the exact bytes it is about to write, and only then swaps
the file into place with `os.replace`, so a refused write leaves any previous
valid file untouched.

## Where each field comes from

| Section | Source |
| --- | --- |
| `languages` | GitHub REST `GET /repos/{owner}/{repo}/languages` — the Linguist-derived classification GitHub itself computes, including `.gitattributes` overrides and vendored/generated handling |
| `timeline` (`pull_request`) | `GET /repos/{owner}/{repo}/pulls?state=closed&base=<default branch>`, filtered to `merged_at` |
| `timeline` (`release`) | `GET /repos/{owner}/{repo}/releases`, published non-draft entries only |
| `repository` | `GET /repos/{owner}/{repo}` |
| authorship | PR author, PR commits, and standard `Co-authored-by:` trailers |
| `sourceCommit` | `GITHUB_SHA` — the commit the deployed kernel/lab was built from |
| `build` | `dist/build-info.json`, `dist/browser-validation.json`, and the workflow's boot-smoke step result |
| `releases` | [`metadata/releases.json`](../metadata/releases.json) — curated, not derived |

Nothing is hard-coded. A new language appearing in the repository shows up
without any generator change, because no whitelist is applied.

### Percentages

Percentages are derived from the raw Linguist byte counts:

```
percentage = round(bytes * 100 / total_bytes, 1)
```

Raw `bytes` are preserved so consumers can re-derive or re-round. Entries are
sorted largest-to-smallest, with ties broken by name for determinism.

### Timeline selection

- Only **merged** pull requests are eligible; open and closed-but-unmerged PRs
  never appear, and individual commits are not timeline entries.
- Entries are ordered **newest-first** with a deterministic tie-break, and
  capped at `--max-timeline` (default 25).
- Releases become `release` entries when actual GitHub Releases exist. Zero
  releases is a supported state — release entries are simply omitted, and
  nothing is fabricated from `version/*.ver` files.

### Named releases (`releases`)

`timeline` above is comprehensive and automatic: every merged pull request and
every published GitHub Release, in full. That is the right shape for "what
changed lately" but the wrong shape for a portfolio, which wants a handful of
milestones a visitor can actually read — and Git history is too granular and
too inconsistent for a stranger to guess which of a few hundred merges matter.

So `releases` is the opposite of `timeline` in one respect: **it is curated,
not derived.** The generator does not infer a named release from commits,
tags, or the `version/*.ver` train under `version/locked/` — it only ever
republishes what a human wrote in [`metadata/releases.json`](../metadata/releases.json):

```json
{
  "releases": [
    {
      "version": "4.0.0 / 4.0.1",
      "startDate": "2026-05-18",
      "endDate": "2026-05-19",
      "summary": "Contracts · IPC/VFS · serial-j1",
      "description": "Inheritable system contracts; then IPC/VFS/shell hardening and serial -j1 default builds."
    }
  ]
}
```

A repository with no such file, or an empty `releases` array in it, publishes
zero named releases — that is a supported state, not an error. `version`,
`startDate`, `endDate` (nullable), `summary`, and `description` are the only
fields a curator writes; the generator computes the rest:

- **`id`** — a stable slug derived from `version` (`"4.0.0 / 4.0.1"` →
  `"4-0-0-4-0-1"`), so nothing but the version string itself has to be kept in
  sync when a release is renamed.
- **`url`** — defaults to `{repository.url}/tree/{sourceCommit}/version/locked`,
  the exact validated commit's locked-version directory, so "View release"
  always points at real, reviewable source. A curator may set `url` on a
  specific entry to link somewhere more precise instead.

`releases` is ordered newest-first by `startDate` and validated the same way
`timeline` is: malformed dates, an `endDate` before its `startDate`, a
duplicated `version`, or a non-`github.com` `url` all fail the generator
outright — nothing partial is published. An email address or credential
anywhere in a curated `summary` or `description` is redacted the same way an
upstream pull request title would be.

### Bot-assisted and co-authored work

A PR is **not** excluded merely because an agent (Cursor, Claude, Codex,
Copilot, GitHub Actions, …) created or pushed the branch. The only PRs dropped
are bot-authored *routine dependency chores* (`dependabot`, `renovate`,
`build(deps): bump …`) with no human association at all.

Authorship is published as:

```json
{ "author": "agent-or-primary-author", "coAuthors": ["BPForbes"] }
```

`coAuthors` combines the GitHub accounts on the PR's commits with the names in
`Co-authored-by:` trailers. Bot identities from commit metadata (for example
`github-actions[bot]` version-lock commits) are treated as noise and skipped;
an explicit trailer is always honoured.

**Email addresses are never published.** A trailer's address is used only to
resolve a public `…@users.noreply.github.com` login; anything else falls back
to the display name. The generator refuses to write a document in which any
string matches an email or credential pattern.

### Build and lab state

The generator **never re-derives compatibility**. It republishes what the
existing validation pipeline already decided, and refuses to run if
`dist/build-info.json` was produced from a different commit than the
deployment's `GITHUB_SHA`:

- `build.bootable` ← `dist/build-info.json` `bootable`
- `build.browserCompatible` ← `dist/build-info.json` `browserCompatible`
  (falling back to `v86Compatible` on schema-1 manifests)
- `build.bootSmokePassed` ← the workflow's QEMU boot-probe step output
- `build.browserValidation` ← a compact subset of `dist/browser-validation.json`

`lab.published` is `true` only when every promotion gate passed *and* this run
is the `main` push that deploys Pages. The lab is described honestly as an
emulated demonstration: `executionModel: "emulated"` and
`physicalHardwareDrivers: false`. No physical hardware-driver execution in the
browser is ever claimed.

## Schema (`schemaVersion: 1`)

Common fields (`schemaVersion`, `generatedAt`, `sourceCommit`, `repository`,
`languages`, `timeline`) are intended to be reusable across portfolio projects;
`build` and `lab` are Flintstone-specific extensions. Every timeline entry
carries a common `date` field so one consumer can render mixed entry types.

```json
{
  "schemaVersion": 1,
  "generatedAt": "2026-09-17T12:00:00Z",
  "sourceCommit": "<40-hex deployed build commit>",
  "repository": {
    "owner": "BPForbes",
    "name": "Bailey-Forbes-Flinstone",
    "defaultBranch": "main",
    "url": "https://github.com/BPForbes/Bailey-Forbes-Flinstone",
    "description": "<optional>"
  },
  "languages": [
    { "name": "C", "bytes": 2792382, "percentage": 79.0 }
  ],
  "build": {
    "sourceCommit": "<same as top-level sourceCommit>",
    "bootable": true,
    "browserCompatible": true,
    "bootSmokePassed": true,
    "bootSuccessMarkerImplemented": true,
    "validationOutcome": "browser-bootable",
    "architecture": "x86_64",
    "artifactFormat": "raw BIOS disk image",
    "artifactSha256": "<64-hex>",
    "builtAt": "2026-09-17T11:00:00Z",
    "blockers": [],
    "browserEmulator": "QEMU Wasm x86_64 (…)",
    "browserValidation": {
      "browser": "Chromium 140",
      "testedAt": "2026-09-17T11:30:00Z",
      "checks": ["exact-marker", "vga-text-memory"],
      "runtime": "ktock/qemu-wasm",
      "runtimeCommit": "<40-hex>"
    }
  },
  "lab": {
    "type": "browser-kernel",
    "architecture": "x86_64",
    "executionModel": "emulated",
    "physicalHardwareDrivers": false,
    "published": true
  },
  "timeline": [
    {
      "type": "pull_request",
      "date": "2026-09-15T17:45:54Z",
      "number": 359,
      "title": "feat(lab): Emscripten Flinstone Shell sandbox",
      "mergedAt": "2026-09-15T17:45:54Z",
      "url": "https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/359",
      "author": "BPForbes",
      "coAuthors": ["cursoragent"]
    },
    {
      "type": "release",
      "date": "2026-09-15T00:00:00Z",
      "tag": "v4.5.4",
      "title": "Flintstone Kernel v4.5.4",
      "publishedAt": "2026-09-15T00:00:00Z",
      "url": "https://github.com/BPForbes/Bailey-Forbes-Flinstone/releases/tag/v4.5.4",
      "prerelease": false
    }
  ],
  "releases": [
    {
      "id": "4-0-0-4-0-1",
      "version": "4.0.0 / 4.0.1",
      "startDate": "2026-05-18",
      "endDate": "2026-05-19",
      "summary": "Contracts · IPC/VFS · serial-j1",
      "description": "Inheritable system contracts; then IPC/VFS/shell hardening and serial -j1 default builds.",
      "url": "https://github.com/BPForbes/Bailey-Forbes-Flinstone/tree/<sourceCommit>/version/locked"
    }
  ]
}
```

Additive fields may appear without a `schemaVersion` bump; consumers should
ignore unknown keys. A breaking change bumps `schemaVersion`.

## Consuming it from bailey-forbes.com

GitHub Pages serves `Access-Control-Allow-Origin: *`, so no proxy or token is
needed, and the portfolio must never make authenticated GitHub API calls from
the browser:

```js
const metadata = await fetch(
  "https://bpforbes.github.io/Bailey-Forbes-Flinstone/project-metadata.json"
).then((response) => response.json());

const languages = metadata.languages
  .map((language) => `${language.name} ${language.percentage.toFixed(1)}%`);

const milestones = metadata.timeline.filter(
  (entry) => entry.type === "pull_request" || entry.type === "release"
);
```

The portfolio should **not** independently maintain language percentages, PR
history, named-release copy, source-commit information, or browser-validation
state. `releases` is presentation-ready: a consumer formats `startDate`/
`endDate` into whatever date string its design calls for and renders
`summary`/`description`/`url` as given, but it should not decide *which*
releases are portfolio-worthy — that editorial judgment belongs here, in
`metadata/releases.json`, not in the portfolio's own code.

## Permissions

The `validate` job runs with the minimum needed:

```yaml
permissions:
  contents: read        # repository, languages, releases, commits
  pull-requests: read   # merged pull requests for the timeline
```

Authentication uses the workflow's `GITHUB_TOKEN`. No Personal Access Token is
required, no token is printed, and no token reaches the published JSON.

## Running it locally

```sh
FL_BOOT_SMOKE_PASSED=true \
GITHUB_REPOSITORY=BPForbes/Bailey-Forbes-Flinstone \
GITHUB_SHA=$(git rev-parse HEAD) \
make project-metadata
```

Useful flags: `--max-timeline`, `--max-pull-pages`, `--skip-commit-authors`
(skips per-PR commit lookups, so no co-author trailers are resolved),
`--output`, and `--publish-context` (set by CI on the `main` push that deploys
Pages). Unauthenticated runs work but are subject to GitHub's 60-requests/hour
limit; export `GITHUB_TOKEN` for a comfortable budget.

Unit tests are hermetic — every GitHub response is injected through a fake
transport:

```sh
make test-project-metadata      # or: make test-browser-lab
```

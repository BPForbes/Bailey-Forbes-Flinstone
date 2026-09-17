#!/usr/bin/env python3
"""Generate the public ``project-metadata.json`` contract for the browser lab.

The generator is deliberately subordinate to the existing Flintstone validation
pipeline: language classification comes from GitHub Linguist (via the REST
Languages endpoint), timeline entries come from the REST API, and every
kernel/browser fact is read back from the artifacts the validated build already
produced (``dist/build-info.json`` and ``dist/browser-validation.json``).

Nothing here re-derives compatibility, and nothing here is published unless the
caller states that the promotion gates passed.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

SCHEMA_VERSION = 1
DEFAULT_API_BASE = "https://api.github.com"
USER_AGENT = "flintstone-project-metadata/1"
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
#  "Co-authored-by: Name <mail@example.com>" (RFC-ish trailer, case-insensitive).
COAUTHOR_RE = re.compile(r"^\s*co-authored-by:\s*(?P<name>.*?)\s*<(?P<email>[^>]*)>\s*$", re.IGNORECASE)
# GitHub's public no-reply forms: "12345+login@users.noreply.github.com".
NOREPLY_RE = re.compile(r"^(?:\d+\+)?(?P<login>[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?)@users\.noreply\.github\.com$", re.IGNORECASE)
EMAIL_RE = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
TOKEN_RE = re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{16,}|github_pat_[A-Za-z0-9_]{20,})")
# Routine dependency chores are only dropped when no human is associated at all.
DEPENDENCY_TITLE_RE = re.compile(r"^\s*(?:build|chore|deps|fix)?\(?deps(?:-dev)?\)?[:\s]|^\s*bump\s+\S+\s+from\s+\S+\s+to\s+", re.IGNORECASE)
DEPENDENCY_BOTS = frozenset({"dependabot", "dependabot-preview", "renovate", "pyup-bot", "snyk-bot", "greenkeeper"})


class MetadataError(RuntimeError):
    """Raised when metadata cannot be generated truthfully."""


# --------------------------------------------------------------------------
# GitHub REST transport
# --------------------------------------------------------------------------

def urllib_transport(url, headers, timeout=30):
    """Return ``(status, headers, body)`` for one GET request."""
    request = urllib.request.Request(url, headers=headers, method="GET")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, dict(response.headers), response.read()
    except urllib.error.HTTPError as error:  # still carries status/headers/body
        return error.code, dict(error.headers or {}), error.read()
    except urllib.error.URLError as error:
        raise MetadataError(f"GitHub request failed for {url}: {error.reason}") from error


class GitHubClient:
    """A very small, fail-loud REST client with pagination and rate-limit awareness."""

    def __init__(self, api_base=DEFAULT_API_BASE, token=None, transport=urllib_transport,
                 attempts=3, sleep=time.sleep):
        self.api_base = api_base.rstrip("/")
        self.token = token
        self.transport = transport
        self.attempts = max(1, int(attempts))
        self.sleep = sleep

    def _headers(self):
        headers = {
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": USER_AGENT,
        }
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        return headers

    def get(self, path, params=None):
        """GET one JSON document, retrying transient failures."""
        url = f"{self.api_base}{path}"
        if params:
            url = f"{url}?{urllib.parse.urlencode(params)}"
        last = None
        for attempt in range(1, self.attempts + 1):
            status, headers, body = self.transport(url, self._headers())
            if status == 200:
                try:
                    return json.loads(body.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError) as error:
                    raise MetadataError(f"GitHub returned non-JSON for {path}: {error}") from error
            lowered = {str(key).lower(): str(value) for key, value in headers.items()}
            if status in (403, 429) and lowered.get("x-ratelimit-remaining") == "0":
                reset = lowered.get("x-ratelimit-reset", "unknown")
                raise MetadataError(
                    f"GitHub rate limit exhausted for {path} (HTTP {status}); resets at {reset}."
                )
            last = f"HTTP {status} for {path}"
            if status in (429,) or 500 <= status < 600:
                if attempt < self.attempts:
                    self.sleep(min(2 ** attempt, 8))
                    continue
            break
        raise MetadataError(f"GitHub request failed: {last}")

    def paginate(self, path, params=None, max_pages=10, per_page=100):
        """Yield items across pages, stopping at ``max_pages`` or a short page."""
        items = []
        for page in range(1, max_pages + 1):
            query = dict(params or {})
            query.update({"per_page": per_page, "page": page})
            payload = self.get(path, query)
            if not isinstance(payload, list):
                raise MetadataError(f"Expected a JSON array from {path}, got {type(payload).__name__}")
            items.extend(payload)
            if len(payload) < per_page:
                break
        return items


# --------------------------------------------------------------------------
# Normalisation helpers
# --------------------------------------------------------------------------

def _require_str(value, field):
    if not isinstance(value, str) or not value:
        raise MetadataError(f"Expected a non-empty string for {field}")
    return value


def _require_mapping(value, field):
    if not isinstance(value, dict):
        raise MetadataError(f"Expected a JSON object for {field}")
    return value


def normalize_timestamp(value, field):
    """Accept any ISO-8601 instant GitHub emits and return a ``Z`` timestamp."""
    _require_str(value, field)
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise MetadataError(f"Invalid timestamp for {field}: {value!r}") from error
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _sort_key(value):
    return normalize_timestamp(value, "timeline date")


def build_repository(repo_payload):
    """Project the REST repository payload onto the public contract."""
    payload = _require_mapping(repo_payload, "repository")
    owner = _require_mapping(payload.get("owner"), "repository.owner")
    section = {
        "owner": _require_str(owner.get("login"), "repository.owner.login"),
        "name": _require_str(payload.get("name"), "repository.name"),
        "defaultBranch": _require_str(payload.get("default_branch"), "repository.default_branch"),
        "url": _require_str(payload.get("html_url"), "repository.html_url"),
    }
    description = payload.get("description")
    if isinstance(description, str) and description:
        section["description"] = description
    return section


def build_languages(languages_payload):
    """Turn Linguist byte counts into a sorted, percentage-annotated list.

    No language whitelist is applied: whatever Linguist reports (after the
    repository's own ``.gitattributes`` overrides and the vendor/generated rules
    GitHub already applies) is what is published.
    """
    payload = _require_mapping(languages_payload, "languages")
    entries = []
    for name, count in payload.items():
        if not isinstance(name, str) or not name:
            raise MetadataError("Language names must be non-empty strings")
        if not isinstance(count, int) or isinstance(count, bool) or count < 0:
            raise MetadataError(f"Language byte count for {name!r} must be a non-negative integer")
        entries.append((name, count))
    total = sum(count for _, count in entries)
    entries.sort(key=lambda item: (-item[1], item[0]))
    return [
        {
            "name": name,
            "bytes": count,
            "percentage": round(count * 100.0 / total, 1) if total else 0.0,
        }
        for name, count in entries
    ]


def _login_of(actor):
    if isinstance(actor, dict):
        login = actor.get("login")
        if isinstance(login, str) and login:
            return login
    return None


def _is_bot(actor):
    if not isinstance(actor, dict):
        return False
    if str(actor.get("type", "")).lower() == "bot":
        return True
    login = _login_of(actor) or ""
    return login.endswith("[bot]")


def _display_name(name):
    """Return a publishable display name, never an email address."""
    if not isinstance(name, str):
        return None
    cleaned = name.strip().strip('"')
    if not cleaned or EMAIL_RE.search(cleaned):
        return None
    return cleaned


def extract_contributors(pull, commits):
    """Resolve the human/agent contributors behind a merged pull request.

    Co-authorship is read from standard ``Co-authored-by:`` trailers and from the
    GitHub accounts attached to the PR's commits. Email addresses are used only
    to resolve a public ``users.noreply.github.com`` login and are never emitted.
    """
    author = _login_of(pull.get("user"))
    co_authors = []
    seen = set()

    def add(candidate):
        if not candidate:
            return
        key = candidate.lower()
        if key in seen or (author and key == author.lower()):
            return
        seen.add(key)
        co_authors.append(candidate)

    for commit in commits or []:
        if not isinstance(commit, dict):
            continue
        # A commit's own GitHub account counts, but CI identities are noise; an
        # explicit Co-authored-by trailer below is always honoured.
        if not _is_bot(commit.get("author")):
            add(_login_of(commit.get("author")))
        message = (commit.get("commit") or {}).get("message")
        if not isinstance(message, str):
            continue
        for line in message.splitlines():
            match = COAUTHOR_RE.match(line)
            if not match:
                continue
            noreply = NOREPLY_RE.match(match.group("email").strip())
            if noreply:
                add(noreply.group("login"))
            else:
                add(_display_name(match.group("name")))
    return author, co_authors


def is_routine_bot_maintenance(pull, author, co_authors):
    """True only for bot-authored dependency chores with no human association."""
    if not _is_bot(pull.get("user")):
        return False
    if co_authors:
        return False
    # A human merge actor still counts as meaningful human association.
    merged_by = pull.get("merged_by")
    if merged_by and not _is_bot(merged_by):
        return False
    login = (author or "").lower().removesuffix("[bot]")
    title = pull.get("title") if isinstance(pull.get("title"), str) else ""
    return login in DEPENDENCY_BOTS or bool(DEPENDENCY_TITLE_RE.search(title))


def build_pull_request_events(pulls, commits_for=None):
    """Select merged pull requests and project them onto timeline entries."""
    events = []
    for pull in pulls:
        if not isinstance(pull, dict):
            raise MetadataError("Expected pull request objects from the REST API")
        merged_at = pull.get("merged_at")
        if not merged_at:  # closed-but-unmerged and open PRs never appear
            continue
        number = pull.get("number")
        if not isinstance(number, int) or isinstance(number, bool):
            raise MetadataError("Pull request numbers must be integers")
        commits = commits_for(number) if commits_for else []
        author, co_authors = extract_contributors(pull, commits)
        if is_routine_bot_maintenance(pull, author, co_authors):
            continue
        date = normalize_timestamp(merged_at, f"pull #{number} merged_at")
        entry = {
            "type": "pull_request",
            "date": date,
            "number": number,
            "title": _require_str(pull.get("title"), f"pull #{number} title"),
            "mergedAt": date,
            "url": _require_str(pull.get("html_url"), f"pull #{number} html_url"),
        }
        if author:
            entry["author"] = author
        if co_authors:
            entry["coAuthors"] = co_authors
        events.append(entry)
    return events


def build_release_events(releases):
    """Project published GitHub Releases onto timeline entries (zero is fine)."""
    events = []
    for release in releases or []:
        if not isinstance(release, dict):
            raise MetadataError("Expected release objects from the REST API")
        if release.get("draft"):
            continue
        published_at = release.get("published_at")
        if not published_at:
            continue
        tag = _require_str(release.get("tag_name"), "release tag_name")
        date = normalize_timestamp(published_at, f"release {tag} published_at")
        title = release.get("name")
        events.append({
            "type": "release",
            "date": date,
            "tag": tag,
            "title": title if isinstance(title, str) and title else tag,
            "publishedAt": date,
            "url": _require_str(release.get("html_url"), f"release {tag} html_url"),
            "prerelease": bool(release.get("prerelease")),
        })
    return events


def order_timeline(events, limit=None):
    """Newest-first, with a fully deterministic tie-break."""
    ordered = sorted(
        events,
        key=lambda event: (
            event["date"],
            str(event.get("number", event.get("tag", ""))),
            event["type"],
        ),
        reverse=True,
    )
    return ordered[:limit] if limit else ordered


def build_build_section(build_info, browser_validation, boot_smoke_passed, source_commit):
    """Expose a compact subset of the *existing* validation outputs.

    This function never computes compatibility; it only republishes what the
    validated pipeline already decided.
    """
    info = _require_mapping(build_info, "build-info.json")
    build_commit = info.get("commit")
    if isinstance(build_commit, str) and build_commit and source_commit and build_commit != source_commit:
        raise MetadataError(
            "dist/build-info.json was produced from commit "
            f"{build_commit} but the deployment source commit is {source_commit}"
        )
    for field in ("bootable", "bootSuccessMarkerImplemented"):
        if not isinstance(info.get(field), bool):
            raise MetadataError(f"build-info.json {field} must be a JSON boolean")
    browser_compatible = info.get("browserCompatible", info.get("v86Compatible"))
    if not isinstance(browser_compatible, bool):
        raise MetadataError("build-info.json browserCompatible must be a JSON boolean")
    section = {
        "sourceCommit": source_commit,
        "bootable": info["bootable"],
        "browserCompatible": browser_compatible,
        "bootSmokePassed": bool(boot_smoke_passed),
        "bootSuccessMarkerImplemented": info["bootSuccessMarkerImplemented"],
        "validationOutcome": _require_str(info.get("validationOutcome"), "validationOutcome"),
        "architecture": _require_str(info.get("architecture"), "architecture"),
        "artifactFormat": _require_str(info.get("artifactFormat"), "artifactFormat"),
        "artifactSha256": _require_str(info.get("sha256"), "sha256"),
        "builtAt": normalize_timestamp(info.get("builtAt"), "build-info.json builtAt"),
        "blockers": list(info.get("blockers") or []),
    }
    if isinstance(info.get("browserEmulator"), str) and info["browserEmulator"]:
        section["browserEmulator"] = info["browserEmulator"]
    evidence = browser_validation if isinstance(browser_validation, dict) else None
    if evidence:
        validation = {}
        if isinstance(evidence.get("browser"), str) and evidence["browser"]:
            validation["browser"] = evidence["browser"]
        if evidence.get("testedAt"):
            validation["testedAt"] = normalize_timestamp(evidence["testedAt"], "browser-validation testedAt")
        checks = evidence.get("checks")
        if isinstance(checks, list) and all(isinstance(check, str) for check in checks):
            validation["checks"] = list(checks)
        if isinstance(evidence.get("runtime"), str) and evidence["runtime"]:
            validation["runtime"] = evidence["runtime"]
        if isinstance(evidence.get("runtimeCommit"), str) and evidence["runtimeCommit"]:
            validation["runtimeCommit"] = evidence["runtimeCommit"]
        if validation:
            section["browserValidation"] = validation
    return section


def build_lab_section(build_info, gates_passed, publish_context):
    """Describe the browser lab honestly: it is an emulated kernel demonstration."""
    info = _require_mapping(build_info, "build-info.json")
    return {
        "type": "browser-kernel",
        "architecture": _require_str(info.get("architecture"), "architecture"),
        "executionModel": "emulated",
        "physicalHardwareDrivers": False,
        "published": bool(gates_passed and publish_context),
    }


def build_metadata(*, repository, languages, timeline, build, lab, source_commit, generated_at):
    return {
        "schemaVersion": SCHEMA_VERSION,
        "generatedAt": generated_at,
        "sourceCommit": source_commit,
        "repository": repository,
        "languages": languages,
        "build": build,
        "lab": lab,
        "timeline": timeline,
    }


# --------------------------------------------------------------------------
# Validation of the produced document
# --------------------------------------------------------------------------

def _walk_strings(node):
    if isinstance(node, dict):
        for key, value in node.items():
            yield from _walk_strings(key)
            yield from _walk_strings(value)
    elif isinstance(node, list):
        for value in node:
            yield from _walk_strings(value)
    elif isinstance(node, str):
        yield node


def validate_metadata(document):
    """Fail loudly rather than publish malformed or leaky metadata."""
    doc = _require_mapping(document, "project-metadata.json")
    if doc.get("schemaVersion") != SCHEMA_VERSION:
        raise MetadataError(f"schemaVersion must be {SCHEMA_VERSION}")
    normalize_timestamp(doc.get("generatedAt"), "generatedAt")
    commit = _require_str(doc.get("sourceCommit"), "sourceCommit")
    if not COMMIT_RE.match(commit):
        raise MetadataError(f"sourceCommit must be a 40-character SHA, got {commit!r}")

    repository = _require_mapping(doc.get("repository"), "repository")
    for field in ("owner", "name", "defaultBranch", "url"):
        _require_str(repository.get(field), f"repository.{field}")

    languages = doc.get("languages")
    if not isinstance(languages, list):
        raise MetadataError("languages must be a JSON array")
    previous = None
    for language in languages:
        entry = _require_mapping(language, "language entry")
        _require_str(entry.get("name"), "language name")
        count = entry.get("bytes")
        if not isinstance(count, int) or isinstance(count, bool) or count < 0:
            raise MetadataError("language bytes must be a non-negative integer")
        percentage = entry.get("percentage")
        if not isinstance(percentage, (int, float)) or isinstance(percentage, bool):
            raise MetadataError("language percentage must be a number")
        if not 0.0 <= float(percentage) <= 100.0:
            raise MetadataError("language percentage must be between 0 and 100")
        if previous is not None and count > previous:
            raise MetadataError("languages must be sorted largest-to-smallest")
        previous = count

    build = _require_mapping(doc.get("build"), "build")
    for field in ("bootable", "browserCompatible", "bootSmokePassed"):
        if not isinstance(build.get(field), bool):
            raise MetadataError(f"build.{field} must be a JSON boolean")
    if build.get("sourceCommit") != commit:
        raise MetadataError("build.sourceCommit must match the top-level sourceCommit")

    lab = _require_mapping(doc.get("lab"), "lab")
    if not isinstance(lab.get("published"), bool):
        raise MetadataError("lab.published must be a JSON boolean")
    if lab.get("physicalHardwareDrivers") is not False:
        raise MetadataError("lab.physicalHardwareDrivers must be false")
    if lab.get("published") and not all(
        build[field] for field in ("bootable", "browserCompatible", "bootSmokePassed")
    ):
        raise MetadataError("lab.published cannot be true unless every promotion gate passed")

    timeline = doc.get("timeline")
    if not isinstance(timeline, list):
        raise MetadataError("timeline must be a JSON array")
    last_date = None
    for event in timeline:
        entry = _require_mapping(event, "timeline entry")
        kind = entry.get("type")
        if kind not in ("pull_request", "release"):
            raise MetadataError(f"Unsupported timeline entry type: {kind!r}")
        date = normalize_timestamp(entry.get("date"), "timeline date")
        _require_str(entry.get("title"), "timeline title")
        _require_str(entry.get("url"), "timeline url")
        if kind == "pull_request":
            if not isinstance(entry.get("number"), int) or isinstance(entry.get("number"), bool):
                raise MetadataError("pull_request entries need an integer number")
            co_authors = entry.get("coAuthors", [])
            if not isinstance(co_authors, list) or not all(
                isinstance(name, str) and name for name in co_authors
            ):
                raise MetadataError("coAuthors must be a list of non-empty strings")
        else:
            _require_str(entry.get("tag"), "release tag")
        if last_date is not None and date > last_date:
            raise MetadataError("timeline must be ordered newest-first")
        last_date = date

    for text in _walk_strings(doc):
        if EMAIL_RE.search(text):
            raise MetadataError("Refusing to publish metadata containing an email address")
        if TOKEN_RE.search(text):
            raise MetadataError("Refusing to publish metadata containing a credential")
    return doc


def write_metadata(document, output_path):
    """Validate, then atomically replace the published file."""
    validate_metadata(document)
    serialized = json.dumps(document, indent=2, ensure_ascii=False, sort_keys=False) + "\n"
    # Re-parse and re-validate exactly what will land on disk.
    validate_metadata(json.loads(serialized))
    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    handle = tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=str(output.parent), prefix=".project-metadata-", delete=False
    )
    try:
        with handle:
            handle.write(serialized)
        os.replace(handle.name, output)
    except BaseException:
        Path(handle.name).unlink(missing_ok=True)
        raise
    return output


# --------------------------------------------------------------------------
# Orchestration
# --------------------------------------------------------------------------

def _read_json(path, required=True):
    file = Path(path)
    if not file.exists():
        if required:
            raise MetadataError(f"Required build output is missing: {path}")
        return None
    try:
        return json.loads(file.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise MetadataError(f"{path} is not valid JSON: {error}") from error


def _parse_bool(value, field):
    if isinstance(value, bool):
        return value
    text = str(value).strip().lower()
    if text in ("true", "1", "yes"):
        return True
    if text in ("false", "0", "no"):
        return False
    raise MetadataError(f"{field} must be stated explicitly as true or false, got {value!r}")


def collect(client, owner, repo, *, max_timeline, max_pull_pages, include_commits=True):
    """Fetch every REST input the contract needs."""
    repository = client.get(f"/repos/{owner}/{repo}")
    languages = client.get(f"/repos/{owner}/{repo}/languages")
    default_branch = repository.get("default_branch") if isinstance(repository, dict) else None
    pulls = client.paginate(
        f"/repos/{owner}/{repo}/pulls",
        {"state": "closed", "sort": "updated", "direction": "desc",
         **({"base": default_branch} if default_branch else {})},
        max_pages=max_pull_pages,
    )
    merged = [pull for pull in pulls if isinstance(pull, dict) and pull.get("merged_at")]
    merged.sort(key=lambda pull: (_sort_key(pull["merged_at"]), pull.get("number") or 0), reverse=True)
    # Only the candidates that can reach the timeline are worth a commits round-trip.
    candidates = merged[: max_timeline + 10]
    commit_cache = {}
    if include_commits:
        for pull in candidates:
            number = pull.get("number")
            commit_cache[number] = client.paginate(
                f"/repos/{owner}/{repo}/pulls/{number}/commits", max_pages=1
            )
    releases = client.paginate(f"/repos/{owner}/{repo}/releases", max_pages=3)
    return repository, languages, candidates, commit_cache, releases


def generate(args, client=None, now=None):
    owner, _, repo = args.repository.partition("/")
    if not owner or not repo:
        raise MetadataError(f"--repository must be OWNER/NAME, got {args.repository!r}")
    source_commit = args.source_commit
    if not COMMIT_RE.match(source_commit or ""):
        raise MetadataError(
            "--source-commit must be a 40-character SHA (GITHUB_SHA), got "
            f"{source_commit!r}"
        )

    build_info = _read_json(args.build_info)
    browser_validation = _read_json(args.browser_validation, required=False)
    boot_smoke_passed = _parse_bool(args.boot_smoke_passed, "--boot-smoke-passed")

    client = client or GitHubClient(api_base=args.api_base, token=args.token)
    repository, languages, pulls, commit_cache, releases = collect(
        client, owner, repo,
        max_timeline=args.max_timeline,
        max_pull_pages=args.max_pull_pages,
        include_commits=not args.skip_commit_authors,
    )

    events = build_pull_request_events(pulls, lambda number: commit_cache.get(number, []))
    events.extend(build_release_events(releases))
    timeline = order_timeline(events, limit=args.max_timeline)

    build = build_build_section(build_info, browser_validation, boot_smoke_passed, source_commit)
    gates_passed = build["bootable"] and build["browserCompatible"] and build["bootSmokePassed"]
    lab = build_lab_section(build_info, gates_passed, args.publish_context)

    generated_at = (now or datetime.now(timezone.utc)).strftime("%Y-%m-%dT%H:%M:%SZ")
    return build_metadata(
        repository=build_repository(repository),
        languages=build_languages(languages),
        timeline=timeline,
        build=build,
        lab=lab,
        source_commit=source_commit,
        generated_at=generated_at,
    )


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", ""),
                        help="OWNER/NAME (defaults to $GITHUB_REPOSITORY)")
    parser.add_argument("--source-commit", default=os.environ.get("GITHUB_SHA", ""),
                        help="Deployed build commit (defaults to $GITHUB_SHA)")
    parser.add_argument("--build-info", default="dist/build-info.json")
    parser.add_argument("--browser-validation", default="dist/browser-validation.json")
    parser.add_argument("--boot-smoke-passed", default=os.environ.get("FL_BOOT_SMOKE_PASSED", ""),
                        help="Result of the existing QEMU boot smoke step (true/false)")
    parser.add_argument("--publish-context", action="store_true",
                        help="Set when this payload is the one being deployed to GitHub Pages")
    parser.add_argument("--output", default="dist/browser-lab/project-metadata.json")
    parser.add_argument("--max-timeline", type=int, default=25)
    parser.add_argument("--max-pull-pages", type=int, default=10)
    parser.add_argument("--skip-commit-authors", action="store_true",
                        help="Skip per-PR commit lookups (co-author trailers are then unavailable)")
    parser.add_argument("--api-base", default=os.environ.get("GITHUB_API_URL", DEFAULT_API_BASE))
    parser.add_argument("--token", default=os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN"))
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        document = generate(args)
        output = write_metadata(document, args.output)
    except MetadataError as error:
        print(f"generate_project_metadata: {error}", file=sys.stderr)
        return 1
    print(f"generate_project_metadata: wrote {output}")
    print(f"  languages: {len(document['languages'])}, timeline: {len(document['timeline'])}")
    print(f"  sourceCommit: {document['sourceCommit']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

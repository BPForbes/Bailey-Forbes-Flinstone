#!/usr/bin/env python3
"""Regression tests for the public project-metadata contract generator.

Every GitHub response is injected through a fake transport, so the suite is
hermetic: it never touches the network and never needs a token.
"""
import argparse
import json
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from scripts.generate_project_metadata import (  # noqa: E402
    GitHubClient,
    MetadataError,
    TransportError,
    build_languages,
    build_build_section,
    build_lab_section,
    build_named_releases,
    build_pull_request_events,
    build_release_events,
    build_repository,
    extract_contributors,
    generate,
    is_routine_bot_maintenance,
    order_timeline,
    validate_metadata,
    write_metadata,
)

COMMIT = "a" * 40
failures = []


def check(name, condition):
    if not condition:
        failures.append(name)


def expect_error(name, callable_, *args, **kwargs):
    try:
        callable_(*args, **kwargs)
    except MetadataError:
        return
    except Exception as error:  # pragma: no cover - surfaced through failures
        failures.append(f"{name} (raised {type(error).__name__}: {error})")
        return
    failures.append(f"{name} (no MetadataError raised)")


# ---------------------------------------------------------------------------
# Languages: dynamic percentages, no whitelist, deterministic ordering
# ---------------------------------------------------------------------------
languages = build_languages({"C": 600, "Assembly": 300, "Brand New Lang": 100})
check("languages sorted largest-to-smallest",
      [entry["name"] for entry in languages] == ["C", "Assembly", "Brand New Lang"])
check("raw byte counts preserved", [entry["bytes"] for entry in languages] == [600, 300, 100])
check("percentages computed dynamically",
      [entry["percentage"] for entry in languages] == [60.0, 30.0, 10.0])
check("a newly introduced language needs no generator change",
      languages[-1] == {"name": "Brand New Lang", "bytes": 100, "percentage": 10.0})
check("empty language payload is tolerated", build_languages({}) == [])
check("equal byte counts break ties by name",
      [entry["name"] for entry in build_languages({"Zig": 10, "Ada": 10})] == ["Ada", "Zig"])
expect_error("non-integer byte counts rejected", build_languages, {"C": "600"})
expect_error("negative byte counts rejected", build_languages, {"C": -1})


# ---------------------------------------------------------------------------
# Repository projection
# ---------------------------------------------------------------------------
repo_payload = {
    "name": "Bailey-Forbes-Flinstone",
    "owner": {"login": "BPForbes"},
    "default_branch": "main",
    "html_url": "https://github.com/BPForbes/Bailey-Forbes-Flinstone",
    "description": "Educational OS/shell codebase",
}
check("an upstream email in the description is redacted, not fatal",
      build_repository({**repo_payload, "description": "ping ops@example.com"})["description"]
      == "ping [redacted]")
check("repository projection", build_repository(repo_payload) == {
    "owner": "BPForbes",
    "name": "Bailey-Forbes-Flinstone",
    "defaultBranch": "main",
    "url": "https://github.com/BPForbes/Bailey-Forbes-Flinstone",
    "description": "Educational OS/shell codebase",
})
expect_error("missing default_branch rejected", build_repository, {
    "name": "x", "owner": {"login": "y"}, "html_url": "https://example.invalid"})


# ---------------------------------------------------------------------------
# Timeline: merged only, agent-assisted work stays eligible, no emails
# ---------------------------------------------------------------------------
def pull(number, *, merged_at, title="Change", login="BPForbes", user_type="User", html=None):
    return {
        "number": number,
        "title": title,
        "merged_at": merged_at,
        "html_url": html or f"https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/{number}",
        "user": {"login": login, "type": user_type},
    }


def commit(message, login=None):
    author = {"login": login, "type": "User"} if login else None
    return {"commit": {"message": message}, "author": author}


merged = pull(354, merged_at="2026-09-17T12:00:00Z", title="Add browser kernel artifact")
unmerged = {**pull(353, merged_at=None, title="Abandoned idea"), "merged_at": None}
events = build_pull_request_events([merged, unmerged], lambda number: [])
check("merged PRs appear", [event["number"] for event in events] == [354])
check("closed-but-unmerged PRs never appear",
      all(event["number"] != 353 for event in events))
check("pull entry shape", events[0] == {
    "type": "pull_request",
    "date": "2026-09-17T12:00:00Z",
    "number": 354,
    "title": "Add browser kernel artifact",
    "mergedAt": "2026-09-17T12:00:00Z",
    "url": "https://github.com/BPForbes/Bailey-Forbes-Flinstone/pull/354",
    "author": "BPForbes",
})

emailed = pull(361, merged_at="2026-09-16T10:00:00Z",
               title="fix: alert ops@example.com when the relay drops")
redacted = build_pull_request_events([emailed], lambda number: [])
check("an upstream email in a PR title is redacted, not fatal",
      redacted[0]["title"] == "fix: alert [redacted] when the relay drops")
check("a redacted title still validates as publishable",
      "@" not in json.dumps(redacted))

emailed_release = build_release_events([
    {"tag_name": "v1.0.0", "name": "thanks ops@example.com", "draft": False,
     "published_at": "2026-01-01T00:00:00Z", "html_url": "https://example.invalid/r"},
])
check("an upstream email in a release title is redacted",
      emailed_release[0]["title"] == "thanks [redacted]")

agent_pull = pull(360, merged_at="2026-09-16T09:30:00Z", title="Publish project metadata",
                  login="claude[bot]", user_type="Bot")
agent_commits = [
    commit("feat: metadata\n\nCo-authored-by: Bailey Forbes <62630945+BPForbes@users.noreply.github.com>"),
    commit("chore: tidy\n\nCo-authored-by: Ada Lovelace <ada@example.com>"),
]
agent_events = build_pull_request_events([agent_pull], lambda number: agent_commits)
check("agent-authored PR with a human co-author stays eligible", len(agent_events) == 1)
check("noreply co-author resolves to the GitHub login",
      agent_events[0]["coAuthors"][0] == "BPForbes")
check("non-noreply co-author falls back to a display name",
      "Ada Lovelace" in agent_events[0]["coAuthors"])
check("no private email is exposed",
      "@" not in json.dumps(agent_events))

author, co_authors = extract_contributors(
    pull(1, merged_at="2026-01-01T00:00:00Z", login="BPForbes"),
    [commit("x\n\nCo-authored-by: Bailey Forbes <62630945+BPForbes@users.noreply.github.com>")],
)
check("self co-authorship is not duplicated", author == "BPForbes" and co_authors == [])

_, ci_noise = extract_contributors(
    pull(2, merged_at="2026-01-02T00:00:00Z", login="BPForbes"),
    [{"commit": {"message": "chore(version): sync"},
      "author": {"login": "github-actions[bot]", "type": "Bot"}},
     commit("feat: work", login="cursoragent")],
)
check("CI bot commit identities are not listed as co-authors", ci_noise == ["cursoragent"])

bot_dep = pull(200, merged_at="2026-02-02T00:00:00Z",
               title="build(deps): bump left-pad from 1.0.0 to 1.0.1",
               login="dependabot[bot]", user_type="Bot")
check("routine bot-only dependency chores are dropped",
      is_routine_bot_maintenance(bot_dep, "dependabot[bot]", []) is True)
check("dependency chore with a human co-author is kept",
      is_routine_bot_maintenance(bot_dep, "dependabot[bot]", ["BPForbes"]) is False)
check("bot-authored feature work is kept",
      is_routine_bot_maintenance(agent_pull, "claude[bot]", []) is False)
check("human-authored dependency bump is kept",
      is_routine_bot_maintenance(
          pull(201, merged_at="2026-02-03T00:00:00Z", title="chore(deps): bump"),
          "BPForbes", []) is False)
check("bot-only dependency chores are filtered out of the timeline",
      build_pull_request_events([bot_dep], lambda number: []) == [])
check("a human merge actor keeps a bot-authored dependency chore",
      is_routine_bot_maintenance(
          {**bot_dep, "merged_by": {"login": "BPForbes", "type": "User"}},
          "dependabot[bot]", []) is False)

# GitHub's "List pull requests" response omits merged_by, so the merge actor has
# to be hydrated from "Get a pull request" before a chore is dropped. The list
# payloads below deliberately carry no merged_by, exactly like production.
hydrated = []


def hydrate_human(number):
    hydrated.append(number)
    return {"merged_by": {"login": "BPForbes", "type": "User"}}


def hydrate_bot(number):
    hydrated.append(number)
    return {"merged_by": {"login": "dependabot[bot]", "type": "Bot"}}


hydrated.clear()
rescued = build_pull_request_events([bot_dep], lambda number: [], hydrate=hydrate_human)
check("a human-merged dependency chore is rescued by hydrating merged_by",
      [entry["number"] for entry in rescued] == [200])
check("hydration is only requested for a candidate about to be dropped",
      hydrated == [200])

hydrated.clear()
check("a bot-merged dependency chore still drops after hydration",
      build_pull_request_events([bot_dep], lambda number: [], hydrate=hydrate_bot) == [])
check("the bot-merged chore was hydrated before being dropped", hydrated == [200])

hydrated.clear()
build_pull_request_events([merged], lambda number: [], hydrate=hydrate_human)
check("eligible work never costs a hydration call", hydrated == [])

# A burst of filtered bot maintenance must not starve the timeline: older
# eligible work has to be walked into instead of stopping at a fixed buffer.
burst = [
    pull(900 - index, merged_at=f"2026-08-{28 - index:02d}T00:00:00Z",
         title="build(deps): bump left-pad from 1.0.0 to 1.0.1",
         login="dependabot[bot]", user_type="Bot")
    for index in range(15)
]
human_tail = [
    pull(800 - index, merged_at=f"2026-07-{28 - index:02d}T00:00:00Z",
         title=f"Real work {index}")
    for index in range(5)
]
filled = build_pull_request_events(burst + human_tail, lambda number: [], limit=3)
check("a bot-maintenance burst does not starve the timeline", len(filled) == 3)
check("the timeline fills from older eligible work",
      [entry["number"] for entry in filled] == [800, 799, 798])
check("the walk stops once the limit is reached",
      build_pull_request_events(human_tail, lambda number: [], limit=2) ==
      build_pull_request_events(human_tail, lambda number: [])[:2])

lookup_calls = []
capped = build_pull_request_events(
    burst + human_tail, lambda number: lookup_calls.append(number) or [],
    limit=5, max_lookups=4)
check("max_lookups caps per-pull API calls", len(lookup_calls) == 4)
check("a capped walk returns only what it could verify", capped == [])


# ---------------------------------------------------------------------------
# Releases: optional, published-only, tolerant of zero
# ---------------------------------------------------------------------------
check("zero releases are tolerated", build_release_events([]) == [])
check("zero releases are tolerated (null)", build_release_events(None) == [])
releases = build_release_events([
    {"tag_name": "v4.5.4", "name": "Flintstone Kernel v4.5.4", "draft": False,
     "prerelease": False, "published_at": "2026-09-15T00:00:00Z",
     "html_url": "https://github.com/BPForbes/Bailey-Forbes-Flinstone/releases/tag/v4.5.4"},
    {"tag_name": "v9.9.9", "draft": True, "published_at": "2026-09-16T00:00:00Z",
     "html_url": "https://example.invalid"},
    {"tag_name": "v0.0.1", "draft": False, "published_at": None, "html_url": "https://example.invalid"},
])
check("only published releases become events", [event["tag"] for event in releases] == ["v4.5.4"])
check("release falls back to the tag for a title", releases[0]["title"] == "Flintstone Kernel v4.5.4")

ordered = order_timeline(events + agent_events + releases)
check("timeline is newest-first",
      [entry["date"] for entry in ordered] == sorted((e["date"] for e in ordered), reverse=True))
check("timeline ordering is deterministic",
      order_timeline(list(reversed(events + agent_events + releases))) == ordered)
check("timeline honours the limit", len(order_timeline(ordered, limit=2)) == 2)


# ---------------------------------------------------------------------------
# Named releases: curated, not derived, and never fabricated
# ---------------------------------------------------------------------------
REPO_URL = "https://github.com/BPForbes/Bailey-Forbes-Flinstone"

check("no curated file publishes zero named releases",
      build_named_releases(None, repository_url=REPO_URL, source_commit=COMMIT) == [])

curated = {
    "releases": [
        {
            "version": "4.0.0 / 4.0.1",
            "startDate": "2026-05-18",
            "endDate": "2026-05-19",
            "summary": "Contracts · IPC/VFS · serial-j1",
            "description": "Inheritable system contracts; then IPC/VFS/shell hardening.",
        },
        {
            "version": "3.3.0",
            "startDate": "2026-05-12",
            "endDate": None,
            "summary": "fs_jail · audit · FL1 history",
            "description": "Contract surfaces for fs_jail and audit.",
        },
    ],
}
named = build_named_releases(curated, repository_url=REPO_URL, source_commit=COMMIT)
check("named releases carry a stable slug id", [entry["id"] for entry in named] == ["4-0-0-4-0-1", "3-3-0"])
check("named releases are ordered newest-first by startDate",
      [entry["version"] for entry in named] == ["4.0.0 / 4.0.1", "3.3.0"])
check("a curated release with no endDate publishes null",
      named[1]["endDate"] is None)
check("a curated release defaults its url to the deployed commit's version/locked",
      named[0]["url"] == f"{REPO_URL}/tree/{COMMIT}/version/locked")
check("summary and description are republished verbatim",
      named[0]["summary"] == "Contracts · IPC/VFS · serial-j1"
      and named[0]["description"].startswith("Inheritable system contracts"))

check("a curator-supplied url overrides the default", build_named_releases(
    {"releases": [{**curated["releases"][0],
                   "url": f"{REPO_URL}/blob/{COMMIT}/version/locked/4_0_0.ver"}]},
    repository_url=REPO_URL, source_commit=COMMIT,
)[0]["url"] == f"{REPO_URL}/blob/{COMMIT}/version/locked/4_0_0.ver")

expect_error("a non-github release url is rejected", build_named_releases,
             {"releases": [{**curated["releases"][0], "url": "https://example.com/x"}]},
             repository_url=REPO_URL, source_commit=COMMIT)
expect_error("a duplicated version is rejected", build_named_releases,
             {"releases": [curated["releases"][0], curated["releases"][0]]},
             repository_url=REPO_URL, source_commit=COMMIT)
expect_error("a malformed startDate is rejected", build_named_releases,
             {"releases": [{**curated["releases"][0], "startDate": "18 May 2026"}]},
             repository_url=REPO_URL, source_commit=COMMIT)
expect_error("a calendar-invalid date is rejected", build_named_releases,
             {"releases": [{**curated["releases"][0], "startDate": "2026-02-30"}]},
             repository_url=REPO_URL, source_commit=COMMIT)
expect_error("an endDate before startDate is rejected", build_named_releases,
             {"releases": [{**curated["releases"][0], "startDate": "2026-05-19",
                            "endDate": "2026-05-18"}]},
             repository_url=REPO_URL, source_commit=COMMIT)
expect_error("a missing summary is rejected", build_named_releases,
             {"releases": [{k: v for k, v in curated["releases"][0].items() if k != "summary"}]},
             repository_url=REPO_URL, source_commit=COMMIT)
expect_error("releases must be a JSON array", build_named_releases,
             {"releases": "not a list"}, repository_url=REPO_URL, source_commit=COMMIT)
check("an email in a curated description is redacted, not fatal",
      build_named_releases(
          {"releases": [{**curated["releases"][0], "description": "ping ops@example.com"}]},
          repository_url=REPO_URL, source_commit=COMMIT,
      )[0]["description"] == "ping [redacted]")


# ---------------------------------------------------------------------------
# Build section: reuse existing validation, never re-derive it
# ---------------------------------------------------------------------------
build_info = {
    "commit": COMMIT,
    "architecture": "x86_64",
    "artifactFormat": "raw BIOS disk image",
    "sha256": "b" * 64,
    "builtAt": "2026-09-17T11:00:00Z",
    "browserEmulator": "QEMU Wasm x86_64",
    "bootable": True,
    "browserCompatible": True,
    "bootSuccessMarkerImplemented": True,
    "validationOutcome": "browser-bootable",
    "blockers": [],
}
evidence = {
    "browser": "Chromium 140",
    "testedAt": "2026-09-17T11:30:00.000Z",
    "checks": ["exact-marker", "vga-text-memory"],
    "runtime": "ktock/qemu-wasm",
    "runtimeCommit": "c" * 40,
}
build = build_build_section(build_info, evidence, True, COMMIT)
check("bootable mirrors the build output", build["bootable"] is True)
check("browserCompatible mirrors the validation output", build["browserCompatible"] is True)
check("bootSmokePassed mirrors the workflow result", build["bootSmokePassed"] is True)
check("build.sourceCommit is the deployed SHA", build["sourceCommit"] == COMMIT)
check("browser validation subset is reused", build["browserValidation"]["browser"] == "Chromium 140")
check("timestamps are normalised", build["browserValidation"]["testedAt"] == "2026-09-17T11:30:00Z")
check("blocked build reports blocked state",
      build_build_section({**build_info, "browserCompatible": False,
                           "validationOutcome": "browser-runtime-blocked"},
                          None, False, COMMIT)["browserCompatible"] is False)
check("schema-1 manifests fall back to v86Compatible",
      build_build_section({**{k: v for k, v in build_info.items() if k != "browserCompatible"},
                           "v86Compatible": False},
                          None, False, COMMIT)["browserCompatible"] is False)
expect_error("a build from another commit is rejected",
             build_build_section, {**build_info, "commit": "d" * 40}, evidence, True, COMMIT)
expect_error("non-boolean bootable is rejected",
             build_build_section, {**build_info, "bootable": "true"}, evidence, True, COMMIT)

lab = build_lab_section(build_info, True, True)
check("lab describes the emulated browser kernel",
      lab["type"] == "browser-kernel" and lab["architecture"] == "x86_64")
check("lab never claims physical hardware drivers", lab["physicalHardwareDrivers"] is False)
check("lab is published when gates pass in a publish context", lab["published"] is True)
check("lab is unpublished outside a publish context",
      build_lab_section(build_info, True, False)["published"] is False)
check("lab is unpublished when a gate fails",
      build_lab_section(build_info, False, True)["published"] is False)


# ---------------------------------------------------------------------------
# Document validation guards
# ---------------------------------------------------------------------------
document = {
    "schemaVersion": 1,
    "generatedAt": "2026-09-17T12:00:00Z",
    "sourceCommit": COMMIT,
    "repository": build_repository(repo_payload),
    "languages": languages,
    "build": build,
    "lab": lab,
    "timeline": ordered,
    "releases": named,
}
check("a well-formed document validates", validate_metadata(document) is document)
expect_error("wrong schemaVersion rejected", validate_metadata, {**document, "schemaVersion": 2})
expect_error("missing schemaVersion rejected",
             validate_metadata, {k: v for k, v in document.items() if k != "schemaVersion"})
expect_error("short sourceCommit rejected", validate_metadata, {**document, "sourceCommit": "abc"})
expect_error("mismatched build.sourceCommit rejected",
             validate_metadata, {**document, "build": {**build, "sourceCommit": "e" * 40}})
expect_error("unsorted languages rejected",
             validate_metadata, {**document, "languages": list(reversed(languages))})
expect_error("oldest-first timeline rejected",
             validate_metadata, {**document, "timeline": list(reversed(ordered))})
expect_error("published without gates rejected", validate_metadata, {
    **document,
    "build": {**build, "bootSmokePassed": False},
})
expect_error("leaked email rejected", validate_metadata, {
    **document,
    "timeline": [{**ordered[0], "title": "Thanks to someone@example.com"}],
})
expect_error("leaked credential rejected", validate_metadata, {
    **document,
    "repository": {**document["repository"], "description": "token ghp_" + "A" * 36},
})
expect_error("unknown timeline type rejected", validate_metadata, {
    **document,
    "timeline": [{**ordered[0], "type": "commit"}],
})
expect_error("non-array releases rejected", validate_metadata, {**document, "releases": "nope"})
expect_error("duplicated release id rejected", validate_metadata, {
    **document,
    "releases": [named[0], {**named[0]}],
})
expect_error("malformed release startDate rejected", validate_metadata, {
    **document,
    "releases": [{**named[0], "startDate": "May 2026"}],
})
expect_error("newest-first release ordering enforced", validate_metadata, {
    **document,
    "releases": list(reversed(named)),
})
expect_error("a non-github release url is rejected at the document level", validate_metadata, {
    **document,
    "releases": [{**named[0], "url": "https://example.com/x"}],
})


# ---------------------------------------------------------------------------
# Atomic write: malformed metadata never replaces a valid file
# ---------------------------------------------------------------------------
with tempfile.TemporaryDirectory() as tmp:
    target = Path(tmp) / "browser-lab" / "project-metadata.json"
    write_metadata(document, target)
    check("metadata is written into the payload directory", target.exists())
    reloaded = json.loads(target.read_text(encoding="utf-8"))
    check("published JSON is syntactically valid and round-trips", reloaded == document)
    check("schemaVersion is present in the published file", reloaded["schemaVersion"] == 1)
    expect_error("malformed metadata is refused",
                 write_metadata, {**document, "schemaVersion": 99}, target)
    check("the previous valid file survives a refused write",
          json.loads(target.read_text(encoding="utf-8")) == document)
    check("no temporary files are left behind",
          sorted(item.name for item in target.parent.iterdir()) == ["project-metadata.json"])


# ---------------------------------------------------------------------------
# Transport behaviour: pagination, rate limits, hard failures
# ---------------------------------------------------------------------------
class FakeTransport:
    def __init__(self, routes):
        self.routes = routes
        self.calls = []

    def __call__(self, url, headers, timeout=30):
        self.calls.append(url)
        path = url.split("api.invalid", 1)[-1]
        for prefix, response in self.routes.items():
            if path.startswith(prefix):
                if callable(response):
                    return response(path)
                return response
        return 404, {}, b'{"message":"Not Found"}'


def ok(payload):
    return 200, {"Content-Type": "application/json"}, json.dumps(payload).encode("utf-8")


paged = FakeTransport({
    "/paged": lambda path: ok(
        [{"n": index} for index in range(100)] if "&page=1" in path else [{"n": 100}]
    ),
})
client = GitHubClient(api_base="https://api.invalid", transport=paged, sleep=lambda _: None)
check("pagination walks pages until a short page", len(client.paginate("/paged")) == 101)
check("pagination stops after the short page", len(paged.calls) == 2)

limited = GitHubClient(api_base="https://api.invalid", sleep=lambda _: None, transport=FakeTransport({
    "/repos": (403, {"X-RateLimit-Remaining": "0", "X-RateLimit-Reset": "1780000000"}, b"{}"),
}))
expect_error("rate limiting fails clearly", limited.get, "/repos/x/y")

flaky_calls = []


def flaky(url, headers, timeout=30):
    flaky_calls.append(url)
    if len(flaky_calls) < 3:
        return 502, {}, b"bad gateway"
    return ok({"ok": True})


retrying = GitHubClient(api_base="https://api.invalid", transport=flaky, sleep=lambda _: None)
check("transient 5xx responses are retried", retrying.get("/repos/x/y") == {"ok": True})

# Transport-level failures (DNS, reset, timeout) must retry, not kill the deploy.
transport_calls = []


def flaky_transport(url, headers, timeout=30):
    transport_calls.append(url)
    if len(transport_calls) < 3:
        raise TransportError("temporary failure in name resolution")
    return ok({"ok": True})


recovered = GitHubClient(api_base="https://api.invalid", transport=flaky_transport,
                         sleep=lambda _: None)
check("a transient transport failure is retried", recovered.get("/repos/x/y") == {"ok": True})
check("the transport retry stays inside the attempt budget", len(transport_calls) == 3)

always_down = GitHubClient(
    api_base="https://api.invalid", sleep=lambda _: None,
    transport=lambda url, headers, timeout=30: (_ for _ in ()).throw(TransportError("down")))
expect_error("a persistent transport failure is still fatal", always_down.get, "/repos/x/y")

# A secondary rate limit answers 403/429 with Retry-After and an untouched budget.
secondary = []


def secondary_limited(url, headers, timeout=30):
    secondary.append(url)
    if len(secondary) < 2:
        return 403, {"Retry-After": "1", "X-RateLimit-Remaining": "4999"}, b"{}"
    return ok({"ok": True})


slept = []
retrying_403 = GitHubClient(api_base="https://api.invalid", transport=secondary_limited,
                            sleep=slept.append)
check("a 403 secondary rate limit is retried", retrying_403.get("/repos/x/y") == {"ok": True})
check("Retry-After drives the delay", slept == [1])

slept.clear()
retry_after_429 = GitHubClient(api_base="https://api.invalid", sleep=slept.append,
                               transport=FakeTransport({
                                   "/a": (429, {"Retry-After": "2",
                                                "X-RateLimit-Remaining": "10"}, b"{}"),
                               }))
expect_error("a persistent 429 is still fatal", retry_after_429.get, "/a")
check("Retry-After is honoured for 429 too", slept == [2, 2])

expect_error("an over-long Retry-After fails clearly rather than hanging",
             GitHubClient(api_base="https://api.invalid", sleep=lambda _: None,
                          max_retry_after=30,
                          transport=FakeTransport({
                              "/a": (403, {"Retry-After": "600",
                                           "X-RateLimit-Remaining": "10"}, b"{}"),
                          })).get, "/a")

# Primary exhaustion still fails immediately rather than waiting out the window.
slept.clear()
exhausted = GitHubClient(api_base="https://api.invalid", sleep=slept.append,
                         transport=FakeTransport({
                             "/a": (403, {"Retry-After": "1",
                                          "X-RateLimit-Remaining": "0",
                                          "X-RateLimit-Reset": "1780000000"}, b"{}"),
                         }))
expect_error("primary exhaustion is not retried", exhausted.get, "/a")
check("primary exhaustion never sleeps", slept == [])

# Token handling: never over cleartext, never across a redirect.
expect_error("a non-HTTP(S) api base is rejected",
             GitHubClient, api_base="file:///etc/passwd")
expect_error("a token is refused over plain HTTP",
             GitHubClient, api_base="http://api.invalid", token="secret")
check("an unauthenticated HTTP base is still allowed",
      GitHubClient(api_base="http://api.invalid").token is None)

failing = GitHubClient(api_base="https://api.invalid", sleep=lambda _: None, transport=FakeTransport({}))
expect_error("a required request failure is fatal", failing.get, "/repos/x/y")

bad_json = GitHubClient(api_base="https://api.invalid", sleep=lambda _: None, transport=FakeTransport({
    "/repos": (200, {}, b"not json"),
}))
expect_error("non-JSON responses are fatal", bad_json.get, "/repos/x/y")

wrong_type = GitHubClient(api_base="https://api.invalid", sleep=lambda _: None, transport=FakeTransport({
    "/repos": ok({"not": "a list"}),
}))
expect_error("a non-array paginated response is fatal", wrong_type.paginate, "/repos/x/y/pulls")


# ---------------------------------------------------------------------------
# End-to-end generation against a fake GitHub
# ---------------------------------------------------------------------------
fake_github = FakeTransport({
    "/repos/BPForbes/Bailey-Forbes-Flinstone/languages": ok({"C": 800, "Python": 200}),
    "/repos/BPForbes/Bailey-Forbes-Flinstone/pulls/360/commits": ok(agent_commits),
    "/repos/BPForbes/Bailey-Forbes-Flinstone/pulls/354/commits": ok([]),
    "/repos/BPForbes/Bailey-Forbes-Flinstone/pulls/200/commits": ok([]),
    # Get a pull request: the only place merged_by is available.
    "/repos/BPForbes/Bailey-Forbes-Flinstone/pulls/200": ok(
        {**bot_dep, "merged_by": {"login": "dependabot[bot]", "type": "Bot"}}),
    "/repos/BPForbes/Bailey-Forbes-Flinstone/pulls": ok([merged, unmerged, agent_pull, bot_dep]),
    "/repos/BPForbes/Bailey-Forbes-Flinstone/releases": ok([]),
    "/repos/BPForbes/Bailey-Forbes-Flinstone": ok(repo_payload),
})

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    (root / "build-info.json").write_text(json.dumps(build_info), encoding="utf-8")
    (root / "browser-validation.json").write_text(json.dumps(evidence), encoding="utf-8")
    (root / "releases.json").write_text(json.dumps(curated), encoding="utf-8")
    args = argparse.Namespace(
        repository="BPForbes/Bailey-Forbes-Flinstone",
        source_commit=COMMIT,
        build_info=str(root / "build-info.json"),
        browser_validation=str(root / "browser-validation.json"),
        boot_smoke_passed="true",
        publish_context=True,
        releases=str(root / "releases.json"),
        output=str(root / "browser-lab" / "project-metadata.json"),
        max_timeline=25,
        max_pull_pages=1,
        max_pull_lookups=100,
        skip_commit_authors=False,
        api_base="https://api.invalid",
        token=None,
    )
    client = GitHubClient(api_base="https://api.invalid", transport=fake_github, sleep=lambda _: None)
    produced = generate(args, client=client, now=datetime(2026, 9, 17, 12, tzinfo=timezone.utc))
    write_metadata(produced, args.output)
    check("end-to-end schemaVersion", produced["schemaVersion"] == 1)
    check("end-to-end sourceCommit is the deployed SHA", produced["sourceCommit"] == COMMIT)
    check("end-to-end languages come from Linguist bytes",
          produced["languages"] == [
              {"name": "C", "bytes": 800, "percentage": 80.0},
              {"name": "Python", "bytes": 200, "percentage": 20.0},
          ])
    numbers = [entry.get("number") for entry in produced["timeline"]]
    check("end-to-end timeline keeps merged work", numbers == [354, 360])
    check("end-to-end timeline drops unmerged and bot-only chores",
          353 not in numbers and 200 not in numbers)
    check("end-to-end hydrates merged_by from the single-pull endpoint",
          any(call.endswith("/pulls/200") for call in fake_github.calls))
    check("end-to-end timeline keeps agent/human co-authorship",
          produced["timeline"][1]["coAuthors"] == ["BPForbes", "Ada Lovelace"])
    check("end-to-end tolerates zero releases",
          all(entry["type"] == "pull_request" for entry in produced["timeline"]))
    check("end-to-end build state is reused verbatim",
          produced["build"]["bootable"] is True
          and produced["build"]["browserCompatible"] is True
          and produced["build"]["bootSmokePassed"] is True)
    check("end-to-end lab is published", produced["lab"]["published"] is True)
    check("end-to-end output lands in the payload directory",
          Path(args.output).name == "project-metadata.json")
    check("end-to-end document has no email addresses", "@users.noreply" not in json.dumps(produced))
    check("end-to-end publishes the curated named releases",
          [entry["version"] for entry in produced["releases"]] == ["4.0.0 / 4.0.1", "3.3.0"])
    check("end-to-end release url defaults to the deployed commit's version/locked",
          produced["releases"][0]["url"]
          == "https://github.com/BPForbes/Bailey-Forbes-Flinstone/tree/"
             f"{COMMIT}/version/locked")

    args.releases = str(root / "missing-releases.json")
    no_curation = generate(args, client=client, now=datetime(2026, 9, 17, 12, tzinfo=timezone.utc))
    check("a missing curated-releases file publishes zero, not an error",
          no_curation["releases"] == [])
    args.releases = str(root / "releases.json")

    args.boot_smoke_passed = ""
    expect_error("an unstated boot smoke result is fatal", generate, args, client=client)
    args.boot_smoke_passed = "false"
    degraded = generate(args, client=client, now=datetime(2026, 9, 17, 12, tzinfo=timezone.utc))
    check("a failed boot smoke never publishes the lab", degraded["lab"]["published"] is False)
    args.source_commit = "f" * 39
    expect_error("a non-SHA source commit is fatal", generate, args, client=client)

if failures:
    for failure in failures:
        print(f"test_generate_project_metadata: FAIL {failure}", file=sys.stderr)
    raise SystemExit(1)
print("test_generate_project_metadata: PASS")

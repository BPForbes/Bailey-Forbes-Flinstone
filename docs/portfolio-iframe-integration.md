# Portfolio iframe integration

This is the Flintstone-side contract for embedding the lab on
`https://bailey-forbes.com`. The portfolio implementation stays in the
portfolio repository.

## Permanent lab URL

Configured GitHub Pages form after a successful `main` deploy of this
workflow:

`https://bpforbes.github.io/Bailey-Forbes-Flinstone/`

This pull request does not publish that URL. Publication happens only when
`browser-kernel-artifact.yml` validates native QEMU and Chromium observations
for the same image and commit, then the `deploy` job runs on `main`. A failed
build leaves the previously published Pages tree in place.

Repository setting required once: **Pages → Source = GitHub Actions**, and the
`github-pages` environment must allow the workflow.

## iframe markup

```html
<iframe
  id="flintstone-lab"
  title="Flintstone Kernel Lab"
  src="https://bpforbes.github.io/Bailey-Forbes-Flinstone/"
  allow="cross-origin-isolated"
  referrerpolicy="strict-origin"
  style="width:100%;min-height:48rem;border:0">
</iframe>
```

Adding an iframe tag is not enough. QEMU WebAssembly needs
`SharedArrayBuffer`, which requires cross-origin isolation in the **child**
and a parent that delegates `cross-origin-isolated`. The lab service worker
cannot isolate `bailey-forbes.com`.

## Hosting headers

### Parent (`https://bailey-forbes.com`)

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: credentialless
Permissions-Policy: cross-origin-isolated=(self "https://bpforbes.github.io")
```

`credentialless` is usually less disruptive than `require-corp` on a marketing
site. If third-party scripts break, that is a portfolio-origin change, not a
Flintstone change.

### Child (lab origin)

GitHub Pages cannot set COOP, COEP, CSP, or Permissions-Policy. The packaged
lab therefore:

- ships `coi-serviceworker.js` so a **top-level** first visit reloads once the
  worker controls the lab origin. Top-level visits receive COEP and COOP.
  Framed visits receive COEP only: adding `COOP: same-origin` on an iframe
  reload makes Chromium replace the child with `chrome-error://chromewebdata/`.
- A parent that is already cross-origin isolated (`COEP: credentialless` or
  `require-corp`) refuses the first headerless document with
  `net::ERR_BLOCKED_BY_RESPONSE`. The service worker therefore cannot install
  on a first-visit iframe from `bailey-forbes.com`. After a top-level Pages
  visit in the same browser, the worker injects COEP and the iframe can boot.
  Nested `SharedArrayBuffer` still needs the parent headers below plus
  `allow="cross-origin-isolated"`. The worker cannot isolate the parent.
- ships `_headers` for Cloudflare/Netlify if the lab is later placed behind a
  host that honors them. Prefer those native child headers when the portfolio
  iframe must boot on the visitor's first load with no prior Pages visit.

When headers are available, use:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: cross-origin
Content-Security-Policy: frame-ancestors https://bailey-forbes.com
Cache-Control: no-store
```

`build-info.json` should not be cached. The disk URL already includes
`?v=<shortCommit>`.

## Readiness message

The guest posts to `https://bailey-forbes.com` only:

```json
{
  "source": "flinstone-guest",
  "type": "ready",
  "schemaVersion": 1,
  "commit": "<shortCommit>"
}
```

Parent validation:

```js
function isTrustedReady(event, { labOrigin, iframe, commit }) {
  const data = event.data;
  return event.origin === labOrigin &&
    event.source === iframe.contentWindow &&
    data && data.source === "flinstone-guest" &&
    data.type === "ready" &&
    data.schemaVersion === 1 &&
    data.commit === commit;
}
```

`commit` is `build-info.json` `shortCommit` for the published image.

## Browser and guest capabilities

- Chromium-family browser, HTTPS (or localhost), `SharedArrayBuffer`
- Guest: long-mode boot marker, VGA diagnostic cell `F`, PS/2 keyboard, lab
  identity (`login` / `su` / `logout` / `whoami` / `useradd`), ramfs
  (`dir` / `cat` / `write` / `mkdir`), `server` via the browser relay, up to
  four concurrent sessions
- Lab seeds: `flinstone` / `flinstone` and `root` / `root` (lab-only, not the
  hosted SQLite store)
- **Not** in this image: hosted FAT32, P3 sockets / `kernel/core/net`
  `server host/join`, SQLite accounts, or the hosted ELF shell

## How `main` updates reach the lab

1. Push to `main` runs native QEMU and Chromium tests on the same commit.
2. The workflow packages `dist/browser-lab/` (runtime, BIOS, disk, manifest,
   evidence) with relative `./artifacts/` paths.
3. Only then does Pages deploy replace the published tree.
4. The iframe `src` stays the same. The portfolio does not need a routine
   URL change.

## One-time portfolio repository changes

1. Insert the iframe with `allow="cross-origin-isolated"`.
2. Emit the parent COOP/COEP/Permissions-Policy headers.
3. Listen for `message` and accept only the trusted ready payload above.
4. Do not rely on the lab service worker to isolate the parent origin.
   The parent must send COOP/COEP (`credentialless` is the documented
   portfolio COEP) and delegate `cross-origin-isolated`. A first-visit
   iframe of the GitHub Pages lab will not boot until the visitor has
   opened the Pages URL top-level once, or until the lab is served with
   native child COOP/COEP.

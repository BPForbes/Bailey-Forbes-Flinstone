# GitHub Pages (static hosting)

GitHub Pages does **not** send `Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy`, so the browser will **not** expose `SharedArrayBuffer`. The default `make wasm` build uses **pthreads** and needs those headers (use `make wasm-serve` locally, or another host that sets COOP+COEP).

For **GitHub Pages only**, build the **single-threaded** profile:

```bash
make wasm-pages
```

Then publish the contents of `wasm/` that are needed for the page:

- `index.html` (loads the ES module factory from `BPForbes_Flinstone_Shell.js` and passes `globalThis.Module` from `wasm/pre.js` for argv / print hooks)
- `BPForbes_Flinstone_Shell.js`
- `BPForbes_Flinstone_Shell.wasm`

No `.worker.js` is produced for this profile.

CI: the workflow `.github/workflows/pages-wasm.yml` runs `make wasm-pages` and deploys via **Actions → Deploy to GitHub Pages** (configure **Settings → Pages → Build and deployment: GitHub Actions**).

If `https://<user>.github.io/<repo>/` shows only the README, Pages is still **Deploy from a branch** (the `.js`/`.wasm` outputs are gitignored). Switch to **GitHub Actions** and run the workflow above. When serving from a branch, open **`wasm/index.html`** (or rely on the repo root **`index.html`** redirect).

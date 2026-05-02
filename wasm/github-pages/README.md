# GitHub Pages (static hosting)

GitHub Pages does **not** send `Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy`, so the browser will **not** expose `SharedArrayBuffer`. The default `make wasm` build uses **pthreads** and needs those headers (use `make wasm-serve` locally, or another host that sets COOP+COEP).

For **GitHub Pages only**, build the **single-threaded** profile:

```bash
make wasm-pages
```

Then publish the contents of `wasm/` that are needed for the page:

- `index.html` (loads the module)
- `BPForbes_Flinstone_Shell.js`
- `BPForbes_Flinstone_Shell.wasm`

No `.worker.js` is produced for this profile.

CI: the workflow `.github/workflows/pages-wasm.yml` runs `make wasm-pages` and deploys via **Actions → Deploy to GitHub Pages** (configure **Settings → Pages → Build and deployment: GitHub Actions**).

# Headless capture scripts

Puppeteer scripts that load a deployed (or local) build in headless Chrome with SwiftShader,
wait for the WASM module to come up, and save a PNG plus a `.log.txt` of the page console,
page errors and failed requests. They are the quickest way to see what the staged loader did
on a machine with no GPU (CI, Colab, a container). For deterministic frame-by-frame capture
use the in-app timeline capture instead (`web/src/capture.ts`, ARCHITECTURE §11.2).

| Script | What it does |
|---|---|
| `screenshot.mjs` | Load, wait 20 s, screenshot. |
| `screenshot-pose.mjs` | As above, then `window.setCameraPose(1850, 0, 0, 0, 0)` to teleport next to Earth so the staged loader initialises Earth, wait 15 s, screenshot. |
| `screenshot-intercept.mjs` | As `screenshot.mjs`, but aborts the requests for the very large skybox, star-spectrum and flare textures (to avoid a SwiftShader out-of-memory) and lists them in the log. |

## Running

`puppeteer` is the only dependency and is resolved from the nearest `node_modules` above this
directory, so install it at the repository root (the root `package.json` declares it):

```bash
npm install
```

```bash
node scripts/headless/screenshot.mjs [url] [out.png]
```

- `url` defaults to `https://test.1ink.us/solar-system/index.html`. For a local build use the
  Vite preview URL, e.g. `http://127.0.0.1:4173/solar-system/`.
- `out.png` defaults to `headless_chrome/<script>.png` at the repository root. That directory
  is git-ignored; nothing here should be committed.
- `PUPPETEER_EXECUTABLE_PATH` selects the Chrome binary (default `/usr/bin/google-chrome`).

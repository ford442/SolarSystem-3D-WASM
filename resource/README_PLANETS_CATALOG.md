# Planet metadata catalog

**Canonical source:** [`planets.catalog.json`](planets.catalog.json)  
**Schema:** [`planets.catalog.schema.json`](planets.catalog.schema.json)

## Generate

```bash
# From repo root
node scripts/generate-planet-metadata.mjs

# Or from web/
npm run generate:planet-metadata

# CI drift check
npm run generate:planet-metadata:check
```

`build-web.sh` and `web` `prebuild` run generation automatically.

## Outputs (do not hand-edit)

| File | Consumer |
|------|----------|
| `web/public/planet_facts.json` | Explorer panel (`planetExplorer.ts`) |
| `web/threejs/src/data/orbital-parameters.json` | Three.js companion |
| `resource/planet_manifest.json` | WASM staged loading |

## Asset checksums

```bash
node scripts/generate-planet-metadata.mjs --write-checksums
# or during deploy:
python3 web/deploy.py assets --update-manifest --dry-run …
```

## Focus indices

`Sun=0 … Pluto=9` — keep in lockstep with `OrbitLayout::Body` and focus APIs.

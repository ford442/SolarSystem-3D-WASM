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
| `web/public/planet_facts.json` | Explorer panel (`planetExplorer.ts`) — focus bodies only (Sun=0 … Vesta=11) |
| `web/threejs/src/data/orbital-parameters.json` | Three.js companion |
| `resource/planet_manifest.json` | WASM staged loading |
| `src/Solar_System/OrbitLayoutBodies.generated.inc` | Heliocentric orbit table |
| `src/Solar_System/BodyCatalog.generated.h` | CatalogBody / CatalogSatellite / CatalogClouds rows (includes moons) |

## Asset checksums

```bash
node scripts/generate-planet-metadata.mjs --write-checksums
# or during deploy:
python3 web/deploy.py assets --update-manifest --dry-run …
```

## Focus indices

`Sun=0 … Vesta=11` — keep in lockstep with `OrbitLayout::Body` and focus APIs. Satellites occupy catalog indices 12+ and are not focusable.

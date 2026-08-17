# Mid-resolution textures (LOD tier)

Intermediate DDS maps used by the three-tier texture LOD system on the web build:

| Tier | Directory | Typical size | When loaded |
|------|-----------|--------------|-------------|
| Low | `textures_low/` | 4×4 placeholders (git) or ≤512² | Staged load / default resident |
| **Mid** | `textures_mid/` | ≤1024² diffuse, ≤512² normal/specular | Medium + full presets when close |
| High | `textures/` | Full web cap (≤8192²) | Full preset only, when very close |

## Naming

`Base.dds` → `Base_Mid.dds`  
Examples: `Earth_Day_Diffuse_Mid.dds`, `Moon_Diffuse_Mid.dds`.

Paths are declared in `src/Solar_System/TexturePaths.h`.

## Generating real mid assets

With a full-res library and Microsoft texconv:

```bash
python3 scripts/generate_web_textures.py \
  --input-dir /path/to/full-res/resource/textures \
  --output-dir resource/textures_web \
  --mid-res-dir resource/textures_mid \
  --mid-res-max-size 1024 \
  --manifest-only
```

Repo commits only small placeholder mid DDS (copied from low) so local path resolution works. Real mid art is a **deploy artifact** (CDN / `deploy.py assets`), same as high-res.

## Quality presets

- **low** — never leaves low
- **medium** — upgrades low → mid only (no full 8K)
- **full** — low → mid → high with distance hysteresis

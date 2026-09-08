# Low-Resolution Textures Directory

This directory contains low-resolution textures for the Level-of-Detail (LOD) system.

## Purpose

When running in WebAssembly mode, the application initially loads low-resolution textures to minimize initial download size.  When the camera zooms close to a planet, high-resolution textures are streamed on-demand using the `TextureLoadingQueue` / `WebResourceFetcher`.

The staged loading system distinguishes between:
- **Required assets** — planet surface textures in this directory. Init is blocked until all required assets are downloaded.
- **Optional assets** — moon, ring, and cloud textures fetched from the remote server. Init is **not** blocked on these; failures show a 4×4 checker fallback without halting startup.

## Naming Convention

Low-resolution texture files follow this naming pattern:
- Format: `{PlanetName}_{TextureType}_Low.dds`
- Example: `Earth_Day_Diffuse_Low.dds`

## Current Low-Resolution Assets (Required)

| Planet  | Diffuse | Normal | Specular |
|---------|---------|--------|----------|
| Mercury | ✓ | ✓ | ✓ |
| Venus   | ✓ | ✓ | ✓ |
| Earth   | ✓ | ✓ | ✓ |
| Mars    | ✓ | ✓ | ✓ |
| Jupiter | ✓ | ✓ | ✓ |
| Saturn  | ✓ | ✓ | ✓ |
| Uranus  | ✓ | ✓ | ✓ |
| Neptune | ✓ | ✓ | ✓ |
| Pluto   | ✓ | ✓ | ✓ |

## Moon / Ring / Cloud Textures (Optional — server-side)

Moon, ring, and cloud textures (e.g. `Europa_Diffuse.dds`, `Saturn_Ring.dds`,
`Jupiter_Cloud.dds`) are **not** bundled in this repository. They are fetched
from the remote asset server at runtime and treated as optional: if they are
unavailable the body renders with a 4×4 checker fallback.

### Recommended specifications for optional low-res fallbacks

When preparing these textures for the server, prefer:
- **Resolution**: 512 × 512 px (or 256 × 256 for small moons / rings)
- **Format**: DXT5 (BC3) DDS — good colour + alpha compression, broad GPU support
- **Mipmaps**: include a full mip chain (`-mipmap` flag in `nvcompress` / `texconv`)

This allows the optional files to load quickly over bandwidth-limited connections
while still providing acceptable visual quality for close-up views.

### Example conversion with `nvcompress`

```bash
nvcompress -bc3 -mipmap Europa_Diffuse.png Europa_Diffuse.dds
```

Or with Microsoft `texconv`:

```bash
texconv -f BC3_UNORM -m 0 Europa_Diffuse.png
```

## Creating New Low-Resolution Planet Textures

To create low-resolution versions of planet surface textures:
1. Take the high-resolution `.dds` texture from `resource/textures/`
2. Scale it down to 25–50 % of original dimensions
3. Re-compress as DXT5 with full mip chain
4. Name it `{PlanetName}_{TextureType}_Low.dds` and place it in this directory

## Extending the LOD System to a New Body

1. Create low-res textures for the body (see above)
2. Add the texture paths to the body's `PlanetSystemManifest::assetPaths` in `src/Application.cpp`
3. Override `LoadHighResIfClose()` in the planet class following the existing pattern
4. Add optional moon/ring paths to `PlanetSystemManifest::optionalAssetPaths` (fire-and-forget, no init blocking)

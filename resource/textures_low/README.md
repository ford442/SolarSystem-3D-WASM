# Low-Resolution Textures Directory

This directory contains low-resolution textures for the Level-of-Detail (LOD) system.

## Purpose

When running in WebAssembly mode, the application initially loads low-resolution textures to minimize initial download size. When the camera zooms close to a planet (distance < 50 units), high-resolution textures are loaded on-demand using the WebResourceFetcher.

## Naming Convention

Low-resolution texture files should follow this naming pattern:
- Format: `{PlanetName}_{TextureType}_Low.dds`
- Example: `Earth_Day_Diffuse_Low.dds`

## Required Files for Earth

The LOD system currently requires these low-resolution textures for Earth:
- `Earth_Day_Diffuse_Low.dds` - Low-res day texture
- `Earth_Normal_Low.dds` - Low-res normal map
- `Earth_Specular_Low.dds` - Low-res specular map

High-resolution versions should be placed in `resource/textures/`:
- `Earth_Day_Diffuse.dds`
- `Earth_Normal.dds`
- `Earth_Specular.dds`

## Creating Low-Resolution Textures

To create low-resolution versions:
1. Take the high-resolution `.dds` texture from `resource/textures/`
2. Scale it down (e.g., to 25-50% of original dimensions)
3. Save with `_Low.dds` suffix
4. Place in this directory

## Future Extensions

The LOD system can be extended to other planets by:
1. Creating low-res textures for the planet
2. Updating the planet's class to override `LoadHighResIfClose()`
3. Following the same pattern as implemented for Earth

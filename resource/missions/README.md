Sampled heliocentric probe trajectories for the educational overlay.

Positions are scene-axis AU (ecliptic x → X, ecliptic z → Y, ecliptic y → Z), matching
`OrbitLayout::GetOffset`. They are baked offline from Standish planet locations at
published encounter dates plus a smooth outbound ray — not SPICE and not live TLE.

Regenerate after editing the baker:

```bash
node scripts/generate-mission-samples.mjs
```

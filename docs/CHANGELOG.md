# Changelog

## v3.1.2 — 2026-09-29

- Fixed incomplete mixed CJK/Latin text vertices and Bezier handle visualization.
- Fixed current-frame motion-frame growth and motion-path occlusion.
- Improved multi-shape-layer motion/occluder association.
- Added recursive precomp shape/text/footage drill-down.
- Hardened mask-outline reads against partial/invalid vertex data.
- Added adjacency lookup for complex mask silhouettes.
- Avoided key-vertex and motion sampling work when those overlays are disabled.
- Reduced per-frame heap allocation and pixel-outline row lookup overhead.
- Rebuilt as macOS arm64, PiPL eVER `0x189601`.
- Added a Windows x64 PE32+ `.aex` cross-build with PiPL resource ID 16000 and `EffectMain` export; runtime AE validation remains pending.

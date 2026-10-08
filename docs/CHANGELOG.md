# Changelog

## v0.1.1 — 2026-10-08

- Replace the single-architecture macOS artifact with a universal `arm64` + `x86_64` bundle; add matching `ma64` and `mi64` PiPL entry points.
- Preserve the exact `PiPL` PE resource name; the old MinGW resource object had uppercased it to `PIPL`. This was a loading-compatibility risk, not a proven crash root cause.
- Prevent GUI render workers from making direct AEGP geometry queries when no frame cache exists. Windows no longer assumes every frame-setup callback is on the host setup thread; preserve the basic-frame/pixel fallback on skipped geometry.
- Guard a failed vertex-scoring allocation instead of dereferencing a null pointer.
- Bump source, PiPL and metadata versions to `0.1.1` (`0x00008E01`) without changing the effect match name or persisted parameter IDs.
- Add complete PE/PiPL validation, Windows OS-loader smoke script, deterministic packages and installation/compatibility notes.
- AE 26.5 on Apple Silicon rendered a one-frame test. Windows AE 23–26 and older/macOS Intel host versions still require real-host validation. The reported MediaCore scan crash does not include a module-specific dump, so a single root cause cannot be proved from that screenshot.

## v0.1.0 — 2026-10-08 (superseded)

- Initial unified `EdgeViz` macOS arm64 and Windows x64 artifacts. The Windows package was static-checked but not AE-host tested and was reported to crash during plug-in scanning.

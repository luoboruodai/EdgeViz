## EdgeViz v0.1.1 — 2026-10-08

**Supersedes v0.1.0**, especially its Windows x64 build. The effect match name `com.edgeviz.outline` and serialized parameter IDs are unchanged.

### Changes

- Windows: preserve the exact `PiPL` resource type name (the previous MinGW resource used `PIPL`); validate the complete PE/PiPL, `8664` entry, resource ID 16000, `EffectMain` export, file version and imports.
- Windows/macOS: avoid AEGP geometry calls on unidentified GUI render-worker threads; fall back safely when no geometry cache is available. Guard failed vertex-score allocation.
- macOS: universal `arm64 + x86_64` plug-in with both PiPL entry points. The v0.1.0 Mac package was arm64 only.

### Validation / important limitations

The macOS universal bundle passed architecture, PiPL, signature and one-frame render checks on **Apple Silicon / AE 26.5**. Windows x64 passed **static PE and resource checks only**; there was no Windows AE 23–26 host available for installation, effect or render testing. Mac Intel and AE 23–25 were also not host-tested. This is a compatibility-targeted build, **not a certification of every AE 23–26 configuration**. The MediaCore scan screenshot lacks a module-specific crash dump and does not establish a unique root cause.

The Windows binary is unsigned and was cross-built with MinGW-w64/older CS6-compatible headers. Validate on real Windows AE hosts and ideally rebuild with a recent Adobe SDK and Visual Studio before broad deployment. The macOS bundle is ad-hoc signed but not notarized.

### Install / recover

Quit After Effects and remove older `EdgeViz` or `EdgeViz Outline` copies before installing the one platform-specific plug-in. Do not keep duplicates with the same effect match name. If AE still crashes during plug-in scanning, remove `EdgeViz.aex` and provide AE/Windows versions and the crash dump. See the bilingual README for paths and detailed checks.

### Assets

- `EdgeViz-v0.1.1-macOS-universal.zip`
- `EdgeViz-v0.1.1-Windows-x64.zip`
- `SHA256SUMS.txt` — checksums for the two ZIP downloads.

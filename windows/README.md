# EdgeViz v0.1.0 Windows x64

Artifact: `artifacts/windows-x64/EdgeViz.aex`

- PE32+ DLL / x86-64
- PiPL resource type `PiPL`, resource ID 16000
- Windows x64 entry key `8664`
- Export: `EffectMain`
- File/product version: `0.1.0`

Build with:

```bash
AE_SDK_ROOT=/path/to/AfterEffectsSDK ./build_win.sh
```

The public source export excludes the Adobe SDK. The build host has verified PE format, x64 architecture, export table, PiPL bytes, version resource, resource ID, and imported DLLs. Windows After Effects runtime loading/rendering still requires a Windows test machine with the target AE version.

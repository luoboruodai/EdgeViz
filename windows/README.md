# EdgeViz v0.1.1 — Windows x64

`artifacts/windows-x64/EdgeViz.aex` is a PE32+ DLL exporting `EffectMain`. Its single `PiPL` resource has ID 16000, x64 entry key `8664`, version `0x00008E01`, and the unchanged effect match name `com.edgeviz.outline`.

## Regression matrix required for AE 23–26

| Host | Install/restart | Apply effect/parameter UI | Reopen v0.1.0 project | Render text/shape/precomp | Status |
|---|---|---|---|---|---|
| Windows x64 AE 23 | — | — | — | — | not run |
| Windows x64 AE 24 | — | — | — | — | not run |
| Windows x64 AE 25 | — | — | — | — | not run |
| Windows x64 AE 26 | — | — | — | — | not run |

Cross-build: `AE_SDK_ROOT=/path/to/AdobeSDK ./build_win.sh`. The script performs structural PE/PiPL checks. On a Windows x64 machine, run `python test/smoke_windows_load.py artifacts/windows-x64/EdgeViz.aex` first, then perform the AE matrix above. An OS loader check is **not** an AE rendering test.

The old MinGW resource had been normalized from `PiPL` to `PIPL`; the new build preserves the canonical case. A Windows host-loading test is still required to assess whether this contributed to the reported crash. The previous code ran AEGP geometry queries from an unidentified Windows thread. v0.1.1 restricts them in the GUI host to the thread observed at `PF_Cmd_GLOBAL_SETUP` (and uses a safe no-geometry fallback otherwise); `aerender` remains an exception. The screenshot shows only the MediaCore scan stage and does not prove that this was the sole cause of the reported crash. The current Windows binary is unsigned and built with MinGW-w64/CS6-compatible headers; validate or rebuild on the target Windows/AE toolchain before broad deployment.

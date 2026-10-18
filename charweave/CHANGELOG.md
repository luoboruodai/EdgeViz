# Changelog / 更新记录

## 0.4.4 beta 2 · 2026-10-18

### Fixed / 修复

- Windows GDI fonts and bitmaps are deselected before release, including failed paths.
- Flush queued GDI drawing before reading DIB pixels; do not read failed/unsynchronized drawing.
- Confirm glyph coverage before selecting a fallback font, rather than treating valid text metrics as proof of coverage.
- Reject whitespace-only text/font input on both platforms while preserving visible BMP text mixed with whitespace.

修复 Windows GDI 资源释放、DIB 同步和缺字回退；同步修复两平台空白输入校验。不改变 1–39 参数 ID、TCW1 数据布局、字符化风格和独立 Styles Manager 流程。

### Identity and verification / 身份与验证

- PF/PiPL `0x00022402`; macOS bundle build `40402`; effect `0.4.4x2`.
- Old implementation reproduced failures in 11 GDI-model cases; all 12 cases pass after the fix, including 1,000 repeated glyph renders and allocation/selection/drawing/flush failures. These are test cases, not 11 distinct product bugs.
- SDK-dependent ASan/UBSan renderer/style regressions and JSX logic-model tests pass locally.
- macOS universal bundle built and ad-hoc signature verified; local Apple Silicon AE 26.5x89 started with one registration after replacement and exported test frames without changing the saved test project.
- Windows x64 binary cross-built and statically checked; real Windows/AE testing is deferred, not passed.

### Preserved limitations / 保留限制

Native Create/Manage buttons remain disabled; use the standalone Styles Manager. Other AE/macOS versions, Intel runtime, full edit-dialog Undo/Redo, 32 bpc/HDR/GPU/MFR are not certified. No Developer ID/notarization or commercial Windows signing is claimed.

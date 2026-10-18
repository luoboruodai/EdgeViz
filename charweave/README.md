# CharWeave Native · 0.4.4 beta 2

**After Effects 的原生字符化效果插件**：用自定义 BMP 文本，或由形状、文字、图片、预合成组成的图层样式库，把素材转换为带调色和动态扩散的字符画。

**A native After Effects character-art effect** using custom BMP text or an 8×8 library of shape, text, image and precomposition styles, with color mapping and animated dispersion.

Repository: `luoboruodai/EdgeViz`, directory `charweave/`. CharWeave is a separate plugin, not an EdgeViz replacement. Release tag: **`charweave-v0.4.4-beta.2`**.

[Download this CharWeave beta](https://github.com/luoboruodai/EdgeViz/releases/tag/charweave-v0.4.4-beta.2) · [Frozen release notes](docs/RELEASE-0.4.4-beta.2.md)

## 安装 / Install

下载对应 Release 中的 **macOS / Windows 可安装 ZIP 附件**，不要把 GitHub 自动生成的 “Source code” 当作安装包。

Use the platform **installable ZIP asset** from the matching Release. GitHub's automatic “Source code” archives are for source review/building, not installation.

- [中文安装与使用指南](docs/INSTALL.zh-CN.md)
- [English installation and usage](docs/INSTALL.en.md)
- [变更与验证边界 / Changes and verification](CHANGELOG.md)
- [构建 / Building](docs/BUILD.md)
- [第三方声明 / Third-party notices](THIRD_PARTY_NOTICES.md)

## 本版变化 / What's changed

- Windows：修复 GDI 字体/位图释放、绘制到 DIB 的同步与缺字字体回退。
- 两平台：拒绝只有空格、Tab、回车或换行的文本/字体名，保留混合空白的可见 BMP 文本。
- 不改参数 ID 1–39、TCW1 数据布局、字符化风格、独立 Styles Manager 的使用方式。

Windows fixes cover GDI object lifetime, synchronized DIB reads, and missing-glyph fallback. Both platforms reject whitespace-only text/font data. Existing parameter IDs, the TCW1 format, rendering controls and the standalone Styles Manager workflow are preserved.

## 平台与证据 / Platforms and evidence

| Platform | Artifact | Verification |
|---|---|---|
| macOS | arm64 + x86_64 universal, ad-hoc signed | Local Apple Silicon build and ASan/UBSan regression; AE 26.5x89 startup/registration and test-frame export after replacement. Intel runtime and other AE/macOS versions not tested. |
| Windows | PE32+ x86-64, AE 2023–2026 candidate | Cross-compiled; PE/PiPL checks and 12 GDI behavior-model cases. Real Windows/AE startup, font pixels and rendering remain unverified. |

This is a **beta/pre-release**, not an all-version compatibility certification. Windows host acceptance is deferred, not passed. Native Create/Manage buttons remain disabled: use `CharWeave Styles Manager.jsx`. Full edit-window Undo/Redo, 32 bpc/HDR/GPU/MFR are not claimed as verified.

本版仍是候选版本。macOS 未 Developer ID 签名/公证；Windows 未商业代码签名。只使用你信任并核对 SHA-256 的包，不要全局关闭系统安全检查。

## 源码结构 / Source layout

- `CharWeave-mac/`: Objective-C++ effect, managed-library JSX and standalone engine tests.
- `CharWeave-windows/`: Windows x64 effect, GDI implementation/model tests and cross-build entry point.
- `tools/`: metadata/privacy/archive checks without Adobe SDK dependencies.

The Adobe SDK and Adobe resource templates are not redistributed. Test logs, AEP projects, media, local handoffs and machine-specific configuration are excluded. Compiler/runtime notices are provided separately; no new license is assigned to the project's own code by this release.

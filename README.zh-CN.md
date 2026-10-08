# EdgeViz After Effects 插件

[English](README.md) | **简体中文**

**版本：v0.1.0** · **PiPL：`0x00008601`** · **平台：macOS arm64 + Windows x64**
**更新日期：2026-10-08**

EdgeViz 是一款 Adobe After Effects 效果插件，用于可视化图层边框、文字字形轮廓、形状路径、贝塞尔顶点与手柄、蒙版剪影、运动路径以及嵌套预合成内容。

效果的匹配名称（match name）保持为 `com.edgeviz.outline`，以保证工程兼容性。面向用户的插件名称与交付文件名统一为 **EdgeViz**。

## 发布文件

- `artifacts/macos-arm64/EdgeViz.plugin` — macOS arm64 插件包。
- `artifacts/macos-arm64/EdgeViz-v0.1.0-macOS-arm64.zip` — macOS 安装包。
- `artifacts/windows-x64/EdgeViz.aex` — Windows x64 PE32+ 插件。
- `artifacts/windows-x64/EdgeViz-v0.1.0-Windows-x64.zip` — Windows 安装包。
- `SHA256SUMS.txt` — 发布文件的校验和。

## v0.1.0 亮点

- 完整的中英文混排文字顶点与贝塞尔手柄可视化。
- 零切线文字与参数化形状转角处的可见回退手柄。
- 运动残影从当前物体连同已走过的路径前缀一起生长。
- 运动路径绘制在运动物体下方，并按其当前遮挡区域裁剪。
- 同一形状图层上的多个形状拥有独立的运动/路径/遮挡数据。
- 递归深入预合成，覆盖形状、文字、素材边缘与嵌套预合成。
- 更安全的无效蒙版处理与更快的复杂剪影链式计算。
- 关闭对应叠加显示时，跳过关键顶点与运动采样计算。
- 点样式：圆形、方形、三角形、菱形、十字与自定义图层。
- 手柄长度、手柄大小、点大小、颜色与像素描边控制。

## macOS 安装

```bash
cp -R "EdgeViz.plugin" \
  "$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/"
```

安装后请完全退出并重新启动 After Effects。卸载方法：

```bash
rm -rf "$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/EdgeViz.plugin"
```

交付的 macOS 插件包为开发/测试用途的 ad-hoc 签名。正式发布需要适当的签名与公证（notarization）。

## 从源码构建

公开仓库有意不包含 Adobe SDK。请将 `AE_SDK_ROOT` 指向合法的 SDK 目录。

### macOS arm64

```bash
AE_SDK_ROOT=/path/to/AfterEffectsSDK ./build.sh
```

需要 Apple Clang、`Rez` 与 `codesign`。

### Windows x64

```bash
AE_SDK_ROOT=/path/to/AfterEffectsSDK ./build_win.sh
```

需要 Python 3 和 x86_64 MinGW-w64 工具链（`x86_64-w64-mingw32-g++`、`windres` 与 `strip`）。该脚本构建 PE32+ DLL，嵌入资源 ID 为 16000 的 `PiPL`（Windows x64 入口键 `8664`），并导出 `EffectMain`。

Windows 构建产物已通过 PE 格式、x64 架构、导出符号、PiPL、资源 ID、版本资源与依赖项检查。由于本 macOS 构建主机没有 Windows 版 After Effects,Windows 上的 AE 加载与渲染仍需在装有目标 AE 版本的 Windows 机器上验证。用于商业分发时，请使用匹配的 Visual Studio/Adobe SDK 工具链重新构建并对二进制文件签名。

## 测试

```bash
python3 test/check_params.py
python3 test/check_release.py
```

仓库仅包含经过脱敏处理的验证截图；不包含 `.aep` 工程、用户媒体、缓存、日志、SDK 文件或任何凭据。

## 仓库与发布

仓库：`luoboruodai/EdgeViz`
发布标签：`v0.1.0`

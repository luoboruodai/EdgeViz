# EdgeViz — After Effects 特效插件

[English](README.md) | **简体中文**

**v0.1.1** · 效果匹配名 `com.edgeviz.outline` · 目标 AE 23–26 · macOS Intel/Apple Silicon + Windows x64

EdgeViz 可显示图层边框、文字字形、形状路径、贝塞尔顶点及手柄、蒙版轮廓、运动路径和嵌套预合成几何。效果匹配名及 39 个持久化参数 ID 均保持不变，以延续旧工程的识别关系。

## 下载

- `EdgeViz-v0.1.1-macOS-universal.zip`：包含 arm64 和 x86_64 双架构 `EdgeViz.plugin`，开发用途的临时签名。
- `EdgeViz-v0.1.1-Windows-x64.zip`：包含 PE32+ x64 `EdgeViz.aex`。
- `SHA256SUMS.txt`：上述两个 ZIP 的 SHA-256 校验值。

**旧版 v0.1.0 的 Windows 文件已被此版取代。** 请勿同时安装旧的 `EdgeViz Outline` 和新的 `EdgeViz`，两者使用相同的效果匹配名。

## 安装与故障恢复

1. **完全退出 AE。** 清理 `MediaCore` 以及 AE 按版本划分的插件目录中旧的、重复的 EdgeViz 文件。
2. macOS：将完整的 `EdgeViz.plugin` 文件夹复制到 `~/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/`。
3. Windows x64：将 `EdgeViz.aex` 复制到 `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\`，可能需要管理员权限。
4. 重新启动 AE。若扫描插件时仍崩溃，移除 `EdgeViz.aex` 即可先恢复启动，并提供 AE 版本、Windows 版本及崩溃转储。截图中的路径是**扫描目录**，不足以单独证明具体是哪一个模块崩溃。

macOS 包只有 ad-hoc 签名、**未经公证**；Windows 包**未做 Authenticode 签名**。

## 已验证范围与限制

- macOS：已核验双架构 Mach-O、两个 PiPL 入口、签名，并在 **Apple Silicon 的 AE 26.5** 对附带的 `EV_Test` 工程完成一帧渲染。
- Windows：已静态核验 x64 PE、导入依赖、`EffectMain` 导出、版本资源、完整的 PiPL、区分大小写的 `PiPL` 资源类型、`8664` 入口、资源 ID 16000 以及 ZIP 完整性。`test/smoke_windows_load.py` 可供 Windows 系统上做加载初检。
- **尚未分别在 AE 23/24/25 及 Windows AE 23–26 上实测安装、效果调用和渲染。** 因此本版只以这些版本为兼容目标，不应把静态检查当成全部实机兼容认证。Windows on Arm 的原生 AE 还需要单独的 ARM64 插件，本包仅适用于 x64。

当前 Windows 构建使用较旧的 CS6 兼容 Adobe 头文件及 MinGW-w64。正式部署前建议用较新的 Adobe SDK/Visual Studio 重新构建，并针对每个目标 AE 版本完成加载、效果应用、旧工程重开和渲染回归。

## 构建与校验

公开仓库不含 Adobe SDK。设置 `AE_SDK_ROOT` 后运行：

```bash
AE_SDK_ROOT=/path/to/AdobeSDK ./build.sh
AE_SDK_ROOT=/path/to/AdobeSDK ./build_win.sh
python3 scripts/package_release.py
python3 test/check_params.py
python3 test/check_windows_binary.py
python3 test/check_version.py
python3 test/check_release.py
```

仓库：`luoboruodai/EdgeViz` · 发布标签：`v0.1.1`。

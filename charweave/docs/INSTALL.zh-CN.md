# CharWeave Native 0.4.4 beta 2 · 安装与使用

[English](INSTALL.en.md) · [返回说明](../README.md)

发行仓库：`luoboruodai/EdgeViz`；独立标签：`charweave-v0.4.4-beta.2`。CharWeave 不是 EdgeViz 更新，两者可按各自安装说明独立使用。

## 先确认包

使用 Release 的 `CharWeave-0.4.4-b2-macOS.zip` 或 `CharWeave-0.4.4-b2-Windows-x64.zip` 安装附件。
自动 “Source code” ZIP/TAR 不含可安装插件。解压后查看 `manifest.json` 与 `SHA256SUMS.txt`；主效果版本为 `0.4.4x2`（AE/PiPL `0x00022402`）。

不要同时安装旧版、Probe 版和新版。升级时先退出 AE 与 aerender，备份旧插件到 AE 不扫描的目录；不要删除工程或偏好设置。

## macOS

支持的构建架构为 Apple Silicon 与 Intel；本版实际宿主抽样为 Apple Silicon / AE 26.5x89，未覆盖所有系统与 AE 版本。插件仅 ad-hoc 签名，未 Developer ID 签名/公证。

1. 退出 AE 与 aerender。
2. 先保留旧版 `CharWeave.plugin` 或 `CharWeaveProbe.plugin`，移到 AE 扫描目录外。
3. 包内 `Install.command` 默认门禁保留。如果明确接受候选版风险，可在终端进入解压目录后运行：

   ```sh
   CHARWEAVE_EXPERIMENTAL_ALLOW_INSTALL=I_ACCEPT_CRASH_RISK ./Install.command
   ```

   默认安装到用户级 Adobe MediaCore。安装器把已有 CharWeave/Probe 备份到用户应用支持目录中的 `CharWeave/Backups/`，不会删除你的 AEP。
4. 重新打开 AE。效果名为 **CharWeave Native**，分类 **CharWeave**。

## Windows x64 · AE 2023–2026 候选

此包已交叉编译与模型测试，尚未完成真实 Windows/AE 宿主验证。

1. 完全退出 AfterFX 与 aerender。
2. 可先运行 `Load-Smoke.cmd`；它只检查 Windows DLL 加载与 `EffectMain` 导出，不启动 AE。
3. 以管理员权限运行 `Install.cmd`，选择 AE 安装目录。可先在 PowerShell 执行 `powershell -NoProfile -File .\Install.ps1 -CheckOnly`。
4. 安装器检查版本、AEX 校验值和重复副本；旧目标插件备份到系统 ProgramData 下的 `CharWeave/Backups/`。
5. 重新启动 AE。不要将多个候选 AEX 同时放在 AE Plug-ins 与共享 MediaCore 中。
6. 遇到启动问题时，退出 AE、恢复旧插件，并保留 `Collect-Diagnostics.cmd` 生成的报告。回传前自行检查诊断中的本机路径。

## 使用文字与图层样式

1. 对素材图层应用 CharWeave Native。
2. **Custom text**：用 Edit text/font 输入最多 256 个 BMP UTF-16 字符，字体来自系统；不支持把 emoji 代理对当作单个字符。
3. **Layer style library**：在 AE 的“文件 → 脚本 → 运行脚本文件”运行包内 `scripts/CharWeave Styles Manager.jsx`。
4. 回到效果所在合成，只选中一个带单个 CharWeave 的素材图层，点击管理器的创建/打开按钮。
5. 库内一个形状、文字、图片或子预合成图层对应一个样式，最多 64 个。管理器接管 Anchor / Position / Scale；需保留自定义变换、父子关系或 3D 时先预合成为一个样式。
6. 使用原有 Text mode、Cell size、调色、扩散参数。效果面板内原生 Create/Manage 按钮保持灰显，不是可用的创建入口。

## 回退与限制

退出宿主后，将新插件移出扫描目录，再把备份的旧插件恢复到原位置。一次只保留一个 CharWeave 注册项。AEP 和用户素材不属于安装包，不应随插件替换而删除。

本版回归覆盖 8/16 bpc 与已有参数/TCW1 格式。完整文字窗口 Undo/Redo、其他宿主版本、32 bpc/HDR/GPU/MFR 不声明通过。不要用模型测试、构建成功或旧版宿主记录代替当前平台实测。

# EdgeViz —— After Effects 图层边缘 / 路径可视化插件

**当前版本：v3.1.2** · **PiPL eVER：0x189601** · **构建目标：macOS arm64 + Windows x64**
**最后更新：2026-09-29**

EdgeViz 是一个 After Effects 原生效果插件，用于显示图层外框、文字字形轮廓、形状路径、贝塞尔点与手柄、遮罩剪影、运动路径，以及预合成内部的可见结构。

菜单：**效果 → PlugIn EdgeViz → EdgeViz**

> 当前已生成 macOS arm64 `.plugin` 和 Windows x64 `.aex`。Windows 二进制由 macOS 上的 MinGW-w64 交叉编译，并通过 PE、导出符号、PiPL 资源和依赖检查；当前环境没有 Windows After Effects，尚未完成 Windows AE 实机加载回归。

## 1. 本次 v3.1.2 更新

### 逻辑与显示修复

- 文字轮廓不再按 `Key Vertices` 抽样：中英文混排时保留每个字形的全部路径点。
- AE 返回零切线的文字角点、参数形状角点会生成可见的短手柄标记；真实手柄与可视化手柄采用不同标记逻辑。
- 运动外框改为：**当前单体包围盒 + 当前帧之前已经走过的运动路径**，第 0 帧不再被未来完整轨迹撑大。
- 运动路径在绘制前进行当前单体遮挡，单体自身位于运动路径之上，运动路径不会穿过当前单体。
- 多个形状单体分别计算路径、运动路径和遮挡区域，避免整层共用一个错误外框或错误手柄。
- 预合成递归下钻：形状层、文字层、普通素材边缘和嵌套预合成均映射到外层合成空间。
- 遮罩顶点读取遇到 AE 错误时会丢弃该条不完整路径，不再把未初始化坐标带入渲染。
- 遮罩剪影的轮廓链路增加邻接表，避免复杂剪影使用全量线性搜索。

### 运行效率与稳定性

- 没有启用顶点/手柄显示时，不再为每条路径分配和计算关键顶点数据。
- 没有启用运动路径或运动外框时，不再进行 120 点运动采样和逐形状运动采样。
- 普通图层运动路径渲染使用固定大小栈缓冲，避免每帧堆分配。
- 像素轮廓计算缓存上下相邻行指针，减少内层循环中的重复行寻址。
- 所有 AEGP 几何查询在 `PF_Cmd_FRAME_SETUP` 完成，渲染阶段读取帧缓存；插件不启用多帧线程渲染，避免 AE 套件线程验证错误。
- 保留 `PF_OutFlag_NON_PARAM_VARY` 与 `PF_OutFlag_WIDE_TIME_INPUT`，避免动画查看器复用过期几何缓存。

## 2. 面板参数顺序

| 顺位 | 组别 | 控件 |
|---|---|---|
| 00 | Target | Auto / This Layer / Layers Below |
| 01 | Language（默认折叠） | English / 中文 / 한국어 |
| 02 | Structure（常用） | Frame、Text Frame、Text Glyphs、Shape Drill-down、Mask Silhouette、Motion Path、Vertex Dots、Bezier Handles、Key Vertices |
| 03 | Style（默认折叠） | Stroke Width、Frame Color、Path Color、Handle Color、Motion Color |
| 04 | Detail / Point Style（默认折叠） | Vertex Size、Handle Size、Handle Length、Point Style、Custom Point Layer |
| 05 | Pixel Outline（默认折叠） | Enable Pixel Outline 位于组外；组内为 Color、Width、Edge Threshold、Edge Source、Invert、Outline Only、Distance Fade |

`Point Style` 支持 `Circle / Square / Triangle / Diamond / Cross / Custom Layer`。`Custom Layer` 通过 `Custom Point Layer` 选择图层作为点标记来源。

> 00–05 是面板的操作优先级，不是 AE 全局快捷键。原生效果参数没有绑定新的键盘热键；若需要 F/T/M 等热键，需要另做脚本或面板层。

## 3. 本地目录说明

| 路径 | 用途 |
|---|---|
| `EdgeViz Outline.plugin/` | 当前 macOS arm64 最终插件 bundle |
| `源码工程/build-win/` | 当前 Windows x64 `.aex` 交叉编译产物 |
| `源码工程/` | C++ 源码、PiPL、构建脚本、静态检查和本机编译产物 |
| `验证截图/` | 本机回归截图；发布目录只挑选脱敏后的 v3.1.2 截图 |
| `验证工程/` | 本机 AE 回归工程副本；不进入公开 GitHub 发布包 |
| `脚本归档/` | 历史 ExtendScript、历史输出和旧插件隔离样本；不进入公开发布包 |
| `开发记录/` | 本轮对话、修复、测试和发布阻塞记录 |
| `发布物料/v3.1.2/` | 当前版本的交付物、校验和、验证截图及发布清单 |

## 4. 安装 / 卸载（macOS）

```bash
# 安装
cp -R "EdgeViz Outline.plugin" \
  "$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/"

# 卸载
rm -rf "$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/EdgeViz Outline.plugin"
```

复制后必须完全退出并重新启动 After Effects。当前本机已完成旧 bundle 删除、新 bundle 安装和 After Effects 重启；安装件与构建件的 SHA-256 一致，`codesign --verify --deep --strict` 通过。

当前构建件为 ad-hoc 签名，仅适合开发机/测试机使用；对外分发还需要开发者签名、公证和安装说明。

## 5. 源码构建

### macOS arm64

```bash
cd 源码工程
AE_SDK_ROOT=/path/to/AfterEffectsSDK ./build.sh
```

依赖：Xcode Command Line Tools 中的 `clang++`、`Rez`、`codesign`，以及可合法使用的 Adobe After Effects SDK 头文件。

### Windows x64 `.aex`（交叉编译）

```bash
cd 源码工程
AE_SDK_ROOT=/path/to/AfterEffectsSDK ./build_win.sh
```

依赖：x86_64 MinGW-w64（`x86_64-w64-mingw32-g++`、`windres`、`strip`）、Python 3，以及 Adobe AE SDK 头文件。脚本生成 PE32+ DLL，嵌入 `PiPL` 资源并只导出 `EffectMain`。

当前 Windows 产物使用 MinGW-w64 交叉编译，依赖 Windows Universal CRT API-set 与 `KERNEL32.dll`。由于本机没有 Windows After Effects，仍需在 Windows + 目标 AE 版本上安装并执行实际加载/渲染回归；正式商业分发建议使用匹配版本的 Visual Studio/Adobe SDK 重新构建。

本机开发副本保留了编译所需 SDK，但公开 GitHub 清洁导出会排除 `SDK/Headers`、缓存、历史工程和旧坏插件。

## 6. 回归测试

静态参数检查：

```bash
cd 源码工程
python3 test/check_params.py
```

本轮在 After Effects 2026 / aerender 26.5 上验证：

- 中英文混排文字：全部字形路径点、真实手柄、零切线角点标记。
- 文字源文本、字符范围选择器、位置/缩放/旋转动画。
- 多形状同层：矩形、椭圆、星形、Bezier 形状各自的路径、点、手柄和运动。
- 形状形变过程中的路径轮廓与当前形状对齐。
- 运动路径在单体下方、外框随已走过路径增长。
- 预合成内的形状、文字、普通素材边缘及嵌套预合成下钻。
- AE GUI 启动无 EdgeViz 版本不匹配弹窗，aerender 回归无错误退出。

本机最新回归输出位于 `发布物料/v3.1.2/verification/`，公开导出只包含截图，不包含用户工程 `.aep`、素材、缓存或日志。

## 7. 隐私与发布清理

2026-09-29 已对当前工程和拟发布清洁目录做静态扫描：

- 未发现 API key、token、password、私钥、Bearer 凭据或云服务密钥。
- 源码工程（排除本机 SDK）未发现 `本机用户目录/...`、`外部卷/...` 等本机绝对路径。
- 公开导出排除 `.DS_Store`、`.aep`、视频、日志、缓存、`脚本归档/out/`、`quarantine_broken_plugins/`、本机 SDK 和个人素材。
- 调试日志只在用户主动设置 `EVIZ_DEBUG` 或创建 `/tmp/ev_dbg_on` 时启用，不包含在发布物料中。
- GitHub 上传前仍需在目标仓库页面/凭据侧确认没有误选个人工程或环境文件。

## 8. Windows 状态

已生成并上传 Windows x64 `.aex`，没有把 macOS `.plugin` 改名冒充 Windows 版本。当前已完成 PE 格式、x64 架构、`EffectMain` 导出、PiPL `8664` 入口、资源 ID 16000 和 DLL 依赖检查；尚未在 Windows After Effects 实机上加载验证。请见 `发布物料/v3.1.2/windows/README.md`。

## 9. 发布状态

本机 v3.1.2 已构建、安装、重启 AE、回归渲染并完成隐私清理。macOS 与 Windows x64 产物均已整理并推送到 GitHub `main`。Windows 产物已完成结构验证，但仍需在 Windows After Effects 实机回归。

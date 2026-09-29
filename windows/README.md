# EdgeViz v3.1.2 Windows x64

本版本已经生成 Windows x64 `.aex`：

```text
macOS/Linux 交叉编译：MinGW-w64 x86_64
PE：PE32+ DLL / x86-64
入口导出：EffectMain
PiPL：资源类型 PiPL，资源 ID 16000，Windows x64 key 8664
```

构建命令：

```bash
cd 源码工程
AE_SDK_ROOT=/path/to/AfterEffectsSDK ./build_win.sh
```

依赖：

- 合法可用的 Adobe After Effects SDK 头文件；
- x86_64 MinGW-w64：`x86_64-w64-mingw32-g++`、`windres`、`strip`；
- Python 3。

本机已完成：

- C++ 源码 Windows 条件编译；
- PE32+ x64 DLL 编译和 strip；
- `EffectMain` 唯一导出检查；
- PiPL 资源、版本 `0x189601`、Windows x64 入口 `8664` 检查；
- `.rsrc` 中资源 ID 16000 检查；
- DLL 依赖检查。

当前环境没有 Windows After Effects，因此尚未完成 Windows AE 实机加载、参数面板和 aerender 回归。正式商业分发建议在目标 Windows + AE 版本上用匹配的 Visual Studio/Adobe SDK 重新构建并签名。

不要把 macOS `.plugin` 改名为 `.aex`；本目录中的 `.aex` 是实际 PE32+ Windows x64 二进制。

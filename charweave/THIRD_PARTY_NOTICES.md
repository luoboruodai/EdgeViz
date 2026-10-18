# Third-party notices / 第三方声明

The Windows binary was built with MinGW-w64 GCC 16.2.0, with libgcc and libstdc++ linked statically. The applicable GCC license text and GCC Runtime Library Exception are included in `licenses/COPYING3` and `licenses/COPYING.RUNTIME`; MinGW-w64's notice collection is in `licenses/MinGW-w64-COPYING.txt`. No toolchain executables are bundled.

Windows 二进制使用 MinGW-w64 GCC 16.2.0 构建，静态链接 libgcc / libstdc++。随附 GCC 许可证、运行库例外与 MinGW-w64 声明；不捆绑编译器或开发环境。

Adobe After Effects and its SDK are third-party products. The Adobe SDK, its headers and the Adobe `AE_General.r` resource template are not included in this source distribution. Builders provide their own appropriately licensed SDK using `AE_SDK_ROOT` and (on macOS) `AE_REZ_ROOT`. No fonts, media, client projects or third-party UI artwork are bundled. System font names are requests to the operating system, not redistributed font files.

Adobe SDK、头文件、资源模板、字体、视频与用户工程均不随源码或安装附件分发。构建者自行提供 SDK；系统字体名不等于附带字体文件。

This notice records third-party components. It does not assign a new license to the project's own source or assert endorsement by Adobe, Microsoft, GCC, or MinGW-w64.

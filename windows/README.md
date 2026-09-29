# Windows build status

No Windows `.aex` is included in v3.1.2. The available source tree was built and verified only on macOS arm64 with the Adobe After Effects SDK and Apple Clang.

To produce a real Windows plug-in, build the same effect with a supported Windows After Effects SDK, Visual Studio toolchain, Windows PiPL/resource settings, and an After Effects version matching the target SDK. Do not rename the macOS `.plugin` bundle to `.aex`; that is not a valid Windows binary.

# Publishing status — 2026-10-08

- Released **v0.1.1** to `luoboruodai/EdgeViz` as the latest GitHub Release with macOS universal ZIP, Windows x64 ZIP and their SHA-256 manifest. A fresh download of both ZIPs matched the local artifacts and `SHA256SUMS.txt`.
- Marked v0.1.0 as superseded; removed its reported-problematic Windows ZIP and old checksum asset. Its macOS arm64 ZIP is retained only as a historical artifact.
- Source, PiPL/PE resources, ZIP files and checksum manifest rebuilt and statically checked. AE 26.5 on Apple Silicon successfully rendered one frame of the supplied test comp; the old installed plug-in was restored afterward.
- **Outstanding:** Windows AE 23–26 and macOS AE 23–25/Intel runtime loading, UI and rendering tests. v0.1.1 is targeted at these versions but must not be called host-certified across the entire matrix.

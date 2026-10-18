# CharWeave Native 0.4.4 beta 2 · Installation and usage

[中文](INSTALL.zh-CN.md) · [Overview](../README.md)

Repository: `luoboruodai/EdgeViz`; separate release tag: `charweave-v0.4.4-beta.2`. This is not an EdgeViz update; use each plugin's own installation instructions.

## Identify the package

Download the installable `CharWeave-0.4.4-b2-macOS.zip` or `CharWeave-0.4.4-b2-Windows-x64.zip` asset from the matching Release. GitHub's automatic “Source code” ZIP/TAR contains no installable plugin. Check `manifest.json` and `SHA256SUMS.txt` after extraction. The effect reports `0.4.4x2` (AE/PiPL `0x00022402`).

Do not keep old, Probe and new copies in scan paths together. Exit AE and aerender before replacement. Back up the old plugin outside AE's scan paths; do not remove projects, footage or preferences.

## macOS

The bundle contains Apple Silicon and Intel architectures. This release's host sample is Apple Silicon / AE 26.5x89; other systems and Intel execution were not tested. The bundle is ad-hoc signed, not Developer ID signed or notarized.

1. Exit AE and aerender.
2. Preserve any existing `CharWeave.plugin` or `CharWeaveProbe.plugin` outside AE's scan directories.
3. `Install.command` is gated by default. To explicitly opt into this candidate, enter the extracted package directory in Terminal and run:

   ```sh
   CHARWEAVE_EXPERIMENTAL_ALLOW_INSTALL=I_ACCEPT_CRASH_RISK ./Install.command
   ```

   The default target is the user's Adobe MediaCore directory. Existing CharWeave/Probe bundles are preserved under the user's application-support `CharWeave/Backups/` directory. AEP files are not modified.
4. Reopen AE. Look for **CharWeave Native** in the **CharWeave** category.

## Windows x64 · AE 2023–2026 candidate

The binary is cross-compiled and model-tested; real Windows/AE host acceptance is still pending.

1. Close AfterFX and aerender completely.
2. Optionally run `Load-Smoke.cmd` first. It checks Windows DLL loading and the `EffectMain` export, not AE behavior.
3. Run `Install.cmd` as administrator and select the AE installation. A read-only preflight is available with `powershell -NoProfile -File .\Install.ps1 -CheckOnly`.
4. The installer checks AE versions, the AEX hash and duplicate copies. It backs up an existing target plugin under ProgramData's `CharWeave/Backups/` directory.
5. Restart AE with only one CharWeave copy installed across AE Plug-ins and shared MediaCore.
6. If startup fails, exit AE, restore the backup, and keep the report from `Collect-Diagnostics.cmd`. Review machine-specific paths before sharing that report.

## Text and layer styles

1. Apply CharWeave Native to a footage layer.
2. **Custom text:** Edit text/font accepts up to 256 BMP UTF-16 characters and uses system fonts. Emoji surrogate pairs are not supported as single characters.
3. **Layer style library:** use AE's File → Scripts → Run Script File to open the packaged `scripts/CharWeave Styles Manager.jsx`.
4. Return to the effect composition, select one layer containing exactly one CharWeave instance, and use the manager's create/open button.
5. Each shape, text, image or precomp layer is one style, up to 64. The manager owns Anchor / Position / Scale. Precompose a style first to preserve custom transforms, parenting or 3D.
6. Use the existing Text mode, Cell size, palette and dispersion controls. The native effect-panel Create/Manage buttons remain disabled; they are not the supported manager entry point.

## Rollback and limits

Exit AE, remove the new plugin from the scan path, and restore the preserved old bundle/AEX. Keep one registration only. Never delete AEPs or media as part of plugin rollback.

Regression coverage includes 8/16 bpc and the existing parameter/TCW1 formats. Full text-dialog Undo/Redo, other host versions, 32 bpc/HDR/GPU/MFR are not claimed as verified. A behavior model, successful build, or older host result is not current-platform host acceptance.

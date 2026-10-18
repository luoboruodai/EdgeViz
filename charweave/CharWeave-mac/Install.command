#!/bin/zsh
set -euo pipefail
# Intentionally gated until button / undo / save-reopen host acceptance passes.
if [[ "${CHARWEAVE_EXPERIMENTAL_ALLOW_INSTALL:-}" != "I_ACCEPT_CRASH_RISK" ]]; then
  cat >&2 <<'TXT'
CharWeave Native 0.4.4 beta 2 macOS 当前仍为测试候选版，安装器默认停用。
本轮完成本地回归、构建及 Apple Silicon / AE 26.5x89 安装后抽样；不代表其他环境通过。原生 Create/Manage 继续灰显停用。
早期 0.3.1 构建曾造成启动扫描崩溃；不要把旧包或本候选版误当正式稳定版安装。
请查看包内 docs/INSTALL.zh-CN.md 或 docs/INSTALL.en.md。
TXT
  exit 3
fi
ROOT="$(cd "$(dirname "$0")" && pwd)"
PLUGIN="$ROOT/build/CharWeave.plugin"
[[ -d "$PLUGIN" ]] || { echo "Missing $PLUGIN" >&2; exit 2; }
set +e
RUNNING="$(pgrep -fl '/Contents/MacOS/After Effects( |$)|/aerender( |$)')"
CHECK=$?
set -e
if (( CHECK > 1 )); then echo 'Cannot verify AE process state; refusing install.' >&2; exit 4; fi
if (( CHECK == 0 )); then echo "Close AE and aerender before installing:\n$RUNNING" >&2; exit 4; fi
codesign --verify --deep --strict "$PLUGIN"
DEFAULT="$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
TARGET="${1:-$DEFAULT}"
[[ "$TARGET" == /* && "$TARGET" != / ]] || { echo 'Target must be an absolute plug-in directory.' >&2; exit 2; }
STAMP="$(date +%Y%m%d-%H%M%S)-$$"
STATE="$HOME/Library/Application Support/CharWeave"
STAGE="$STATE/Staging/$STAMP.plugin"
BACKUP="$STATE/Backups/$STAMP"
mkdir -p "$TARGET" "$(dirname "$STAGE")"
ditto "$PLUGIN" "$STAGE"
codesign --verify --deep --strict "$STAGE"
for NAME in CharWeave.plugin CharWeaveProbe.plugin; do
  if [[ -e "$TARGET/$NAME" ]]; then
    mkdir -p "$BACKUP"
    mv "$TARGET/$NAME" "$BACKUP/$NAME"
    echo "Previous bundle preserved outside AE scan path: $BACKUP/$NAME"
  fi
done
mv "$STAGE" "$TARGET/CharWeave.plugin"
echo "Experimental candidate installed: $TARGET/CharWeave.plugin"

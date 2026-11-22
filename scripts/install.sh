#!/usr/bin/env bash
# Installs (or removes) a built Mutation Engine plugin for the current user, or system-wide.
#
#   scripts/install.sh                      # install from ./build or a release folder
#   scripts/install.sh --build-dir out      # install from a specific CMake build directory
#   scripts/install.sh --system             # system-wide (needs sudo)
#   scripts/install.sh --uninstall
#
# macOS : VST3 -> ~/Library/Audio/Plug-Ins/VST3     AU -> ~/Library/Audio/Plug-Ins/Components
# Linux : VST3 -> ~/.vst3
set -euo pipefail

BUILD_DIR="build"; SYSTEM=0; UNINSTALL=0
while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --system) SYSTEM=1; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

OS="$(uname -s)"
case "$OS" in
  Darwin) VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"; AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
          [ "$SYSTEM" = 1 ] && { VST3_DIR="/Library/Audio/Plug-Ins/VST3"; AU_DIR="/Library/Audio/Plug-Ins/Components"; } ;;
  Linux)  VST3_DIR="$HOME/.vst3"; AU_DIR=""
          [ "$SYSTEM" = 1 ] && VST3_DIR="/usr/lib/vst3" ;;
  *) echo "unsupported OS: $OS" >&2; exit 1 ;;
esac
NAME="Mutation Engine"

if [ "$UNINSTALL" = 1 ]; then
  rm -rf "$VST3_DIR/$NAME.vst3"; [ -n "$AU_DIR" ] && rm -rf "$AU_DIR/$NAME.component"
  echo "Removed $NAME from $VST3_DIR${AU_DIR:+ and $AU_DIR}. Rescan plugins in your DAW."
  exit 0
fi

# Find the built bundle: a CMake tree, or a release archive unpacked next to this script.
HERE="$(cd "$(dirname "$0")" && pwd)"
find_bundle() { # $1 = suffix
  for root in "$BUILD_DIR" "$HERE/.." "$HERE"; do
    found="$(find "$root" -maxdepth 6 -name "$NAME.$1" -not -path '*/.git/*' 2>/dev/null | head -n 1 || true)"
    [ -n "$found" ] && { echo "$found"; return; }
  done
}
VST3_SRC="$(find_bundle vst3)"
[ -n "$VST3_SRC" ] || { echo "Could not find '$NAME.vst3'. Build first (see INSTALL.md) or pass --build-dir." >&2; exit 1; }

install_bundle() { # $1 src  $2 dest dir
  mkdir -p "$2"; rm -rf "$2/$(basename "$1")"; cp -R "$1" "$2/"
  echo "Installed $(basename "$1") -> $2"
}
sudo_cmd=""; [ "$SYSTEM" = 1 ] && sudo_cmd="sudo"
$sudo_cmd bash -c "$(declare -f install_bundle); install_bundle '$VST3_SRC' '$VST3_DIR'"

if [ "$OS" = Darwin ]; then
  AU_SRC="$(find_bundle component)"
  [ -n "$AU_SRC" ] && $sudo_cmd bash -c "$(declare -f install_bundle); install_bundle '$AU_SRC' '$AU_DIR'"
  # Locally built binaries are unsigned; Apple Silicon refuses to load unsigned code, and
  # downloaded archives carry a quarantine flag that makes Gatekeeper block them.
  for b in "$VST3_DIR/$NAME.vst3" "${AU_DIR:+$AU_DIR/$NAME.component}"; do
    [ -n "$b" ] && [ -e "$b" ] || continue
    $sudo_cmd xattr -dr com.apple.quarantine "$b" 2>/dev/null || true
    $sudo_cmd codesign --force --deep --sign - "$b" 2>/dev/null && echo "Ad-hoc signed $(basename "$b")" || true
  done
  echo "Done. In Ableton Live: Settings > Plug-Ins > enable 'Use VST3 Plug-In System Folders', then Rescan."
else
  echo "Done. Rescan plugins in REAPER (Options > Preferences > Plug-ins > VST > Re-scan) or Bitwig (Settings > Plug-ins > Rescan)."
fi

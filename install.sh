#!/usr/bin/env bash
# Installer for EAR Suite Extras (macOS, Apple Silicon prebuilt plugins).
# Copies the prebuilt VST3 plugins into your VST3 folder, and - if the
# official reaper-adm-extension is already installed - patches it so
# Reverb/IR Reverb export correctly to ADM. See README.md for what each
# step does and why, and for build-from-source instructions on other
# platforms.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VST3_SRC="$SCRIPT_DIR/Compiled Plugins/VST3"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
REAPER_ADM_DEST="$HOME/Library/Application Support/REAPER/UserPlugins/reaper_adm.dylib"
REAPER_ADM_PATCH="$SCRIPT_DIR/reaper-adm-extension-patch/reaper_adm.dylib"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This installer is for macOS only. See README.md to build from source on your platform."
  exit 1
fi

echo "== EAR Suite Extras installer =="
echo

if [[ -d "$VST3_SRC" ]]; then
  mkdir -p "$VST3_DEST"
  echo "Installing plugins to: $VST3_DEST"
  for plugin in "$VST3_SRC"/*.vst3; do
    name="$(basename "$plugin")"
    echo "  - $name"
    rm -rf "${VST3_DEST:?}/${name:?}"
    cp -R "$plugin" "$VST3_DEST/"
  done
else
  echo "ERROR: Could not find '$VST3_SRC'. Run this script from the repo root."
  exit 1
fi

echo
echo "== reaper-adm-extension ADM export patch (Reverb / IR Reverb) =="
if [[ -f "$REAPER_ADM_DEST" ]]; then
  if [[ ! -f "$REAPER_ADM_DEST.orig" ]]; then
    echo "Backing up existing reaper_adm.dylib to reaper_adm.dylib.orig"
    cp "$REAPER_ADM_DEST" "$REAPER_ADM_DEST.orig"
  else
    echo "Backup already exists (reaper_adm.dylib.orig) - leaving it as is."
  fi
  echo "Installing patched reaper_adm.dylib (enables ADM export for Reverb/IR Reverb)"
  cp "$REAPER_ADM_PATCH" "$REAPER_ADM_DEST"
else
  echo "reaper-adm-extension not found at:"
  echo "  $REAPER_ADM_DEST"
  echo "Skipping this step - it's only needed if you use REAPER's ADM export."
  echo "Install the official EAR Production Suite first if you need this,"
  echo "then re-run this script, or see reaper-adm-extension-patch/README.md"
  echo "to apply it manually."
fi

echo
echo "Done. Quit and restart REAPER (and rescan plugins if it doesn't pick"
echo "the new ones up automatically)."

#!/usr/bin/env bash
# Build MacTunes on macOS.  This script never installs packages implicitly:
# Homebrew commands are printed as guidance so the user remains in control.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "buildMac.sh must be run on macOS." >&2
  exit 1
fi
if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required. Install it from https://brew.sh/ and rerun." >&2
  exit 1
fi

formulae=(cmake pkg-config gtk+3 gstreamer gst-plugins-good gst-plugins-bad
          taglib sqlite)
missing=()
for formula in "${formulae[@]}"; do
  brew list --versions "$formula" >/dev/null 2>&1 || missing+=("$formula")
done
if ((${#missing[@]})); then
  echo "Missing Homebrew formulae (not installed automatically):"
  printf '  %s\n' "${missing[@]}"
  echo "Install them with: brew install ${missing[*]}"
  echo "Then rerun this script."
  exit 1
fi

radio=OFF
if pkg-config --exists libvlc; then
  radio=ON
elif brew list --cask --versions vlc >/dev/null 2>&1 ||
     [[ -d /Applications/VLC.app ]] ||
     [[ -d "$HOME/Applications/VLC.app" ]]; then
  radio=ON
  echo "VLC.app found. MacTunes will load libVLC directly from the application bundle."
else
  echo "VLC/libVLC was not found. MacTunes will build without Internet Radio."
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
  -DMACTUNES_ENABLE_IPOD=OFF -DMACTUNES_ENABLE_CD=ON \
  -DMACTUNES_ENABLE_RADIO="$radio"
cmake --build "$BUILD_DIR" --parallel
echo "Build complete: $BUILD_DIR/mactunes"
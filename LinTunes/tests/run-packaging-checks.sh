#!/usr/bin/env bash
# Validate installation layout without installing anything on the host.
# Usage: bash tests/run-packaging-checks.sh /path/to/existing/build
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
[[ -f "$BUILD/CPackConfig.cmake" ]] || {
  echo "Configure/build LinTunes before running this check." >&2
  exit 1
}
BUILD="$(cd "$BUILD" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
bash -n "$ROOT/packageLinux.sh" "$ROOT/buildPod.sh"
bash "$ROOT/packageLinux.sh" --help >"$TMP/help"
grep -q -- '--format' "$TMP/help"
if bash "$ROOT/packageLinux.sh" --format invalid >"$TMP/error" 2>&1; then
  echo "Invalid format was incorrectly accepted." >&2
  exit 1
fi
grep -q 'Unsupported format' "$TMP/error"
if bash "$ROOT/packageLinux.sh" --format deb >"$TMP/error" 2>&1; then
  echo "Removed DEB format was incorrectly accepted." >&2
  exit 1
fi
grep -q 'Unsupported format' "$TMP/error"
if bash "$ROOT/packageLinux.sh" --build-dir "$TMP/no-build" >"$TMP/error" 2>&1; then
  echo "Missing build was incorrectly accepted." >&2
  exit 1
fi
grep -q 'Build directory not found' "$TMP/error"

DESTDIR="$TMP/stage" cmake --install "$BUILD" --prefix /usr
for file in usr/bin/lintunes usr/share/applications/lintunes.desktop \
            usr/share/licenses/lintunes/LICENSE \
            usr/share/doc/lintunes/README.md usr/share/doc/lintunes/manual.md; do
  [[ -f "$TMP/stage/$file" ]] || { echo "Missing installed file: $file" >&2; exit 1; }
done
[[ -x "$TMP/stage/usr/bin/lintunes" ]]
[[ ! -e "$TMP/stage/usr/local/bin/lintunes" ]]
grep -q '^Exec=lintunes$' "$TMP/stage/usr/share/applications/lintunes.desktop"
if grep -q 'CPACK_DEBIAN_' "$BUILD/CPackConfig.cmake"; then
  echo "Removed Debian packaging metadata is still configured." >&2
  exit 1
fi
grep -q 'CPACK_RPM_PACKAGE_AUTOREQPROV "yes"' "$BUILD/CPackConfig.cmake"
if bash "$ROOT/packageLinux.sh" --format arch >"$TMP/error" 2>&1; then
  echo "Removed Arch format was incorrectly accepted." >&2
  exit 1
fi
grep -q 'Unsupported format' "$TMP/error"
echo "PASS: helper arguments, /usr install layout, desktop entry and native package configuration."

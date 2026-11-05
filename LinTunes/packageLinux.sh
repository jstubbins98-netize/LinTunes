#!/usr/bin/env bash
# Package an existing native build. Never installs packages or uses sudo.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
OUTPUT_DIR="$ROOT/generated-artifacts/packages"
FORMAT=auto

die() { printf 'LinTunes packaging: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "$1 is required. $2"; }
usage() {
  cat <<'EOF'
Usage: bash packageLinux.sh [--format auto|rpm|arch|tgz]
                           [--build-dir DIR] [--output-dir DIR]

Packages an already built LinTunes executable for the local Linux distribution.
Default build: ./build (relative to this script).
Default output: ./generated-artifacts/packages.
auto selects RPM or Arch from /etc/os-release; otherwise uses TGZ.
No packages are installed automatically. Build on the destination distribution.
EOF
}
while (($#)); do
  case "$1" in
    --format|--build-dir|--output-dir)
      (($# >= 2)) || die "$1 requires a value."
      case "$1" in
        --format) FORMAT="$2" ;;
        --build-dir) BUILD_DIR="$2" ;;
        --output-dir) OUTPUT_DIR="$2" ;;
      esac
      shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) die "Unknown argument: $1. Use --help." ;;
  esac
done
[[ "$(uname -s)" == Linux ]] || die "This helper is for LinTunes on Linux only."
case "$FORMAT" in auto|rpm|arch|tgz) ;; *) die "Unsupported format: $FORMAT" ;; esac
if [[ "$FORMAT" == auto ]]; then
  distro=""
  if [[ -r /etc/os-release ]]; then
    distro="$(. /etc/os-release; printf '%s %s' "${ID:-}" "${ID_LIKE:-}")"
  fi
  case " $distro " in
    *" fedora "*|*" rhel "*|*" centos "*|*" suse "*|*" opensuse "*|*" opensuse-leap "*|*" opensuse-tumbleweed "*) FORMAT=rpm ;;
    *" arch "*|*" manjaro "*) FORMAT=arch ;;
    *) FORMAT=tgz; echo "No supported native package family detected; creating a TGZ archive." ;;
  esac
fi
need cmake "Install CMake with your distribution's package manager."
[[ -d "$BUILD_DIR" ]] || die "Build directory not found: $BUILD_DIR. Build LinTunes first."
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
[[ -f "$BUILD_DIR/CMakeCache.txt" && -x "$BUILD_DIR/lintunes" ]] ||
  die "No built LinTunes found in $BUILD_DIR. Run cmake --build there first."
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

case "$FORMAT" in
  rpm)
    need cpack "Install CMake."
    need rpmbuild "Install rpm-build (Fedora/RHEL) or rpm-build on openSUSE."
    ;;
  arch)
    need makepkg "Install base-devel."
    [[ "$(id -u)" != 0 ]] || die "Arch packages must be built as a normal user, not root."
    ;;
  tgz) need cpack "Install CMake." ;;
esac

# Refresh packaging/install metadata in older build trees without rebuilding
# source files or silently changing the build's existing compiler options.
cmake -S "$ROOT" -B "$BUILD_DIR"
[[ -f "$BUILD_DIR/CPackConfig.cmake" ]] || die "CMake did not generate CPackConfig.cmake."

if [[ "$FORMAT" == arch ]]; then
  WORK="$(mktemp -d)"
  trap 'rm -rf "$WORK"' EXIT
  export LINTUNES_STAGE_DIR="$WORK/stage"
  DESTDIR="$LINTUNES_STAGE_DIR" cmake --install "$BUILD_DIR" --prefix /usr
  cp "$BUILD_DIR/packaging/PKGBUILD" "$WORK/PKGBUILD"
  (
    cd "$WORK"
    export PKGDEST="$OUTPUT_DIR"
    makepkg --force --cleanbuild
  )
else
  case "$FORMAT" in rpm) GENERATOR=RPM ;; tgz) GENERATOR=TGZ ;; esac
  cpack --config "$BUILD_DIR/CPackConfig.cmake" -G "$GENERATOR" -B "$OUTPUT_DIR"
fi
echo
echo "Package output: $OUTPUT_DIR"
echo "Install the package for this distribution using:"
case "$FORMAT" in
  rpm)
    if command -v zypper >/dev/null 2>&1; then
      printf '  sudo zypper install %q/lintunes-*.rpm\n' "$OUTPUT_DIR"
    else
      printf '  sudo dnf install %q/lintunes-*.rpm  (or yum install)\n' "$OUTPUT_DIR"
    fi ;;
  arch) printf '  sudo pacman -U %q/lintunes-*.pkg.tar.*\n' "$OUTPUT_DIR" ;;
  tgz) echo "  TGZ is a manual-install archive, not a package-manager package. See README.md." ;;
esac

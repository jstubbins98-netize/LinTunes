#!/usr/bin/env bash
# buildPod.sh — Install dependencies and compile LinTunes
# Supports: Ubuntu/Debian, Fedora/RHEL/CentOS, Arch Linux

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

info()    { echo -e "${CYAN}${BOLD}[LinTunes]${RESET} $*"; }
success() { echo -e "${GREEN}${BOLD}[  OK  ]${RESET} $*"; }
warn()    { echo -e "${YELLOW}${BOLD}[ WARN ]${RESET} $*"; }
die()     { echo -e "${RED}${BOLD}[ FAIL ]${RESET} $*" >&2; exit 1; }

# ── Detect distro ─────────────────────────────────────────────────────────────
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "${ID_LIKE:-$ID}"
    elif command -v apt-get &>/dev/null; then
        echo "debian"
    elif command -v dnf &>/dev/null; then
        echo "fedora"
    elif command -v pacman &>/dev/null; then
        echo "arch"
    else
        echo "unknown"
    fi
}

# ── Install helpers ────────────────────────────────────────────────────────────
install_debian() {
    info "Detected Debian / Ubuntu — installing packages via apt…"
    sudo apt-get update -qq
    sudo apt-get install -y \
        build-essential cmake pkg-config \
        libgtk-3-dev \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-libav \
        libtag1-dev \
        libsqlite3-dev \
        libgpod-dev \
        libcdio-dev \
        libcdio-paranoia-dev \
        libcddb2-dev \
        lame flac vorbis-tools ffmpeg wodim
}

install_fedora() {
    info "Detected Fedora / RHEL / CentOS — installing packages via dnf…"
    local PM="dnf"
    command -v dnf &>/dev/null || PM="yum"

    # Enable RPM Fusion for GStreamer ugly/libav plugins and lame
    if ! rpm -q rpmfusion-free-release &>/dev/null; then
        warn "RPM Fusion free repo not found — attempting to enable it for codec packages…"
        sudo "$PM" install -y \
            "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm" \
            || warn "Could not enable RPM Fusion — some codec packages may be skipped."
    fi

    sudo "$PM" install -y \
        gcc-c++ cmake pkgconfig \
        gtk3-devel \
        gstreamer1-devel \
        gstreamer1-plugins-base-devel \
        gstreamer1-plugins-good \
        gstreamer1-plugins-bad-free \
        taglib-devel \
        sqlite-devel \
        libgpod-devel \
        libcdio-devel \
        libcdio-paranoia-devel \
        libcddb-devel \
        flac vorbis-tools ffmpeg wodim || true

    # Optional packages that may not be in base repos
    sudo "$PM" install -y lame gstreamer1-plugins-ugly gstreamer1-libav 2>/dev/null \
        || warn "Some optional codec packages were unavailable — playback of some formats may be limited."
}

install_arch() {
    info "Detected Arch Linux — installing packages via pacman…"
    sudo pacman -Sy --needed --noconfirm \
        base-devel cmake \
        gtk3 \
        gstreamer \
        gst-plugins-base \
        gst-plugins-good \
        gst-plugins-bad \
        gst-plugins-ugly \
        gst-libav \
        taglib \
        sqlite \
        libgpod \
        libcdio \
        libcdio-paranoia \
        libcddb \
        lame flac vorbis-tools ffmpeg dvd+rw-tools cdrtools
}

install_deps() {
    local distro
    distro=$(detect_distro)

    case "$distro" in
        *debian*|*ubuntu*)  install_debian ;;
        *fedora*|*rhel*|*centos*|*ol*) install_fedora ;;
        *arch*|*manjaro*)   install_arch ;;
        *)
            warn "Unrecognised distribution: '$distro'"
            warn "Please install the required libraries manually (see README.md) then re-run with --skip-deps."
            ;;
    esac
}

# ── Build ──────────────────────────────────────────────────────────────────────
build() {
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    BUILD_DIR="$SCRIPT_DIR/build"

    info "Configuring with CMake…"
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    local JOBS
    JOBS=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
    info "Compiling with $JOBS parallel jobs…"
    cmake --build "$BUILD_DIR" --parallel "$JOBS"

    success "Build complete!  Binary: $BUILD_DIR/lintunes"
    echo
    echo -e "  Run:  ${BOLD}$BUILD_DIR/lintunes${RESET}"
    echo -e "  Or:   ${BOLD}sudo cmake --install $BUILD_DIR${RESET}  (installs to /usr/local/bin)"
}

# ── Entry point ────────────────────────────────────────────────────────────────
SKIP_DEPS=0
for arg in "$@"; do
    case "$arg" in
        --skip-deps|-s) SKIP_DEPS=1 ;;
        --help|-h)
            echo "Usage: $0 [--skip-deps]"
            echo "  --skip-deps / -s   Skip package installation, go straight to build"
            exit 0
            ;;
        *) die "Unknown argument: $arg" ;;
    esac
done

echo
echo -e "${BOLD}╔══════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║        LinTunes — Build Script        ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════╝${RESET}"
echo

if [ "$SKIP_DEPS" -eq 0 ]; then
    install_deps
    success "All dependencies installed."
    echo
else
    info "Skipping dependency installation (--skip-deps)."
fi

build

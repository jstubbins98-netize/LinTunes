#!/usr/bin/env bash
set -euo pipefail
# In an MSYS2 UCRT64 shell. Package names are the official UCRT64 packages.
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk3 \
  mingw-w64-ucrt-x86_64-gstreamer mingw-w64-ucrt-x86_64-gst-plugins-base \
  mingw-w64-ucrt-x86_64-gst-plugins-good mingw-w64-ucrt-x86_64-taglib \
  mingw-w64-ucrt-x86_64-sqlite3
cmake -S . -B build-windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DWINTUNES_ENABLE_IPOD=OFF -DWINTUNES_ENABLE_CD=OFF -DWINTUNES_ENABLE_RADIO=OFF
cmake --build build-windows --parallel
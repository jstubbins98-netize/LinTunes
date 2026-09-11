$ErrorActionPreference = "Stop"

# Run from an MSYS2 UCRT64 PowerShell (or with C:\msys64\usr\bin\bash.exe).
# GTK/GStreamer are provided by the UCRT64 repository. libgpod and libcdio are
# intentionally optional because they are not packaged consistently.
& C:\msys64\usr\bin\bash.exe -lc @'
set -e
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk3 \
  mingw-w64-ucrt-x86_64-gstreamer mingw-w64-ucrt-x86_64-gst-plugins-base \
  mingw-w64-ucrt-x86_64-gst-plugins-good mingw-w64-ucrt-x86_64-taglib \
  mingw-w64-ucrt-x86_64-sqlite3
cd "$(cygpath -u "$PWD")"
rm -rf build-windows
cmake -S . -B build-windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
  -DWINTUNES_ENABLE_IPOD=OFF -DWINTUNES_ENABLE_CD=OFF -DWINTUNES_ENABLE_RADIO=OFF
cmake --build build-windows --parallel
'@
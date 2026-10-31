#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"${CXX:-c++}" -std=c++17 -I"$root/src" \
    "$root/tests/ipod-volume-safety.cpp" "$root/src/iPodVolume.cpp" "$root/src/iPodSync.cpp" \
    $(pkg-config --cflags --libs libgpod-1.0 gio-2.0) \
    -Wl,--wrap=statfs -Wl,--wrap=access -o "$tmp/ipod-volume-safety"
"$tmp/ipod-volume-safety"
"${CXX:-c++}" -std=c++17 -I"$root/src" \
    "$root/tests/ipod-format-safety.cpp" "$root/src/iPodVolume.cpp" \
    $(pkg-config --cflags --libs libgpod-1.0 gio-2.0) \
    -Wl,--wrap=g_bus_get_sync -Wl,--wrap=g_dbus_connection_call_sync \
    -o "$tmp/ipod-format-safety"
"$tmp/ipod-format-safety"

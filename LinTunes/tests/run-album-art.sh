#!/usr/bin/env bash
# Non-destructive artwork tests. All media is generated in a temporary directory.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v ffmpeg >/dev/null || { echo "ffmpeg is required to generate test audio." >&2; exit 1; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for format in wav mp3 flac m4a ogg opus; do
    ffmpeg -hide_banner -loglevel error -f lavfi -i anullsrc=r=44100:cl=mono \
        -t 0.1 "$tmp/sample.$format"
done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$root/src" \
    "$root/tests/album-art.cpp" "$root/src/AlbumArt.cpp" \
    $(pkg-config --cflags --libs gtk+-3.0 taglib) -o "$tmp/test-album-art"
"$tmp/test-album-art" "$tmp"

# LinTunes Family

LinTunes is an open-source, iTunes-style desktop music manager written in
C++17 with GTK3. This repository contains separate editions for Linux, macOS,
and Windows.

## Choose Your Version

| Operating system | Edition | Directory | Main executable | Status |
|---|---|---|---|---|
| Linux | **LinTunes** | [`LinTunes/`](LinTunes/) | `lintunes` | Full-featured primary edition |
| macOS | **MacTunes** | [`MacTunes/`](MacTunes/) | `mactunes` | Native paths and macOS device handling; some hardware features are optional |
| Windows | **WinTunes** | [`WinTunes/`](WinTunes/) | `WinTunes.exe` | Native Windows paths; iPod and CD features are disabled by default |

Each edition has its own source tree, CMake configuration, documentation, and
platform build scripts. Changes made to one edition are not automatically
copied to the other editions.

## Shared Features

All three editions share the same core music-management interface:

- Import individual audio files or complete folders
- Validate imported audio and reject AppleDouble and unreadable files
- Browse and search by title, artist, album, and genre
- Create and manage playlists
- Play local music with GStreamer
- Play, pause, seek, shuffle, repeat, and control volume
- Read track metadata with TagLib
- Store the music library in SQLite
- Stream internet radio through libVLC when enabled
- Use built-in KQED, KEXP, and SomaFM presets

Supported audio formats include MP3, M4A/AAC, FLAC, OGG Vorbis, WAV, AIFF,
WMA, Opus, APE, and ALAC, subject to the codecs installed on the host system.

## Platform Support

### LinTunes for Linux

LinTunes is the primary and most complete edition. Depending on installed
system dependencies, it supports:

- iPod classic, nano, mini, shuffle, and mounted iPod touch devices through
  libgpod
- Mac- and Windows-formatted disk-mode iPods
- Playlist and track synchronization
- Guarded iPod touch IPSW restoration through external libimobiledevice tools
- Safe music-database reset for connected disk-mode iPods
- Audio CD detection and ripping
- CDDB metadata lookup
- Audio CD burning
- Apple SuperDrive detection after the drive is unlocked by Linux

See [LinTunes/README.md](LinTunes/README.md) for dependencies and build
instructions, or [LinTunes/manual.md](LinTunes/manual.md) for the complete user
manual.

Quick build:

```bash
cd LinTunes
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/lintunes
```

### MacTunes for macOS

MacTunes uses macOS-specific locations and device behavior:

- Application data: `~/Library/Application Support/MacTunes`
- Music folder: `~/Music`
- Mounted-volume discovery under `/Volumes`
- Optical-disc eject through `drutil`
- macOS-compatible filesystem flushing

The main library and playback features are supported. iPod synchronization is
optional because libgpod is not normally available from Homebrew core. CD
ripping depends on the available libcdio packages and optical drive. Linux
`wodim` CD burning is intentionally disabled.

Homebrew is required by the supplied build helper. It checks dependencies and
prints the required installation command without installing packages
automatically.

```bash
cd MacTunes
./buildMac.sh
./build/mactunes
```

See [MacTunes/README.md](MacTunes/README.md) and
[MacTunes/manual.md](MacTunes/manual.md) for full details.

### WinTunes for Windows

WinTunes is designed for native Windows builds using MSYS2 UCRT64 and MinGW.
It uses:

- `%LOCALAPPDATA%\WinTunes` for application data
- The Windows Known Folder API for the user's Music directory
- UTF-8-aware filesystem paths
- A native `WinTunes.exe` target

The standard Windows build supports library management, playlists, local
playback, searching, metadata, and SQLite persistence. Internet radio can be
enabled when a compatible libVLC development package is supplied.

libgpod and the Linux optical-disc stack are not available in the standard
MSYS2 UCRT64 repositories. Therefore, iPod synchronization, iPod restoration,
CD ripping, and CD burning are disabled by default rather than presented as
working features.

From PowerShell:

```powershell
cd WinTunes
.\buildWindows.ps1
```

Or from an MSYS2 UCRT64 shell:

```bash
cd WinTunes
./msys2-build.sh
```

The Windows executable is produced at `WinTunes/build-windows/WinTunes.exe`.
See [WinTunes/README.md](WinTunes/README.md) and
[WinTunes/manual.md](WinTunes/manual.md) for complete setup instructions and
limitations.

## Optional Feature Switches

MacTunes and WinTunes can be configured without platform-specific optional
libraries.

MacTunes:

```bash
cmake -S MacTunes -B MacTunes/build \
  -DMACTUNES_ENABLE_IPOD=OFF \
  -DMACTUNES_ENABLE_CD=OFF \
  -DMACTUNES_ENABLE_RADIO=OFF
```

WinTunes:

```bash
cmake -S WinTunes -B WinTunes/build \
  -DWINTUNES_ENABLE_IPOD=OFF \
  -DWINTUNES_ENABLE_CD=OFF \
  -DWINTUNES_ENABLE_RADIO=OFF
```

When a feature is disabled, its implementation uses an explicit unsupported
response instead of silently attempting platform-incompatible operations.

## Repository Layout

```text
.
├── LinTunes/       Linux edition
├── MacTunes/       macOS edition
├── WinTunes/       Windows edition
└── README.md       Edition overview
```

Each edition contains:

```text
<Edition>/
├── CMakeLists.txt
├── README.md
├── manual.md
├── resources/
└── src/
```

## Safety

iPod firmware restore and music-database reset operations can erase data.
Destructive operations require confirmation and must remain bound to the
selected physical device. Never disconnect an iPod while tracks or its music
database are being written.

## License

GPL-2.0 or later.

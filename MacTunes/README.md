# MacTunes

A cross-platform iTunes-style music manager written in C++17 using GTK3. The
macOS port uses native `~/Library/Application Support/MacTunes` storage and
`~/Music` as its default music folder.

## macOS support and limitations

Build on macOS with `./buildMac.sh` (Homebrew is required). The script checks
for dependencies and prints an install command; it never runs package
installation itself. GTK3, GStreamer, TagLib, SQLite and libVLC are supported.
CD ripping and burning use the built-in macOS `drutil` and `afconvert` tools;
libcdio, libcdio-paranoia, and libcddb are not required on macOS. Ripping
supports AIFF, WAV, AAC, and M4A (the formats available through afconvert).
libgpod is not a Homebrew core formula, so iPod synchronization is optional and
is disabled when it is unavailable. iPod touch restore can use
`idevicerestore`/`idevice_id` if separately installed; classic firmware flashing
is not offered. The Linux build remains available with the portable fallback
branches and Linux device discovery.

## Features

- **Music library** — import audio files and folders, browse by artist/album/genre/playlist
- **Audio playback** — full playback via GStreamer: play, pause, seek, shuffle, repeat
- **Internet radio** — stream HTTP/HTTPS radio stations through libVLC
- **SQLite database** — persistent library at `~/Library/Application Support/MacTunes/library.db`
- **iPod sync** — connect any iPod (classic, nano, mini, touch) via libgpod, add/remove tracks and sync playlists
- **Guarded iPod restore** — restore iPod touch from IPSW, restore disk-mode iPod firmware, or erase and recreate its music database
- **CD ripping** — macOS drutil/afconvert ripping to AIFF, WAV, AAC, or M4A
- **CD burning** — stage Red Book PCM AIFF files and burn with macOS drutil
- **Apple SuperDrive** — handled through the built-in macOS optical-disc tools
- **Playlists** — create, populate, and delete playlists; sync playlists to iPod
- **Search** — real-time library search across title, artist, album, and genre
- **Right-click context menu** — play, sync to iPod, remove from library, delete file

## Supported Audio Formats

MP3, M4A/AAC, FLAC, OGG Vorbis, WAV, AIFF, WMA, Opus, APE, ALAC

## Dependencies

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake pkg-config \
    libgtk-3-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly gstreamer1.0-libav \
    libtag1-dev \
    libsqlite3-dev \
    libgpod-dev \
    libcdio-dev libcdio-paranoia-dev \
    libcddb2-dev \
    libvlc-dev vlc-plugin-base \
    lame flac vorbis-tools ffmpeg wodim
```

### Fedora / RHEL

```bash
sudo dnf install -y \
    gcc-c++ cmake pkgconfig \
    gtk3-devel \
    gstreamer1-devel gstreamer1-plugins-base-devel \
    gstreamer1-plugins-good gstreamer1-plugins-bad-free \
    gstreamer1-plugins-ugly gstreamer1-libav \
    taglib-devel \
    sqlite-devel \
    libgpod-devel \
    libcdio-devel libcdio-paranoia-devel \
    libcddb-devel \
    vlc-devel \
    lame flac vorbis-tools ffmpeg wodim
```

### Arch Linux

```bash
sudo pacman -S --needed \
    base-devel cmake \
    gtk3 \
    gstreamer gst-plugins-base gst-plugins-good \
    gst-plugins-bad gst-plugins-ugly gst-libav \
    taglib \
    sqlite \
    libgpod \
    libcdio libcdio-paranoia \
    libcddb \
    vlc \
    lame flac vorbis-tools ffmpeg dvd+rw-tools cdrtools
```

## Building

On macOS, `buildMac.sh` checks Homebrew dependencies without installing them:

```bash
./buildMac.sh
```

The CMake feature switches are `MACTUNES_ENABLE_IPOD`,
`MACTUNES_ENABLE_CD`, and `MACTUNES_ENABLE_RADIO`. Set any to `OFF` when its
optional dependency is unavailable.

```bash
git clone <this-repo>
cd MacTunes

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Optional: install system-wide
sudo make install
```

## Running

```bash
./build/mactunes
```

Or after `make install`:
```bash
mactunes
```

## Internet Radio

1. Click **📻 Internet Radio** in the toolbar.
2. Choose a built-in station such as **KQED Public Radio**, **KEXP**, or
   **SomaFM Groove Salad**, or paste a custom HTTP/HTTPS stream URL.
3. Click **Play**. MacTunes stops local playback and starts the stream with libVLC.
4. Use the normal volume slider, or reopen the dialog and choose **Stop Radio**.

## iPod Setup

1. Plug in your iPod via USB.
2. Let your system auto-mount it (usually under `/media/$USER/<iPod name>` or `/run/media/$USER/<iPod name>`).
3. In MacTunes, go to **Device → Connect iPod…** — it will auto-detect the mount point.
4. Select tracks in the library and choose **Device → Sync to iPod** (or right-click → Add to iPod).
5. After syncing, the iPod database is written automatically.

Windows/PC-formatted FAT32 iPods are supported. MacTunes accepts devices that
do not expose a libgpod UUID, resolves control-directory capitalization through
libgpod, writes the additional database required by iPod Shuffle models, and
flushes FAT filesystem changes after each completed sync.

> **Note:** For newer iPod touch/nano models, you may need `libimobiledevice` and `ifuse` to mount the device first:
> ```bash
> sudo apt install libimobiledevice-utils ifuse
> idevicepair pair
> ifuse ~/ipod
> ```

### Restoring an iPod

**Device → Restore iPod from Firmware…** supports two separate workflows:

- iPod touch: an Apple-signed `.ipsw` file via `idevicerestore`, targeted to
  the single UDID reported by `idevice_id`.
- Classic/nano/mini/shuffle firmware flashing is intentionally unavailable:
  stock `ipodpatcher` cannot bind a write to the device selected in MacTunes.

Install the matching restore tool separately. Restores erase the device and require
typed confirmation. MacTunes never accepts a manually entered device path and never
falls back to an auto-selected disk for destructive firmware writes.

**Device → Erase and Reset Music Database…** erases music/playlists and recreates
the libgpod database on an already-connected disk-mode iPod. It does not flash
firmware or repartition the disk.

## Apple SuperDrive

The Apple SuperDrive is a USB optical drive. On Linux it requires a one-time udev rule or the `apple-superdrive` utility to unlock the drive:

```bash
# Install sg3-utils
sudo apt install sg3-utils

# Send the magic byte to unlock the drive (run once after each plug-in)
sg_raw /dev/sr0 EA 00 00 00 00 00 01

# Or install apple-superdrive-enabler (AUR on Arch, manual on others)
```

After unlocking, the drive appears as `/dev/sr0` (or similar) and MacTunes detects it automatically alongside any other CD drives.

You can also create a udev rule to unlock it automatically on plug-in:

```udev
# /etc/udev/rules.d/71-apple-superdrive.rules
ACTION=="add", ATTRS{idVendor}=="05ac", ATTRS{idProduct}=="8406", \
    RUN+="/usr/bin/sg_raw /dev/$kernel EA 00 00 00 00 00 01"
```

## CD Ripping

1. Insert an audio CD.
2. Go to **Device → Rip CD…**
3. Choose output folder and format (FLAC recommended for lossless archival).
4. Click Rip — tracks are saved to `<output>/<Artist>/<Album>/NN - Title.flac` and imported into your library.

On Linux, metadata is fetched from CDDB (gnudb.gnudb.org). macOS uses the
native disc status/TOC path and falls back to numbered tracks when no tags are
available on mounted source files.

## CD Burning

1. Insert a blank CD-R.
2. Select tracks in the library (up to ~74 minutes).
3. Go to **Device → Burn Disc…** and confirm.

On macOS, burning uses `drutil`; simulation and manual speed selection are not
supported by its portable command syntax and are rejected explicitly.

## Project Structure

```
MacTunes/
├── CMakeLists.txt          — CMake build definition
├── README.md               — This file
├── resources/
│   └── mactunes.desktop    — XDG desktop entry
└── src/
    ├── main.cpp            — Entry point, initialisation
    ├── Track.h             — Track data model (POD struct)
    ├── Database.h/cpp      — SQLite3 library database
    ├── Library.h/cpp       — Music library (import, search, playlists)
    ├── AudioPlayer.h/cpp   — GStreamer playback engine
    ├── RadioPlayer.h/cpp   — libVLC internet radio playback
    ├── iPodSync.h/cpp      — libgpod iPod sync
    ├── CDManager.h/cpp     — native macOS CD ripping and audio-CD burning
    └── MainWindow.h/cpp    — GTK3 main application window
```

## License

GPL-3.0 or later

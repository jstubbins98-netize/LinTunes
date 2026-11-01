# LinTunes

A fully-featured iTunes equivalent for Linux written in C++17, using GTK3 for the GUI.

## Features

- **Music library** — import audio files and folders, browse by artist/album/genre/playlist
- **Audio playback** — full playback via GStreamer: play, pause, seek, shuffle, repeat
- **Internet radio** — stream HTTP/HTTPS radio stations through libVLC
- **SQLite database** — persistent library at `~/.local/share/lintunes/library.db`
- **iPod sync** — sync supported Windows/FAT-formatted disk-mode iPods through libgpod; HFS/HFS+ and unknown filesystems are blocked
- **Windows iPod formatting** — create FAT32 on a verified iPod data partition while preserving its existing MBR and firmware partition
- **Guarded iPod restore** — restore iPod touch from IPSW, or erase and recreate a Windows-formatted disk-mode iPod's music database
- **CD ripping** — paranoia-mode ripping to FLAC, MP3, OGG, AAC, or WAV with CDDB metadata
- **CD burning** — burn selected library tracks to a blank audio CD
- **Apple SuperDrive** — works as a standard USB CD drive on Linux; auto-detected alongside `/dev/sr*` devices
- **Playlists** — create, populate, and delete playlists; sync playlists to iPod
- **Search** — real-time library search across title, artist, album, and genre
- **Right-click context menu** — play, sync to iPod, remove from library, delete file

## Supported Audio Formats

MP3, M4A/AAC, FLAC, OGG Vorbis, WAV, AIFF, WMA, Opus, APE, ALAC

## Install LinTunes via apt (Ubuntu)

Install the published package from the LinTunes PPA without compiling locally.
The published build supports **Ubuntu 26.04 LTS (Resolute), amd64
(64-bit Intel/AMD)**.

```bash
sudo apt update
sudo apt install software-properties-common
sudo add-apt-repository ppa:doctor-retro-g/lintunes
sudo apt update
sudo apt install lintunes
```

Open **LinTunes** from your application menu, or run:

```bash
lintunes
```

The PPA is community-maintained, not part of Ubuntu's official repositories.
Add it only if you trust its maintainer. It is not intended for Debian; other
Ubuntu releases and architectures require separate compatible builds.
Check the [PPA page](https://launchpad.net/~doctor-retro-g/+archive/ubuntu/lintunes)
for published packages and supported releases.

### Updates

With the PPA enabled, LinTunes updates are available through your normal
system updates. To request an available LinTunes upgrade specifically:

```bash
sudo apt update
sudo apt install --only-upgrade lintunes
```

### Removal

```bash
sudo apt remove lintunes
sudo add-apt-repository --remove ppa:doctor-retro-g/lintunes
sudo apt update
```

Removing the package leaves your personal music and library database intact.

## Dependencies for building from source

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
    lame flac vorbis-tools ffmpeg wodim udisks2 dosfstools
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
    lame flac vorbis-tools ffmpeg wodim udisks2 dosfstools
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
    lame flac vorbis-tools ffmpeg dvd+rw-tools cdrtools udisks2 dosfstools
```

## Building

```bash
git clone https://github.com/jstubbins98-netize/LinTunes.git
cd LinTunes

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Optional: install system-wide
sudo make install
```
## Internet Radio

1. Click **📻 Internet Radio** in the toolbar.
2. Choose a built-in station such as **KQED Public Radio**, **KEXP**, or
   **SomaFM Groove Salad**, or paste a custom HTTP/HTTPS stream URL.
3. Click **Play**. LinTunes stops local playback and starts the stream with libVLC.
4. Use the normal volume slider, or reopen the dialog and choose **Stop Radio**.

## iPod Setup

1. Plug in your iPod via USB.
2. Let your system auto-mount it (usually under `/media/$USER/<iPod name>` or `/run/media/$USER/<iPod name>`).
3. In LinTunes, go to **Device → Connect iPod…** — it will auto-detect the mount point.
4. Select tracks in the library and choose **Device → Sync to iPod** (or right-click → Add to iPod).
5. After syncing, the iPod database is written automatically.

Only Windows/PC-formatted FAT iPods can be written. HFS/HFS+ devices prompt
you to choose **Cancel** or **Format as FAT32…**, with an explicit warning
that formatting permanently erases all user data on the music/data partition.
Cancel is the default and leaves the device unchanged. Choosing FAT32 opens
device verification and a separate typed erase confirmation; it does not
immediately format the iPod. Unknown and FUSE filesystems are rejected. The
check is repeated before file copies, track removal, playlist/database writes,
and music resets, not just at connection time. LinTunes accepts devices that
do not expose a libgpod UUID, resolves control-directory capitalization through
libgpod, writes the additional database required by iPod Shuffle models, and
flushes FAT filesystem changes after each completed sync.

Mounting an iPod touch through `ifuse` does not bypass this policy. Its separate
IPSW restoration workflow remains available, but FUSE/AFC music writes are not.

### Format an iPod for Windows

**Warning: formatting permanently erases every file on the music/data partition.**
Back up music, playlists, and other files first.

1. Mount the iPod, then choose **Device → Format iPod for Windows…**.
2. Select the device. LinTunes checks its USB hardware serial, libgpod model,
   filesystem UUID, and partition layout.
3. Review the displayed device details and type `FORMAT IPOD`.
4. Authorize the operation in your desktop's Polkit dialog if requested.
5. Wait while LinTunes unmounts the partition, formats it as FAT32, remounts it,
   restores device information, initializes and verifies the database, and
   flushes pending writes. Do not unplug it during this operation.

Formatting requires **UDisks2**, **dosfstools** (`mkfs.fat`), a running system
D-Bus, and a desktop Polkit authentication agent. Do not run LinTunes as root.
Device identity and model information are rechecked after confirmation.

**Supported formatting layout:** a known USB disk-mode iPod with an existing
DOS/MBR table, a separate firmware partition 1, and music partition 2. Only the
music partition is formatted; the partition table and firmware contents are not
rewritten.

**Apple partition maps are not automatically converted.** HFS/HFS+ often comes
with an Apple partition map; simply formatting that partition is not enough to
make the device Windows-compatible. Such devices require a model-aware restore
using Windows iTunes or another trusted restore tool. LinTunes refuses these,
GPT disks, unverified layouts, missing serials/models, and whole-disk targets.
The formatter is not an iPod touch restore tool or a partition-repair utility.

If formatting succeeds but remounting or database initialization fails, LinTunes
reports the failure; the old data has still been erased. Do not assume an error
means the disk was unchanged.

### Non-destructive safety tests

```bash
bash tests/run-ipod-safety.sh
```

The tests simulate filesystems and UDisks replies. They never format a disk or
contact the real system UDisks service.

### Restoring an iPod

**Device → Restore iPod from Firmware…** supports two separate workflows:

- iPod touch: an Apple-signed `.ipsw` file via `idevicerestore`, targeted to
  the single UDID reported by `idevice_id`.
- Classic/nano/mini/shuffle firmware flashing is intentionally unavailable:
  stock `ipodpatcher` cannot bind a write to the device selected in LinTunes.

Install the matching restore tool separately. Restores erase the device and require
typed confirmation. LinTunes never accepts a manually entered device path and never
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

After unlocking, the drive appears as `/dev/sr0` (or similar) and LinTunes detects it automatically alongside any other CD drives.

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

Metadata is fetched automatically from CDDB (gnudb.gnudb.org).

## CD Burning

1. Insert a blank CD-R.
2. Select tracks in the library (up to ~74 minutes).
3. Go to **Device → Burn Disc…** and confirm.

Burning requires `wodim` (or `cdrecord`) to be installed.

## Project Structure

```
LinTunes/
├── CMakeLists.txt          — CMake build definition
├── README.md               — This file
├── resources/
│   └── lintunes.desktop    — XDG desktop entry
└── src/
    ├── main.cpp            — Entry point, initialisation
    ├── Track.h             — Track data model (POD struct)
    ├── Database.h/cpp      — SQLite3 library database
    ├── Library.h/cpp       — Music library (import, search, playlists)
    ├── AudioPlayer.h/cpp   — GStreamer playback engine
    ├── RadioPlayer.h/cpp   — libVLC internet radio playback
    ├── iPodSync.h/cpp      — libgpod iPod sync
    ├── CDManager.h/cpp     — CD ripping (libcdio-paranoia) and burning (wodim)
    └── MainWindow.h/cpp    — GTK3 main application window
```

## License

GPL-3.0 or later

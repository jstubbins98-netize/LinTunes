# MacTunes User Manual

MacTunes is a cross-platform music manager designed to work and feel like
iTunes. On macOS it manages a music library, plays audio, and can use supported
GTK3/GStreamer integrations. macOS stores its database under
`~/Library/Application Support/MacTunes/library.db` and defaults to `~/Music`.
The macOS port uses native `drutil` and `afconvert` for optical discs, so
libcdio and Linux burning tools are not required. libgpod iPod sync remains
optional. Linux retains its portable device integration.

---

## Table of Contents

1. [Installation](#1-installation)
2. [First Launch](#2-first-launch)
3. [The Main Window](#3-the-main-window)
4. [Adding Music to Your Library](#4-adding-music-to-your-library)
5. [Browsing Your Library](#5-browsing-your-library)
6. [Searching](#6-searching)
7. [Audio Playback](#7-audio-playback)
8. [Playlists](#8-playlists)
9. [Track Context Menu](#9-track-context-menu)
10. [iPod Sync](#10-ipod-sync)
11. [Restoring and Resetting an iPod](#11-restoring-and-resetting-an-ipod)
12. [CD Ripping](#12-cd-ripping)
13. [CD Burning](#13-cd-burning)
14. [Apple SuperDrive](#14-apple-superdrive)
15. [Supported Audio Formats](#15-supported-audio-formats)
16. [Keyboard and Mouse Reference](#16-keyboard-and-mouse-reference)
17. [File Locations](#17-file-locations)
18. [Dependencies and Build Instructions](#18-dependencies-and-build-instructions)
19. [Troubleshooting](#19-troubleshooting)

---

## 1. Installation

### Option A — Automated build script

On macOS, use `buildMac.sh`. It checks Homebrew formulae and prints safe
installation guidance without installing anything automatically:

```bash
cd MacTunes
chmod +x buildMac.sh
./buildMac.sh
```

Linux users should install the dependencies listed below with their distribution
package manager and configure with CMake; MacTunes no longer carries a
distribution-specific installer script.

### Option B — Manual build

**1. Install dependencies**

Ubuntu / Debian:
```bash
sudo apt update
sudo apt install -y \
    build-essential cmake pkg-config \
    libgtk-3-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly gstreamer1.0-libav \
    libtag1-dev libsqlite3-dev libgpod-dev \
    libcdio-dev libcdio-paranoia-dev libcddb2-dev \
    lame flac vorbis-tools ffmpeg wodim
```

Fedora / RHEL:
```bash
sudo dnf install -y \
    gcc-c++ cmake pkgconfig \
    gtk3-devel \
    gstreamer1-devel gstreamer1-plugins-base-devel \
    gstreamer1-plugins-good gstreamer1-plugins-bad-free \
    gstreamer1-plugins-ugly gstreamer1-libav \
    taglib-devel sqlite-devel libgpod-devel \
    libcdio-devel libcdio-paranoia-devel libcddb-devel \
    lame flac vorbis-tools ffmpeg wodim
```

Arch Linux:
```bash
sudo pacman -S --needed \
    base-devel cmake gtk3 \
    gstreamer gst-plugins-base gst-plugins-good \
    gst-plugins-bad gst-plugins-ugly gst-libav \
    taglib sqlite libgpod \
    libcdio libcdio-paranoia libcddb \
    lame flac vorbis-tools ffmpeg dvd+rw-tools cdrtools
```

**2. Compile**
```bash
cd MacTunes
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**3. Run**
```bash
./build/mactunes
```

**4. Optional system-wide install**
```bash
sudo make install
# then launch from anywhere:
mactunes
```

---

## 2. First Launch

When MacTunes starts for the first time, the library will be empty and a welcome screen is shown with a large **+ Add Music** button. The library database is created automatically at `~/.local/share/mactunes/library.db`. Nothing is imported automatically — you choose what goes in your library.

---

## 3. The Main Window

The main window is divided into four areas:

```
┌─────────────────────────────────────────────────────────┐
│  Menu bar  (File / Library / Device / Help)             │
├─────────────────────────────────────────────────────────┤
│  Toolbar   ⏮ ▶ ⏭  ⇄ ↺  [+ Add Music]  Track info  Vol  │
├────────────┬────────────────────────────────────────────┤
│            │  Search bar                                │
│  Sidebar   ├────────────────────────────────────────────┤
│            │                                            │
│  Music     │  Track list                               │
│  Library   │  (or empty-state overlay)                 │
│  Playlists │                                            │
│  Devices   │                                            │
│            │                                            │
├────────────┴────────────────────────────────────────────┤
│  Status bar                                             │
└─────────────────────────────────────────────────────────┘
```

### Menu bar

| Menu | Items |
|------|-------|
| **File** | Add Files to Library, Add Folder to Library, Quit |
| **Library** | New Playlist |
| **Device** | Connect iPod, Sync to iPod, Rip CD, Burn Disc, Eject Disc |
| **Help** | About |

### Toolbar

| Control | Function |
|---------|----------|
| ⏮ | Previous track |
| ▶ / ⏸ | Play / Pause |
| ⏭ | Next track |
| ⇄ | Toggle shuffle |
| ↺ | Toggle repeat |
| **+ Add Music** | Drop-down to add files or a folder |
| Track label | Shows currently playing title and artist |
| Seek bar | Scrub through the current track |
| Time label | Current position / total duration |
| 🔊 slider | Volume |

### Source sidebar

The sidebar on the left organises your content into three top-level nodes:

- **Music Library** — your full track collection
- **Playlists** — any playlists you have created; click a playlist name to view its tracks
- **Devices** — shown when an iPod is connected

### Status bar

The bar along the bottom shows the current operation (importing, syncing, ripping) and general library information.

---

## 4. Adding Music to Your Library

MacTunes never moves or copies your files — it adds references to wherever your audio files already live on disk.

### Via the + Add Music button

Click **+ Add Music** in the toolbar. A small menu appears:

- **Add Files…** — opens a file chooser filtered to audio formats. You can select multiple files at once.
- **Add Folder…** — opens a folder chooser. MacTunes scans the selected folder and all sub-folders recursively, importing every audio file it finds.

### Via the File menu

**File → Add Files to Library…** and **File → Add Folder to Library…** do the same thing.

### Via drag and drop

Drag audio files or folders from your file manager directly onto the track list. MacTunes accepts them and imports them in the background. You can drop multiple files and folders at the same time.

### What happens during import

- MacTunes reads the embedded tags (title, artist, album, genre, year, track number, disc number, composer, album artist, BPM, comment) using TagLib.
- Track duration and bitrate are read from the audio stream properties.
- File size is recorded.
- All data is stored in the SQLite library database.
- The track list refreshes automatically when the import finishes.
- The status bar shows progress and a final count of how many tracks were added.

---

## 5. Browsing Your Library

### Track list columns

| Column | Description |
|--------|-------------|
| # | Track number |
| Title | Track title |
| Artist | Performing artist |
| Album | Album name |
| Genre | Genre tag |
| Year | Release year |
| Time | Duration (mm:ss) |
| Bitrate | Audio bitrate in kbps |
| On iPod | Checkbox — ticked if the track is already on your connected iPod |

Click any column header to sort the list by that field. Click again to reverse the sort order. Columns can be resized by dragging their dividers.

### Selecting tracks

- **Click** to select a single track.
- **Shift+click** to select a range.
- **Ctrl+click** to add or remove individual tracks from the selection.

---

## 6. Searching

The search bar sits above the track list. As you type, the list filters in real time across **title**, **artist**, **album**, and **genre** simultaneously. Clearing the search box returns the full library view.

---

## 7. Audio Playback

### Starting playback

- **Double-click** any track in the list to play it immediately.
- Right-click a track and choose **Play** from the context menu.

### Playback controls

| Control | Action |
|---------|--------|
| ▶ | Play (or resume if paused) |
| ⏸ | Pause |
| ⏮ | Go to the previous track in the queue |
| ⏭ | Skip to the next track in the queue |
| ⇄ (toggle) | Shuffle — when on, tracks play in random order |
| ↺ (toggle) | Repeat — when on, the queue loops from the beginning after the last track |
| 📻 Internet Radio | Open the libVLC internet radio dialog |

### Internet radio

Click **📻 Internet Radio** and choose one of the included stations: **KQED
Public Radio**, **KEXP 90.3 FM**, or **SomaFM Groove Salad**. Select **Custom
stream URL** to enter another direct HTTP or HTTPS stream. Icecast, Shoutcast,
and playlist URLs work when supported by libVLC. Starting radio stops local
music playback. The normal volume control also controls the radio stream.
Reopen the dialog and click **Stop Radio** to end the stream.

### Seeking

Drag the seek bar in the toolbar left or right to jump to any position in the current track. The time label updates while you drag and shows the new position when you release.

### Volume

Drag the volume slider in the toolbar. The range is 0 (silent) to 100% (full volume). The last position is not saved between sessions; it always starts at full volume.

### Now-playing display

While a track is playing, the toolbar label shows:

```
Track Title  –  Artist Name  [Album Name]
```

The window title bar also updates to show the current track name.

---

## 8. Playlists

### Creating a playlist

Go to **Library → New Playlist**. Enter a name in the dialog and click **Create**. The new playlist appears in the sidebar under **Playlists**.

### Viewing a playlist

Click the playlist name in the sidebar. The track list changes to show only the tracks in that playlist.

### Returning to the full library

Click **Music Library** at the top of the sidebar.

### Syncing a playlist to your iPod

With an iPod connected, the playlist sync feature copies the playlist and all its tracks to the device. See [iPod Sync](#10-ipod-sync) for details.

---

## 9. Track Context Menu

Right-click any track (or selection of tracks) in the list to open the context menu:

| Option | Action |
|--------|--------|
| **Play** | Starts playback of the selected track(s) |
| **Add to iPod** | Copies the selected track(s) to the connected iPod (iPod must be connected first) |
| **Remove from Library** | Removes the track(s) from MacTunes but leaves the audio file on disk |
| **Delete File** | Removes the track(s) from the library **and permanently deletes the file** from disk |

> **Warning:** Delete File is irreversible. The audio file is removed from your hard drive.

---

## 10. iPod Sync

MacTunes supports all classic iPods (iPod classic, iPod mini, iPod nano, iPod photo) using the libgpod library.

### Supported iPod models

- iPod classic (all generations)
- iPod mini
- iPod nano (1st – 6th generation)
- iPod photo
- iPod touch — requires `ifuse` to mount first (see below)

### Step 1 — Connect and mount your iPod

Plug your iPod into a USB port. On most modern Linux desktops (GNOME, KDE, XFCE with udisks2), the iPod mounts automatically under `/run/media/$USER/<iPod name>` or `/media/$USER/<iPod name>`.

If it does not auto-mount:
```bash
# Find the device
lsblk

# Mount manually (replace sdX1 with your device)
sudo mount /dev/sdX1 /mnt/ipod
```

For **iPod touch and newer nano models** that use the MTP/AFC protocol instead of a mass-storage filesystem, use `ifuse`:
```bash
sudo apt install libimobiledevice-utils ifuse
mkdir ~/ipod
idevicepair pair
ifuse ~/ipod
```

### Step 2 — Connect in MacTunes

Go to **Device → Connect iPod…**

MacTunes automatically scans `/proc/mounts`, `/media`, `/mnt`, and `/run/media` for a directory containing an `iPod_Control` or `iTunes_Control` folder. If exactly one iPod is found, it connects automatically. If multiple are found, a dialog lets you choose which one.

The status bar confirms the connection:
```
iPod connected at /run/media/user/MyIPod — 347 tracks, 4.2 / 14.9 GB used
```

### Step 3 — Sync tracks

1. Select one or more tracks in the library track list.
2. Go to **Device → Sync to iPod**, or right-click and choose **Add to iPod**.
3. The status bar shows progress track by track.
4. When all files are copied, MacTunes writes the iPod database automatically.

The **On iPod** column in the track list is ticked for tracks that are already on your iPod.

### Sync behaviour

- Tracks are copied to the iPod using libgpod's standard file-placement logic. The iPod database is updated to reflect the new tracks.
- If a file copy fails (e.g. not enough space), that track is skipped and the rest continue.
- The database write at the end is what makes tracks visible in the iPod's own music app. Do not unplug the iPod before the status bar confirms the sync is complete.

### Disconnecting safely

After a successful sync, eject the iPod from your file manager or run:
```bash
udisksctl unmount -b /dev/sdX1
udisksctl power-off -b /dev/sdX
```

---

## 11. Restoring and Resetting an iPod

Restoring is a destructive maintenance operation. It is separate from normal
music synchronization and permanently erases data from the iPod. Back up
anything important before continuing, keep the iPod connected to reliable power,
and use firmware made for the exact model.

### iPod touch firmware restore

This workflow uses an Apple `.ipsw` file and the system's `idevicerestore` tool.

1. Install `idevicerestore` and `idevice_id` using your distribution's package
   manager.
2. Obtain an Apple-signed IPSW for the exact iPod touch model. MacTunes does not
   download firmware or bypass Apple's signing checks.
3. Connect exactly one iPod touch directly by USB so `idevice_id` can identify
   its UDID. `idevicerestore` will handle the required restore-mode transition.
4. Choose **Device → Restore iPod from Firmware…**.
5. Select **iPod touch**, choose the `.ipsw` file, and review the warning.
6. Type `ERASE IPOD` exactly and choose **Restore and Erase**.
7. Do not unplug the iPod until MacTunes reports that the restore finished.

MacTunes records the selected UDID, verifies that same device again immediately
before launch, and passes the UDID to `idevicerestore`. The tool validates firmware
identity and Apple restore requirements. A restore can fail if the IPSW is unsigned
or is for another model.

### Classic, nano, mini, and shuffle firmware restore

Disk-mode firmware flashing is intentionally disabled. Stock `ipodpatcher`
auto-selects a detected disk and does not provide a supported interface that
binds the destructive write to the iPod selected in MacTunes. MacTunes refuses
to launch it rather than risk writing to another attached device.

Selecting the disk-mode family therefore shows an explanation and stops before
asking for a firmware file or changing the device. Use **Erase and Reset Music
Database** when the goal is to clear a safely identified, mounted disk-mode
iPod. A full firmware-partition restore must be performed outside MacTunes with
a procedure that explicitly identifies the intended physical device.

### Erase and reset the music database

This option deletes music, playlists, and the iPod database without flashing
firmware or repartitioning the disk:

1. Mount and connect the disk-mode iPod normally in MacTunes.
2. Choose **Device → Erase and Reset Music Database…**.
3. Verify the displayed mount point.
4. Type `ERASE MUSIC` exactly and confirm.

MacTunes identifies the model through libgpod, deletes only the iPod control
directories, and creates a fresh database. The operation is cancelled if the
model cannot be identified or the mount point is not writable.

Immediately before deleting anything, MacTunes reopens the iPod database and
checks that the mount point still refers to the originally connected physical
block device and model. It also verifies the iPod UUID when the model exposes
one. Older PC/FAT-formatted iPods may not provide a UUID, so MacTunes does not
require one for ordinary connection or syncing. If the iPod was unplugged,
remounted as another disk, or swapped after confirmation, the reset is cancelled.

### Restore safety and troubleshooting

- Never rename an arbitrary disk image to a supported firmware extension.
- Never disconnect the device while firmware is being written.
- Connect exactly one iPod touch before opening the restore workflow. MacTunes
  refuses to continue when zero or multiple identifiable devices are present.
- MacTunes records the selected iPod touch UDID before confirmation and checks
  for the same UDID again immediately before launching `idevicerestore`.
- Disk-mode firmware flashing is disabled; MacTunes never lets
  `ipodpatcher` auto-select a disk.
- If MacTunes reports that a restore tool is missing, install
  `idevicerestore` and the `idevice_id` utility, then restart MacTunes.
- If permission is denied, configure your distribution's USB/device access
  rules for the restore tool. Do not run the entire MacTunes application as root.
- Firmware restore output is shown in the completion error dialog when a tool
  fails, making model mismatch and recovery-mode errors visible.

---

## 12. CD Ripping

On macOS, MacTunes asks `drutil` for disc status/TOC and uses the AIFF/WAV
tracks macOS mounts under `/Volumes`, converting them with `afconvert`.
macOS output formats are AIFF, WAV, AAC, and M4A. Linux uses libcdio-paranoia.

### Metadata

Track metadata (album title, artist, track names, genre, year) is fetched automatically from the CDDB network (gnudb.gnudb.org) before ripping begins. An internet connection is required for CDDB lookup; if it fails, tracks are named "Track 01", "Track 02", etc.

### How to rip a CD

1. Insert an audio CD into your drive.
2. Go to **Device → Rip CD…**
3. The disc is read and metadata is fetched. A dialog shows:
   - **Output folder** — where ripped files are saved (defaults to your Music directory)
   - **Format** — choose from FLAC, MP3, OGG Vorbis, AAC, or WAV
   - **Disc info** — album name, artist, and track count from CDDB
4. Click **Rip**. Progress is shown in the status bar track by track.
5. When complete, the ripped files are automatically imported into your library.

### Output file naming

Files are saved in the following structure inside your chosen output folder:
```
<Artist>/<Album>/NN - Track Title.<format>
```
For example:
```
Pink Floyd/The Dark Side of the Moon/01 - Speak to Me.flac
```

### Format guide

| Format | Notes |
|--------|-------|
| FLAC | Lossless — recommended for archiving. Requires `flac`. |
| MP3 | Lossy, universal compatibility. Requires `lame`. |
| OGG Vorbis | Lossy, open format. Requires `oggenc` (vorbis-tools). |
| AAC | Lossy, good quality at low bitrates. Requires `ffmpeg`. |
| WAV | Uncompressed — large files, no metadata. |

### Encoding tools

These command-line tools must be installed for encoding to work; they are
listed in the dependency section of this manual.

---

## 13. CD Burning

On macOS, MacTunes converts each selected track to numbered, stereo 44.1 kHz
16-bit PCM AIFF in a temporary folder and runs `drutil burn -audio` on that
folder. Linux continues to use `wodim`.

### Requirements

- A blank CD-R disc (standard 74-minute / 650 MB).
- macOS `drutil` and `afconvert` (both supplied by macOS), or `wodim` and
  `ffmpeg` on Linux.

### How to burn a CD

1. Insert a blank CD-R into your drive.
2. Select the tracks you want to burn in the library. A standard audio CD holds up to approximately 74 minutes of audio.
3. Go to **Device → Burn Disc…**
4. A confirmation dialog shows the number of tracks and the drive that will be used.
5. Click **Yes** to start burning.
6. Progress is shown in the status bar. Do not eject the disc or shut down the computer while burning.

### Notes

- Track order on the CD matches the order you see in the track list at the time of burning.
- Source formats readable by `afconvert` can be burned; they are converted to
  Red Book-compatible AIFF before `drutil` is called. macOS simulation and
  manual speed selection are rejected because drutil has no portable syntax
  for either option.
- If burning fails, check that the disc is blank and that `wodim` has permission to access the drive (you may need to be in the `cdrom` group: `sudo usermod -aG cdrom $USER`).

### Eject disc

Go to **Device → Eject Disc** to open the drive tray when you are done.

---

## 14. Apple SuperDrive

The Apple USB SuperDrive is a slim external optical drive sold by Apple. On Linux it requires a one-time initialisation command each time it is plugged in, because the drive starts in a locked state by default.

### One-time unlock (manual)

```bash
sudo apt install sg3-utils
sg_raw /dev/sr0 EA 00 00 00 00 00 01
```

Replace `/dev/sr0` with the actual device path if different (check `lsblk` or `dmesg | tail` after plugging in).

### Automatic unlock with a udev rule (recommended)

Create the file `/etc/udev/rules.d/71-apple-superdrive.rules` with the following content:

```udev
ACTION=="add", ATTRS{idVendor}=="05ac", ATTRS{idProduct}=="8406", \
    RUN+="/usr/bin/sg_raw /dev/$kernel EA 00 00 00 00 00 01"
```

After saving the file, reload the rules:
```bash
sudo udevadm control --reload-rules
```

From then on, the SuperDrive unlocks automatically whenever it is plugged in.

### Using the SuperDrive in MacTunes

Once unlocked, the SuperDrive appears as `/dev/sr0` (or `/dev/sr1` if another drive is already present) and MacTunes detects it automatically alongside any internal drives. All ripping and burning features work identically to any other CD drive.

---

## 15. Supported Audio Formats

| Format | Extension(s) | Notes |
|--------|-------------|-------|
| MP3 | `.mp3` | MPEG Layer 3, most common format |
| AAC / M4A | `.aac`, `.m4a` | MPEG-4 audio, iTunes native format |
| FLAC | `.flac` | Free Lossless Audio Codec |
| OGG Vorbis | `.ogg` | Open lossy format |
| WAV | `.wav` | Uncompressed PCM |
| AIFF | `.aiff`, `.aif` | Apple uncompressed, equivalent to WAV |
| WMA | `.wma` | Windows Media Audio |
| Opus | `.opus` | Modern high-quality lossy codec |
| APE (Monkey's Audio) | `.ape` | Lossless, less common |
| ALAC | `.m4a` | Apple Lossless Audio Codec |

GStreamer handles playback of all formats. TagLib handles metadata reading. Not all formats support all tag fields; unsupported fields are stored as empty strings.

---

## 16. Keyboard and Mouse Reference

| Action | Input |
|--------|-------|
| Play selected track | Double-click |
| Play / Pause | Click ▶/⏸ button |
| Skip to next track | Click ⏭ |
| Skip to previous track | Click ⏮ |
| Seek within track | Drag seek bar |
| Adjust volume | Drag volume slider |
| Select a track | Click |
| Select a range of tracks | Shift+click |
| Add/remove a track from selection | Ctrl+click |
| Open context menu | Right-click |
| Add music | Click **+ Add Music** button |
| Search | Type in the search bar |
| Clear search | Delete all text in the search bar |
| Import files by drag and drop | Drag from file manager onto track list |

---

## 17. File Locations

| Path | Purpose |
|------|---------|
| `~/.local/share/mactunes/library.db` | SQLite library database (tracks, playlists) |
| `~/Music/` | Default output folder for CD ripping |
| `/tmp/mactunes_rip_*.wav` | Temporary WAV files created during CD ripping (deleted automatically) |
| `/tmp/mactunes_burn_*.wav` | Temporary WAV files created during CD burning (deleted automatically) |

The library database contains only metadata and file paths — your audio files are never moved or copied by MacTunes (except when syncing to an iPod or ripping a CD, where new files are intentionally created).

---

## 18. Dependencies and Build Instructions

### Runtime dependencies

| Library | Purpose |
|---------|---------|
| GTK 3 | Graphical user interface |
| GStreamer 1.0 | Audio playback engine |
| libVLC | Internet radio stream playback |
| TagLib | Reading and writing audio file metadata |
| SQLite 3 | Library database storage |
| libgpod | iPod database reading and writing |
| libcdio (Linux only) | CD device access and TOC reading |
| libcdio-paranoia (Linux only) | Error-corrected CD audio ripping |
| libcddb (Linux only) | CDDB/GnuDB metadata lookup |

### External tools (must be in PATH)

| Tool | Purpose | Package name |
|------|---------|-------------|
| `lame` | MP3 encoding during rip | `lame` |
| `flac` | FLAC encoding during rip | `flac` |
| `oggenc` | OGG encoding during rip | `vorbis-tools` |
| `ffmpeg` | AAC encoding during rip; audio conversion for burning | `ffmpeg` |
| `drutil` | macOS CD status, TOC, eject, and burning | supplied by macOS |
| `afconvert` | macOS CD conversion and encoding | supplied by macOS |
| `sg_raw` | Apple SuperDrive unlock | `sg3-utils` |
| `idevicerestore` | Restore iPod touch from an Apple IPSW | `idevicerestore` |

### CMake build options

```bash
# Optional integrations (all names are also accepted by buildMac.sh)
cmake .. -DMACTUNES_ENABLE_IPOD=OFF \
         -DMACTUNES_ENABLE_CD=OFF \
         -DMACTUNES_ENABLE_RADIO=OFF

# Debug build (includes symbols, no optimisation)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release build (optimised, default)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Install to a custom prefix
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make install
```

---

## 19. Troubleshooting

### The program won't start / GStreamer error

Ensure the GStreamer plugin packages are installed. On Ubuntu:
```bash
sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
                 gstreamer1.0-plugins-ugly gstreamer1.0-libav
```

### A track plays but has no sound

Check that GStreamer's audio output sink can find your sound card. Test with:
```bash
gst-launch-1.0 audiotestsrc ! autoaudiosink
```
If that also has no sound, the issue is with your system audio (PulseAudio/PipeWire), not MacTunes.

### "No iPod detected" when an iPod is plugged in

- Check that the iPod is actually mounted: run `lsblk` or look in your file manager.
- Make sure the mount point contains an `iPod_Control` or `iTunes_Control` directory.
- For iPod touch, you need `ifuse` — see [iPod Sync](#10-ipod-sync).
- Some iPod models require the filesystem to be unlocked with iTunes on Windows or macOS before MacTunes can access them.
- PC/FAT-formatted iPods are supported. Volume labels containing spaces are
  decoded automatically when MacTunes reads the Linux mount table. MacTunes
  also accepts older models without a reported UUID, handles control-directory
  capitalization, writes the additional iPod Shuffle database, and flushes FAT
  filesystem changes after syncing. The device must be mounted with write access.

### iPod sync fails or the iPod database is not updated

- Do not unplug the iPod until the status bar shows the sync is complete.
- Make sure you have write access to the iPod mount point.
- The iPod's filesystem must not be full. Check free space in the status bar after connecting.

### CD rip produces no tracks

- Confirm the disc is an audio CD: run `cd-info /dev/sr0` to inspect it.
- Make sure `libcdio` can see the drive: `cdio-info` should list it.
- For the Apple SuperDrive, make sure you have unlocked it first (see [Apple SuperDrive](#14-apple-superdrive)).

### CD burn fails

- Confirm `wodim` is installed: `which wodim`
- Confirm you are in the `cdrom` group: `groups $USER`
  - If not: `sudo usermod -aG cdrom $USER` then log out and back in.
- The disc must be a blank CD-R (CD-RW discs may work but are less reliable).

### Metadata is wrong or missing after import

TagLib reads whatever tags are embedded in the file. If a file has no tags, all fields will be empty. You can edit tags in a dedicated tag editor (such as MusicBrainz Picard or EasyTag) and then re-import the file.

### Permission denied when clicking Connect iPod

This error appears if MacTunes tries to probe a restricted kernel filesystem. It is harmless and is suppressed automatically — MacTunes will still scan the correct removable-media locations. If it persists, make sure your user account has read access to your media mount directories:
```bash
ls /run/media/$USER/
```

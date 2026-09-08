#include "iPodSync.h"
#include <gpod/itdb.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <sys/statvfs.h>

namespace fs = std::filesystem;

// Convert an iPod-style path (colon-separated, e.g. ":iPod_Control:Music:F00:foo.mp3")
// to a real filesystem path under the given mount point.
static std::string ipodPathToFs(const std::string& mount, const gchar* ipod_path) {
    if (!ipod_path) return {};
    std::string p = ipod_path;
    std::replace(p.begin(), p.end(), ':', '/');
    // p now starts with '/' so mount + p gives the full path
    return mount + p;
}

iPodSync::iPodSync() {}
iPodSync::~iPodSync() { disconnect(); }

// Returns true if path has an iPod_Control or iTunes_Control subdirectory.
// Uses the error_code overload so it never throws on EACCES or other errors.
static bool looksLikeIPod(const std::string& mp) {
    std::error_code ec;
    return fs::exists(mp + "/iPod_Control",  ec) ||
           fs::exists(mp + "/iTunes_Control", ec);
}

// Returns true if this mount-point prefix is a kernel virtual filesystem we
// should never probe (they are not removable media and may deny stat calls).
static bool isKernelFs(const std::string& mp) {
    static const char* prefixes[] = {
        "/proc", "/sys", "/dev", "/run/lock", "/run/user",
        "/snap", "/boot", "/tmp", nullptr
    };
    for (int i = 0; prefixes[i]; ++i)
        if (mp.compare(0, strlen(prefixes[i]), prefixes[i]) == 0)
            return true;
    return false;
}

// /proc/mounts escapes spaces and a few other characters as octal sequences
// (for example "MY\040IPOD"). Decode them before probing the mount point.
static std::string decodeMountField(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 3 < value.size() &&
            value[i + 1] >= '0' && value[i + 1] <= '7' &&
            value[i + 2] >= '0' && value[i + 2] <= '7' &&
            value[i + 3] >= '0' && value[i + 3] <= '7') {
            int byte = (value[i + 1] - '0') * 64 +
                       (value[i + 2] - '0') * 8 +
                       (value[i + 3] - '0');
            decoded.push_back(static_cast<char>(byte));
            i += 3;
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

static bool resolveWholeDisk(const std::string& mount, std::string& devicePath,
                             uint64_t& deviceNumber) {
    struct stat mountStat {};
    if (stat(mount.c_str(), &mountStat) != 0) return false;
    fs::path sysDevice = fs::path("/sys/dev/block") /
        (std::to_string(major(mountStat.st_dev)) + ":" +
         std::to_string(minor(mountStat.st_dev)));
    std::error_code ec;
    fs::path resolved = fs::canonical(sysDevice, ec);
    if (ec || resolved.empty()) return false;
    if (fs::exists(resolved / "partition", ec) && !ec)
        resolved = resolved.parent_path();
    devicePath = "/dev/" + resolved.filename().string();
    struct stat deviceStat {};
    if (stat(devicePath.c_str(), &deviceStat) != 0 || !S_ISBLK(deviceStat.st_mode))
        return false;
    deviceNumber = static_cast<uint64_t>(deviceStat.st_rdev);
    return true;
}

std::vector<std::string> iPodSync::detectMountPoints() {
    std::vector<std::string> mounts;

    // ── 1. Parse /proc/mounts ────────────────────────────────────────────────
    // Skip kernel virtual filesystems — probing them can raise EACCES.
    {
        std::ifstream mf("/proc/mounts");
        std::string   line;
        while (std::getline(mf, line)) {
            std::istringstream iss(line);
            std::string dev, mp;
            iss >> dev >> mp;
            mp = decodeMountField(mp);
            if (mp.empty() || isKernelFs(mp)) continue;
            if (looksLikeIPod(mp))
                mounts.push_back(mp);
        }
    }

    // ── 2. Scan common removable-media directories ───────────────────────────
    // /run/media is the standard on systemd-based distros;
    // /media and /mnt are also common.
    const std::vector<std::string> bases = { "/media", "/mnt", "/run/media" };
    for (const auto& base : bases) {
        std::error_code ec;
        if (!fs::exists(base, ec) || ec) continue;

        try {
            for (const auto& entry : fs::directory_iterator(base, ec)) {
                if (ec) { ec.clear(); continue; }
                if (!entry.is_directory(ec) || ec) { ec.clear(); continue; }
                std::string mp = entry.path().string();

                if (looksLikeIPod(mp)) {
                    if (std::find(mounts.begin(), mounts.end(), mp) == mounts.end())
                        mounts.push_back(mp);
                }
                // One level deeper: /run/media/<username>/<device>
                try {
                    for (const auto& sub : fs::directory_iterator(mp, ec)) {
                        if (ec) { ec.clear(); continue; }
                        if (!sub.is_directory(ec) || ec) { ec.clear(); continue; }
                        std::string smp = sub.path().string();
                        if (looksLikeIPod(smp)) {
                            if (std::find(mounts.begin(), mounts.end(), smp) == mounts.end())
                                mounts.push_back(smp);
                        }
                    }
                } catch (...) {}
            }
        } catch (...) {}
    }

    return mounts;
}

bool iPodSync::connect(const std::string& mount_point) {
    disconnect();
    last_error_.clear();
    GError* err = nullptr;
    itdb_ = itdb_parse(mount_point.c_str(), &err);
    if (!itdb_) {
        if (err) {
            last_error_ = err->message;
            g_error_free(err);
        } else {
            last_error_ = "Unknown error connecting to iPod at " + mount_point;
        }
        return false;
    }
    mount_point_ = mount_point;
    if (itdb_->device) {
        gchar* uuid = itdb_device_get_uuid(itdb_->device);
        if (uuid) {
            connected_uuid_ = uuid;
            g_free(uuid);
        }
    }
    // Older PC/FAT-formatted iPods often do not expose a UUID through
    // libgpod. That is not required for browsing or syncing.
    std::string ignoredPath;
    // Physical identity is best-effort at connection time and becomes
    // mandatory only before a destructive database reset.
    resolveWholeDisk(mount_point_, ignoredPath, mounted_device_number_);
    return true;
}

void iPodSync::disconnect() {
    if (itdb_) {
        itdb_free(itdb_);
        itdb_ = nullptr;
    }
    mount_point_.clear();
    connected_uuid_.clear();
    mounted_device_number_ = 0;
}

iPodInfo iPodSync::getInfo() const {
    iPodInfo info;
    if (!itdb_) return info;
    info.mount_point = mount_point_;

    Itdb_Device* dev = itdb_->device;
    if (dev) {
        const Itdb_IpodInfo* ipod_info = itdb_device_get_ipod_info(dev);
        if (ipod_info) {
            info.model_name = ipod_info->model_number ? ipod_info->model_number : "";
        }
        info.serial_number = connected_uuid_;
    }
    // Count tracks
    info.track_count = static_cast<int>(g_list_length(itdb_->tracks));

    // Disk space via statvfs
    struct statvfs sv;
    if (statvfs(mount_point_.c_str(), &sv) == 0) {
        info.capacity_bytes = sv.f_blocks * sv.f_frsize;
        info.free_bytes     = sv.f_bavail * sv.f_frsize;
    }

    return info;
}

std::vector<Track> iPodSync::getIPodTracks() const {
    std::vector<Track> tracks;
    if (!itdb_) return tracks;

    for (GList* l = itdb_->tracks; l; l = l->next) {
        auto* gt = static_cast<Itdb_Track*>(l->data);
        tracks.push_back(gpodToTrack(gt));
    }
    return tracks;
}

Track iPodSync::gpodToTrack(Itdb_Track* gt) const {
    Track t;
    t.on_ipod      = true;
    t.title        = gt->title        ? gt->title        : "";
    t.artist       = gt->artist       ? gt->artist       : "";
    t.album        = gt->album        ? gt->album        : "";
    t.genre        = gt->genre        ? gt->genre        : "";
    t.year         = gt->year;
    t.track_number = gt->track_nr;
    t.disc_number  = gt->cd_nr;
    t.duration_ms  = gt->tracklen;
    t.bitrate      = gt->bitrate;
    t.sample_rate  = gt->samplerate;
    t.play_count   = gt->playcount;
    t.rating       = gt->rating;
    t.compilation  = gt->compilation != 0;
    t.comment      = gt->comment      ? gt->comment      : "";
    t.composer     = gt->composer     ? gt->composer     : "";
    t.album_artist = gt->albumartist  ? gt->albumartist  : "";

    // Build the full filesystem path on the iPod.
    if (gt->ipod_path)
        t.file_path = ipodPathToFs(mount_point_, gt->ipod_path);

    // Store a unique identifier using dbid
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "%" G_GUINT64_FORMAT, gt->dbid);
    t.ipod_id = id_buf;

    return t;
}

Itdb_Track* iPodSync::trackToGpod(const Track& t) const {
    Itdb_Track* gt = itdb_track_new();
    gt->title       = g_strdup(t.title.c_str());
    gt->artist      = g_strdup(t.artist.c_str());
    gt->album       = g_strdup(t.album.c_str());
    gt->genre       = g_strdup(t.genre.c_str());
    gt->year        = t.year;
    gt->track_nr    = t.track_number;
    gt->cd_nr       = t.disc_number;
    gt->tracklen    = t.duration_ms;
    gt->bitrate     = t.bitrate;
    gt->samplerate  = t.sample_rate;
    gt->playcount   = t.play_count;
    gt->rating      = t.rating;
    gt->compilation = t.compilation ? 1 : 0;
    gt->comment     = g_strdup(t.comment.c_str());
    gt->composer    = g_strdup(t.composer.c_str());
    gt->albumartist = g_strdup(t.album_artist.c_str());

    // Determine media type by extension
    std::string ext = fs::path(t.file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if      (ext == ".mp3")  gt->mediatype = ITDB_MEDIATYPE_AUDIO;
    else if (ext == ".m4a")  gt->mediatype = ITDB_MEDIATYPE_AUDIO;
    else if (ext == ".aac")  gt->mediatype = ITDB_MEDIATYPE_AUDIO;
    else if (ext == ".flac") gt->mediatype = ITDB_MEDIATYPE_AUDIO;
    else if (ext == ".aiff" || ext == ".aif") gt->mediatype = ITDB_MEDIATYPE_AUDIO;
    else                     gt->mediatype = ITDB_MEDIATYPE_AUDIO;

    gt->size = static_cast<guint32>(t.file_size);

    return gt;
}

std::string iPodSync::copyFileToIPod(const std::string& src_path, Itdb_Track* gt) const {
    GError* err = nullptr;
    if (!itdb_cp_track_to_ipod(gt, src_path.c_str(), &err)) {
        if (err) {
            last_error_ = err->message;
            g_error_free(err);
        } else {
            last_error_ = "Failed to copy " + src_path + " to iPod";
        }
        return {};
    }
    // After itdb_cp_track_to_ipod the track's ipod_path is populated.
    return gt->ipod_path ? ipodPathToFs(mount_point_, gt->ipod_path) : std::string{};
}

bool iPodSync::addTrack(const Track& track, SyncProgressCallback progress) {
    if (!itdb_) { last_error_ = "Not connected to iPod"; return false; }

    if (progress) progress(0, 1, "Copying: " + track.title);

    Itdb_Track* gt = trackToGpod(track);

    // Add to master playlist first
    Itdb_Playlist* mpl = itdb_playlist_mpl(itdb_);
    itdb_track_add(itdb_, gt, -1);
    itdb_playlist_add_track(mpl, gt, -1);

    // Copy the file
    std::string dest = copyFileToIPod(track.file_path, gt);
    if (dest.empty()) {
        // Must remove from the master playlist first — itdb_track_remove only
        // removes from itdb_->tracks, leaving a dangling pointer in mpl->members.
        itdb_playlist_remove_track(mpl, gt);
        itdb_track_remove(gt);
        return false;
    }

    if (progress) progress(1, 1, "Done: " + track.title);
    return true;
}

bool iPodSync::removeTrack(const std::string& ipod_id) {
    if (!itdb_) { last_error_ = "Not connected"; return false; }

    for (GList* l = itdb_->tracks; l; l = l->next) {
        auto* gt = static_cast<Itdb_Track*>(l->data);
        char id_buf[64];
        snprintf(id_buf, sizeof(id_buf), "%" G_GUINT64_FORMAT, gt->dbid);
        if (ipod_id == id_buf) {
            // Remove from all playlists
            for (GList* pl = itdb_->playlists; pl; pl = pl->next) {
                auto* playlist = static_cast<Itdb_Playlist*>(pl->data);
                itdb_playlist_remove_track(playlist, gt);
            }
            // Delete the file from iPod storage
            if (gt->ipod_path) {
                std::error_code ec;
                fs::remove(ipodPathToFs(mount_point_, gt->ipod_path), ec);
            }
            itdb_track_remove(gt);
            return true;
        }
    }
    last_error_ = "Track not found on iPod: " + ipod_id;
    return false;
}

bool iPodSync::syncPlaylist(const std::string& playlist_name,
                             const std::vector<Track>& tracks,
                             SyncProgressCallback progress) {
    if (!itdb_) { last_error_ = "Not connected"; return false; }

    // Find or create the playlist on the iPod
    Itdb_Playlist* pl = nullptr;
    for (GList* l = itdb_->playlists; l; l = l->next) {
        auto* p = static_cast<Itdb_Playlist*>(l->data);
        if (!itdb_playlist_is_mpl(p) && p->name && playlist_name == p->name) {
            pl = p;
            break;
        }
    }
    if (!pl) {
        pl = itdb_playlist_new(playlist_name.c_str(), false);
        itdb_playlist_add(itdb_, pl, -1);
    } else {
        // Clear existing tracks from the playlist (don't delete from iPod)
        g_list_free(pl->members);
        pl->members = nullptr;
    }

    int total = static_cast<int>(tracks.size());
    for (int i = 0; i < total; ++i) {
        const Track& t = tracks[i];
        if (progress) progress(i, total, "Syncing: " + t.title);

        // Find the track on iPod or add it
        Itdb_Track* gt = nullptr;
        for (GList* l = itdb_->tracks; l; l = l->next) {
            auto* candidate = static_cast<Itdb_Track*>(l->data);
            if (candidate->title && t.title == candidate->title &&
                candidate->artist && t.artist == candidate->artist) {
                gt = candidate;
                break;
            }
        }

        if (!gt) {
            gt = trackToGpod(t);
            itdb_track_add(itdb_, gt, -1);
            itdb_playlist_add_track(itdb_playlist_mpl(itdb_), gt, -1);
            if (copyFileToIPod(t.file_path, gt).empty()) continue;
        }
        itdb_playlist_add_track(pl, gt, -1);
    }

    if (progress) progress(total, total, "Playlist synced");
    return true;
}

bool iPodSync::writeDatabase() {
    if (!itdb_) { last_error_ = "Not connected"; return false; }
    GError* err = nullptr;
    bool ok = itdb_write(itdb_, &err);
    if (!ok && err) {
        last_error_ = err->message;
        g_error_free(err);
    }
    return ok;
}

bool iPodSync::resetMusicDatabase(const std::string& display_name) {
    if (!itdb_ || mount_point_.empty()) {
        last_error_ = "Connect a mounted disk-mode iPod before resetting it.";
        return false;
    }

    std::string mount = mount_point_;
    std::string expectedUuid = connected_uuid_;
    GError* parseError = nullptr;
    Itdb_iTunesDB* current = itdb_parse(mount.c_str(), &parseError);
    std::string model;
    std::string currentUuid;
    if (current && current->device) {
        const Itdb_IpodInfo* info = itdb_device_get_ipod_info(current->device);
        if (info && info->model_number) model = info->model_number;
        gchar* uuid = itdb_device_get_uuid(current->device);
        if (uuid) {
            currentUuid = uuid;
            g_free(uuid);
        }
    }
    if (current) itdb_free(current);
    if (parseError) g_error_free(parseError);
    if (model.empty()) {
        last_error_ = "The iPod model could not be identified; reset was cancelled.";
        return false;
    }
    std::string currentDevicePath;
    uint64_t currentDeviceNumber = 0;
    if (mounted_device_number_ == 0 ||
        !resolveWholeDisk(mount, currentDevicePath, currentDeviceNumber) ||
        currentDeviceNumber != mounted_device_number_ ||
        (!expectedUuid.empty() && currentUuid != expectedUuid)) {
        last_error_ = "The connected device identity changed; reset was cancelled.";
        return false;
    }

    disconnect();
    std::error_code ec;
    fs::remove_all(fs::path(mount) / "iPod_Control", ec);
    if (ec) {
        last_error_ = "Could not erase iPod_Control: " + ec.message();
        return false;
    }
    ec.clear();
    fs::remove_all(fs::path(mount) / "iTunes_Control", ec);
    if (ec) {
        last_error_ = "Could not erase iTunes_Control: " + ec.message();
        return false;
    }

    GError* err = nullptr;
    if (!itdb_init_ipod(mount.c_str(), model.c_str(), display_name.c_str(), &err)) {
        last_error_ = err ? err->message : "libgpod could not initialize the iPod.";
        if (err) g_error_free(err);
        return false;
    }
    return connect(mount);
}

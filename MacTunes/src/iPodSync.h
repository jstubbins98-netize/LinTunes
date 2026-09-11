#pragma once
#include "Track.h"
#include <string>
#include <vector>
#include <functional>

// Forward-declare libgpod types to avoid pulling in the header everywhere
struct _Itdb_iTunesDB;
struct _Itdb_Track;
struct _Itdb_Playlist;

using SyncProgressCallback = std::function<void(int current, int total, const std::string& msg)>;

struct iPodInfo {
    std::string mount_point;
    std::string model_name;
    std::string serial_number;
    std::string firmware_version;
    uint64_t    capacity_bytes    = 0;
    uint64_t    free_bytes        = 0;
    int         track_count       = 0;
};

class iPodSync {
public:
    iPodSync();
    ~iPodSync();

    // Detect mounted iPods — returns a list of mount points
    static std::vector<std::string> detectMountPoints();

    // Connect to an iPod at a given mount point
    bool connect(const std::string& mount_point);
    void disconnect();
    bool isConnected() const { return itdb_ != nullptr; }

    iPodInfo getInfo() const;

    // Retrieve tracks currently on the iPod
    std::vector<Track> getIPodTracks() const;

    // Sync: add a local library track to the iPod
    bool addTrack(const Track& track, SyncProgressCallback progress = nullptr);

    // Remove a track from the iPod (by ipod_id stored in library)
    bool removeTrack(const std::string& ipod_id);

    // Sync an entire playlist to the iPod
    bool syncPlaylist(const std::string& playlist_name,
                      const std::vector<Track>& tracks,
                      SyncProgressCallback progress = nullptr);

    // Write all pending changes and close the database
    bool writeDatabase();

    // Erase media/database content and create a fresh libgpod database.
    // This does not repartition the device or flash firmware.
    bool resetMusicDatabase(const std::string& display_name = "iPod");
    const std::string& lastError() const { return last_error_; }

private:
    _Itdb_iTunesDB* itdb_        = nullptr;
    std::string     mount_point_;
    std::string     connected_uuid_;
    uint64_t        mounted_device_number_ = 0;
    mutable std::string last_error_;

    _Itdb_Track*    trackToGpod(const Track& t) const;
    Track           gpodToTrack(_Itdb_Track* gt) const;
    std::string     copyFileToIPod(const std::string& src_path,
                                   _Itdb_Track* gpod_track) const;
};

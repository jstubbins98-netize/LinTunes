#pragma once
#include "Track.h"
#include <string>
#include <vector>
#include <functional>

using CDProgressCallback = std::function<void(int track_num, int total_tracks,
                                               int sector, int total_sectors,
                                               const std::string& msg)>;

struct CDTrackInfo {
    int         track_number  = 0;
    int         duration_secs = 0;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    int         year          = 0;
};

struct CDInfo {
    std::string              disc_id;
    std::string              album_title;
    std::string              artist;
    int                      year         = 0;
    std::string              genre;
    std::vector<CDTrackInfo> tracks;
    bool                     metadata_ok  = false;
};

class CDManager {
public:
#ifdef __APPLE__
    explicit CDManager(const std::string& device = "/dev/disk2");
#else
    explicit CDManager(const std::string& device = "/dev/cdrom");
#endif
    ~CDManager();

    // Device management
    void setDevice(const std::string& device) { device_ = device; }
    const std::string& device() const { return device_; }

    // Detect CD drives, including Apple SuperDrive connected via USB
    static std::vector<std::string> detectDrives();

    // Check if an audio CD is present
    bool hasAudioCD() const;

    // Read CD table of contents
    CDInfo readTOC() const;

    // Fetch metadata from CDDB/MusicBrainz
    CDInfo fetchMetadata() const;

    // Rip one track to a WAV file, then encode to target format
    // Supported formats: "mp3", "flac", "ogg", "aac", "wav"
    bool ripTrack(int track_num,
                  const std::string& output_path,
                  const std::string& format = "flac",
                  int quality = 8,
                  CDProgressCallback progress = nullptr);

    // Rip all tracks
    int ripAllTracks(const std::string& output_dir,
                     const std::string& format = "flac",
                     int quality = 8,
                     CDProgressCallback progress = nullptr,
                     const CDInfo* info = nullptr);

    // Burn an audio CD from a list of audio files
    // Requires cdrecord/wodim to be installed
    bool burnCD(const std::vector<std::string>& audio_files,
                int write_speed = 0,               // 0 = auto
                bool simulate   = false,
                CDProgressCallback progress = nullptr);

    // Eject the disc
    bool eject();

    const std::string& lastError() const { return last_error_; }

private:
    std::string device_;
    mutable std::string last_error_;

    std::string buildOutputFilename(const CDInfo& info, int track_num,
                                    const std::string& dir,
                                    const std::string& format) const;
    bool encodeWav(const std::string& wav_path,
                   const std::string& out_path,
                   const std::string& format,
                   int quality) const;
};

#pragma once
#include "Track.h"
#include "Database.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

// Metadata read from an audio file on disk
struct FileMetadata {
    bool        valid_audio  = false;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    int         year         = 0;
    int         track_number = 0;
    int         disc_number  = 1;
    int         duration_ms  = 0;
    int         bitrate      = 0;
    int         sample_rate  = 0;
    int64_t     file_size    = 0;
    std::string file_format;
    std::string comment;
    std::string composer;
    std::string album_artist;
    bool        compilation  = false;
};

using ProgressCallback = std::function<void(int current, int total, const std::string& current_file)>;

class Library {
public:
    explicit Library(Database& db);

    // Import a single file or recursively scan a directory
    bool importFile(const std::string& path, Track& out_track);
    int  importDirectory(const std::string& dir_path,
                         bool recursive = true,
                         ProgressCallback progress = nullptr);

    // Read metadata from a file using taglib
    static FileMetadata readMetadata(const std::string& path);
    static bool         isSupportedFormat(const std::string& path);

    // Retrieve data from db
    std::vector<Track> getAllTracks()                          const;
    std::vector<Track> searchTracks(const std::string& query) const;
    std::vector<Track> getTracksByArtist(const std::string& a) const;
    std::vector<Track> getTracksByAlbum (const std::string& a) const;
    std::vector<Track> getTracksByGenre (const std::string& g) const;

    std::vector<std::string> getAllArtists() const;
    std::vector<std::string> getAllAlbums()  const;
    std::vector<std::string> getAllGenres()  const;

    bool removeTrack(int64_t id, bool delete_file = false);

    // Playlists
    bool createPlaylist(const std::string& name, int64_t& id);
    bool deletePlaylist(int64_t id);
    bool addToPlaylist(int64_t playlist_id, int64_t track_id, int position);
    bool removeFromPlaylist(int64_t playlist_id, int64_t track_id);
    std::vector<Track>                         getPlaylistTracks(int64_t id) const;
    std::vector<std::pair<int64_t,std::string>> getAllPlaylists()             const;

    Database& db() { return db_; }

private:
    Database& db_;

    static std::vector<std::string> collectAudioFiles(const std::string& dir, bool recursive);
};

#pragma once
#include "Track.h"
#include <string>
#include <vector>
#include <functional>
#include <sqlite3.h>

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    bool open();
    void close();
    bool isOpen() const { return db_ != nullptr; }

    // Library operations
    bool        addTrack(Track& track);           // sets track.id on success
    bool        updateTrack(const Track& track);
    bool        removeTrack(int64_t id);
    bool        trackExists(const std::string& path) const;
    Track       getTrack(int64_t id) const;

    std::vector<Track> getAllTracks() const;
    std::vector<Track> searchTracks(const std::string& query) const;
    std::vector<Track> getTracksByArtist(const std::string& artist) const;
    std::vector<Track> getTracksByAlbum(const std::string& album) const;
    std::vector<Track> getTracksByGenre(const std::string& genre) const;

    std::vector<std::string> getAllArtists() const;
    std::vector<std::string> getAllAlbums() const;
    std::vector<std::string> getAllGenres() const;

    // Playlists
    bool               createPlaylist(const std::string& name, int64_t& playlist_id);
    bool               deletePlaylist(int64_t playlist_id);
    bool               addTrackToPlaylist(int64_t playlist_id, int64_t track_id, int position);
    bool               removeTrackFromPlaylist(int64_t playlist_id, int64_t track_id);
    std::vector<Track> getPlaylistTracks(int64_t playlist_id) const;
    std::vector<std::pair<int64_t, std::string>> getAllPlaylists() const;

    // Stats
    int    getTotalTrackCount() const;
    int64_t getTotalDuration() const;   // milliseconds

    const std::string& lastError() const { return last_error_; }

private:
    std::string db_path_;
    sqlite3*    db_          = nullptr;
    mutable std::string last_error_;

    bool   execSQL(const std::string& sql);
    bool   createSchema();
    Track  rowToTrack(sqlite3_stmt* stmt) const;
    std::vector<Track> queryTracks(const std::string& sql, const std::vector<std::string>& params = {}) const;
};

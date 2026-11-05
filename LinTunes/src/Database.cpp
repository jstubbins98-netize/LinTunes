#include "Database.h"
#include <iostream>
#include <sstream>

Database::Database(const std::string& path) : db_path_(path) {}

Database::~Database() {
    close();
}

bool Database::open() {
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        db_ = nullptr;
        return false;
    }
    execSQL("PRAGMA journal_mode=WAL;");
    execSQL("PRAGMA foreign_keys=ON;");
    return createSchema();
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::execSQL(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        last_error_ = err ? err : "unknown error";
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool Database::createSchema() {
    const std::string schema = R"(
        CREATE TABLE IF NOT EXISTS tracks (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            title        TEXT,
            artist       TEXT,
            album        TEXT,
            genre        TEXT,
            year         INTEGER DEFAULT 0,
            track_number INTEGER DEFAULT 0,
            disc_number  INTEGER DEFAULT 1,
            duration_ms  INTEGER DEFAULT 0,
            bitrate      INTEGER DEFAULT 0,
            sample_rate  INTEGER DEFAULT 0,
            file_size    INTEGER DEFAULT 0,
            file_path    TEXT UNIQUE NOT NULL,
            file_format  TEXT,
            comment      TEXT,
            composer     TEXT,
            album_artist TEXT,
            play_count   INTEGER DEFAULT 0,
            rating       INTEGER DEFAULT 0,
            on_ipod      INTEGER DEFAULT 0,
            ipod_id      TEXT DEFAULT '',
            compilation  INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS playlists (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS playlist_tracks (
            playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
            track_id    INTEGER NOT NULL REFERENCES tracks(id)    ON DELETE CASCADE,
            position    INTEGER DEFAULT 0,
            PRIMARY KEY (playlist_id, track_id)
        );

        CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist);
        CREATE INDEX IF NOT EXISTS idx_tracks_album  ON tracks(album);
        CREATE INDEX IF NOT EXISTS idx_tracks_genre  ON tracks(genre);
    )";
    return execSQL(schema);
}

Track Database::rowToTrack(sqlite3_stmt* stmt) const {
    Track t;
    t.id           = sqlite3_column_int64(stmt, 0);
    auto col_text  = [&](int i) -> std::string {
        const unsigned char* v = sqlite3_column_text(stmt, i);
        return v ? reinterpret_cast<const char*>(v) : "";
    };
    t.title        = col_text(1);
    t.artist       = col_text(2);
    t.album        = col_text(3);
    t.genre        = col_text(4);
    t.year         = sqlite3_column_int(stmt, 5);
    t.track_number = sqlite3_column_int(stmt, 6);
    t.disc_number  = sqlite3_column_int(stmt, 7);
    t.duration_ms  = sqlite3_column_int(stmt, 8);
    t.bitrate      = sqlite3_column_int(stmt, 9);
    t.sample_rate  = sqlite3_column_int(stmt, 10);
    t.file_size    = sqlite3_column_int64(stmt, 11);
    t.file_path    = col_text(12);
    t.file_format  = col_text(13);
    t.comment      = col_text(14);
    t.composer     = col_text(15);
    t.album_artist = col_text(16);
    t.play_count   = sqlite3_column_int(stmt, 17);
    t.rating       = sqlite3_column_int(stmt, 18);
    t.on_ipod      = sqlite3_column_int(stmt, 19) != 0;
    t.ipod_id      = col_text(20);
    t.compilation  = sqlite3_column_int(stmt, 21) != 0;
    return t;
}

std::vector<Track> Database::queryTracks(const std::string& sql, const std::vector<std::string>& params) const {
    std::vector<Track> results;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return results;
    }
    for (int i = 0; i < (int)params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(rowToTrack(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

bool Database::addTrack(Track& track) {
    const std::string sql = R"(
        INSERT INTO tracks
            (title, artist, album, genre, year, track_number, disc_number,
             duration_ms, bitrate, sample_rate, file_size, file_path, file_format,
             comment, composer, album_artist, play_count, rating, on_ipod, ipod_id, compilation)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_bind_text(stmt, 1,  track.title.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2,  track.artist.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3,  track.album.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4,  track.genre.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 5,  track.year);
    sqlite3_bind_int (stmt, 6,  track.track_number);
    sqlite3_bind_int (stmt, 7,  track.disc_number);
    sqlite3_bind_int (stmt, 8,  track.duration_ms);
    sqlite3_bind_int (stmt, 9,  track.bitrate);
    sqlite3_bind_int (stmt, 10, track.sample_rate);
    sqlite3_bind_int64(stmt,11, track.file_size);
    sqlite3_bind_text(stmt, 12, track.file_path.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, track.file_format.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, track.comment.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, track.composer.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, track.album_artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 17, track.play_count);
    sqlite3_bind_int (stmt, 18, track.rating);
    sqlite3_bind_int (stmt, 19, track.on_ipod ? 1 : 0);
    sqlite3_bind_text(stmt, 20, track.ipod_id.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 21, track.compilation ? 1 : 0);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (ok) track.id = sqlite3_last_insert_rowid(db_);
    else    last_error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::updateTrack(const Track& track) {
    const std::string sql = R"(
        UPDATE tracks SET
            title=?, artist=?, album=?, genre=?, year=?, track_number=?, disc_number=?,
            duration_ms=?, bitrate=?, sample_rate=?, file_size=?, file_path=?, file_format=?,
            comment=?, composer=?, album_artist=?, play_count=?, rating=?, on_ipod=?, ipod_id=?, compilation=?
        WHERE id=?
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_bind_text(stmt, 1,  track.title.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2,  track.artist.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3,  track.album.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4,  track.genre.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 5,  track.year);
    sqlite3_bind_int (stmt, 6,  track.track_number);
    sqlite3_bind_int (stmt, 7,  track.disc_number);
    sqlite3_bind_int (stmt, 8,  track.duration_ms);
    sqlite3_bind_int (stmt, 9,  track.bitrate);
    sqlite3_bind_int (stmt, 10, track.sample_rate);
    sqlite3_bind_int64(stmt,11, track.file_size);
    sqlite3_bind_text(stmt, 12, track.file_path.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, track.file_format.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, track.comment.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, track.composer.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, track.album_artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 17, track.play_count);
    sqlite3_bind_int (stmt, 18, track.rating);
    sqlite3_bind_int (stmt, 19, track.on_ipod ? 1 : 0);
    sqlite3_bind_text(stmt, 20, track.ipod_id.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 21, track.compilation ? 1 : 0);
    sqlite3_bind_int64(stmt,22, track.id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) last_error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::removeTrack(int64_t id) {
    const std::string sql = "DELETE FROM tracks WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) last_error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::trackExists(const std::string& path) const {
    const std::string sql = "SELECT COUNT(*) FROM tracks WHERE file_path=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count > 0;
}

Track Database::getTrack(int64_t id) const {
    const std::string sql = "SELECT * FROM tracks WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, id);
    Track t;
    if (sqlite3_step(stmt) == SQLITE_ROW) t = rowToTrack(stmt);
    sqlite3_finalize(stmt);
    return t;
}

std::vector<Track> Database::getAllTracks() const {
    return queryTracks("SELECT * FROM tracks ORDER BY artist, album, disc_number, track_number");
}

std::vector<Track> Database::searchTracks(const std::string& query) const {
    std::string q = "%" + query + "%";
    const std::string sql = R"(
        SELECT * FROM tracks
        WHERE title LIKE ? OR artist LIKE ? OR album LIKE ? OR genre LIKE ?
        ORDER BY artist, album, track_number
    )";
    return queryTracks(sql, {q, q, q, q});
}

std::vector<Track> Database::getTracksByArtist(const std::string& artist) const {
    return queryTracks("SELECT * FROM tracks WHERE artist=? ORDER BY album, disc_number, track_number", {artist});
}

std::vector<Track> Database::getTracksByAlbum(const std::string& album) const {
    return queryTracks("SELECT * FROM tracks WHERE album=? ORDER BY disc_number, track_number", {album});
}

std::vector<Track> Database::getTracksByGenre(const std::string& genre) const {
    return queryTracks("SELECT * FROM tracks WHERE genre=? ORDER BY artist, album, track_number", {genre});
}

std::vector<std::string> Database::getAllArtists() const {
    std::vector<std::string> results;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT DISTINCT artist FROM tracks WHERE artist!='' ORDER BY artist", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(stmt, 0);
        if (v) results.emplace_back(reinterpret_cast<const char*>(v));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::string> Database::getAllAlbums() const {
    std::vector<std::string> results;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT DISTINCT album FROM tracks WHERE album!='' ORDER BY album", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(stmt, 0);
        if (v) results.emplace_back(reinterpret_cast<const char*>(v));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::string> Database::getAllGenres() const {
    std::vector<std::string> results;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT DISTINCT genre FROM tracks WHERE genre!='' ORDER BY genre", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(stmt, 0);
        if (v) results.emplace_back(reinterpret_cast<const char*>(v));
    }
    sqlite3_finalize(stmt);
    return results;
}

bool Database::createPlaylist(const std::string& name, int64_t& playlist_id) {
    const std::string sql = "INSERT INTO playlists (name) VALUES (?)";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (ok) playlist_id = sqlite3_last_insert_rowid(db_);
    else    last_error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deletePlaylist(int64_t playlist_id) {
    const std::string sql = "DELETE FROM playlists WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, playlist_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::addTrackToPlaylist(int64_t playlist_id, int64_t track_id, int position) {
    const std::string sql = "INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position) VALUES (?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, playlist_id);
    sqlite3_bind_int64(stmt, 2, track_id);
    sqlite3_bind_int  (stmt, 3, position);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::removeTrackFromPlaylist(int64_t playlist_id, int64_t track_id) {
    const std::string sql = "DELETE FROM playlist_tracks WHERE playlist_id=? AND track_id=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, playlist_id);
    sqlite3_bind_int64(stmt, 2, track_id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Track> Database::getPlaylistTracks(int64_t playlist_id) const {
    const std::string sql = R"(
        SELECT t.* FROM tracks t
        JOIN playlist_tracks pt ON t.id = pt.track_id
        WHERE pt.playlist_id=?
        ORDER BY pt.position
    )";
    sqlite3_stmt* stmt = nullptr;
    std::vector<Track> results;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, playlist_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) results.push_back(rowToTrack(stmt));
    sqlite3_finalize(stmt);
    return results;
}

std::vector<std::pair<int64_t, std::string>> Database::getAllPlaylists() const {
    std::vector<std::pair<int64_t, std::string>> results;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id, name FROM playlists ORDER BY name", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t pid = sqlite3_column_int64(stmt, 0);
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        results.emplace_back(pid, name ? reinterpret_cast<const char*>(name) : "");
    }
    sqlite3_finalize(stmt);
    return results;
}

int Database::getTotalTrackCount() const {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM tracks", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int64_t Database::getTotalDuration() const {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT SUM(duration_ms) FROM tracks", -1, &stmt, nullptr);
    int64_t total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) total = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return total;
}

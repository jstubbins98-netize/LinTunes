#include "Library.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <sys/stat.h>

// TagLib headers for metadata reading
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/audioproperties.h>
// MPEG/ID3v2 — used for disc number, album artist, composer extraction
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>

namespace fs = std::filesystem;

Library::Library(Database& db) : db_(db) {}

static const std::vector<std::string> SUPPORTED_EXTENSIONS = {
    ".mp3", ".m4a", ".aac", ".flac", ".ogg", ".wav", ".aiff", ".aif",
    ".wma", ".opus", ".ape", ".alac"
};

bool Library::isSupportedFormat(const std::string& path) {
    fs::path file_path(path);
    const std::string filename = file_path.filename().string();
    // macOS creates AppleDouble resource-fork files such as ._Song.mp3 on
    // non-Apple filesystems. They have an audio extension but are not songs.
    if (filename.rfind("._", 0) == 0) return false;

    std::string ext = file_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& e : SUPPORTED_EXTENSIONS) {
        if (ext == e) return true;
    }
    return false;
}

FileMetadata Library::readMetadata(const std::string& path) {
    FileMetadata meta;
    meta.file_format = fs::path(path).extension().string();
    std::transform(meta.file_format.begin(), meta.file_format.end(),
                   meta.file_format.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!meta.file_format.empty() && meta.file_format[0] == '.')
        meta.file_format = meta.file_format.substr(1);

    // Get file size
    struct stat st;
    if (stat(path.c_str(), &st) == 0) meta.file_size = st.st_size;

    // Read tags via TagLib
    TagLib::FileRef f(path.c_str());
    if (f.isNull() || !f.file() || !f.audioProperties()) return meta;

    auto* ap = f.audioProperties();
    meta.duration_ms = ap->lengthInMilliseconds();
    meta.bitrate     = ap->bitrate();
    meta.sample_rate = ap->sampleRate();
    // A supported extension alone is not enough. Resource forks, thumbnails,
    // and damaged files otherwise become zero-length tracks that cannot play.
    if (meta.duration_ms <= 0 || meta.sample_rate <= 0) return meta;
    meta.valid_audio = true;

    if (TagLib::Tag* tag = f.tag()) {
        meta.title        = tag->title().toCString(true);
        meta.artist       = tag->artist().toCString(true);
        meta.album        = tag->album().toCString(true);
        meta.genre        = tag->genre().toCString(true);
        meta.year         = static_cast<int>(tag->year());
        meta.track_number = static_cast<int>(tag->track());
        meta.comment      = tag->comment().toCString(true);
    }

    // Try to extract disc number, album artist, composer from ID3v2 / other frames
    if (auto* mp3 = dynamic_cast<TagLib::MPEG::File*>(f.file())) {
        auto* id3 = mp3->ID3v2Tag();
        if (id3) {
            auto tpos = id3->frameListMap()["TPOS"];
            if (!tpos.isEmpty()) {
                std::string disc = tpos.front()->toString().toCString(true);
                meta.disc_number = std::stoi(disc.substr(0, disc.find('/')));
            }
            auto tpe2 = id3->frameListMap()["TPE2"];
            if (!tpe2.isEmpty()) meta.album_artist = tpe2.front()->toString().toCString(true);
            auto tcom = id3->frameListMap()["TCOM"];
            if (!tcom.isEmpty()) meta.composer = tcom.front()->toString().toCString(true);
            auto tcmp = id3->frameListMap()["TCMP"];
            if (!tcmp.isEmpty()) meta.compilation = (tcmp.front()->toString().toInt() != 0);
        }
    }

    return meta;
}

bool Library::importFile(const std::string& path, Track& out_track) {
    if (!isSupportedFormat(path)) return false;

    std::error_code ec;
    fs::path input(path);
    if (!fs::is_regular_file(input, ec) || ec) return false;
    fs::path canonical = fs::weakly_canonical(input, ec);
    if (ec) return false;
    std::string normalized_path = canonical.string();

    // Returning false here means "nothing new was imported"; callers no
    // longer report an existing track as another successful import.
    if (db_.trackExists(normalized_path)) return false;

    FileMetadata meta = readMetadata(normalized_path);
    if (!meta.valid_audio) {
        std::cerr << "Skipping invalid or unreadable audio file: "
                  << normalized_path << "\n";
        return false;
    }

    Track t;
    t.file_path    = normalized_path;
    t.title        = meta.title.empty() ? canonical.stem().string() : meta.title;
    t.artist       = meta.artist;
    t.album        = meta.album;
    t.genre        = meta.genre;
    t.year         = meta.year;
    t.track_number = meta.track_number;
    t.disc_number  = meta.disc_number;
    t.duration_ms  = meta.duration_ms;
    t.bitrate      = meta.bitrate;
    t.sample_rate  = meta.sample_rate;
    t.file_size    = meta.file_size;
    t.file_format  = meta.file_format;
    t.comment      = meta.comment;
    t.composer     = meta.composer;
    t.album_artist = meta.album_artist;
    t.compilation  = meta.compilation;

    if (!db_.addTrack(t)) {
        std::cerr << "Failed to add track: " << db_.lastError() << "\n";
        return false;
    }
    out_track = t;
    return true;
}

std::vector<std::string> Library::collectAudioFiles(const std::string& dir, bool recursive) {
    std::vector<std::string> files;
    try {
        if (recursive) {
            for (auto& entry : fs::recursive_directory_iterator(dir,
                    fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file() && isSupportedFormat(entry.path().string()))
                    files.push_back(entry.path().string());
            }
        } else {
            for (auto& entry : fs::directory_iterator(dir)) {
                if (entry.is_regular_file() && isSupportedFormat(entry.path().string()))
                    files.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Directory scan error: " << e.what() << "\n";
    }
    std::sort(files.begin(), files.end());
    return files;
}

int Library::importDirectory(const std::string& dir_path, bool recursive, ProgressCallback progress) {
    auto files = collectAudioFiles(dir_path, recursive);
    int imported = 0;
    int total = static_cast<int>(files.size());
    for (int i = 0; i < total; ++i) {
        if (progress) progress(i, total, files[i]);
        Track t;
        if (importFile(files[i], t)) ++imported;
    }
    if (progress) progress(total, total, "");
    return imported;
}

std::vector<Track> Library::getAllTracks()                           const { return db_.getAllTracks(); }
std::vector<Track> Library::searchTracks(const std::string& query)  const { return db_.searchTracks(query); }
std::vector<Track> Library::getTracksByArtist(const std::string& a) const { return db_.getTracksByArtist(a); }
std::vector<Track> Library::getTracksByAlbum(const std::string& a)  const { return db_.getTracksByAlbum(a); }
std::vector<Track> Library::getTracksByGenre(const std::string& g)  const { return db_.getTracksByGenre(g); }
std::vector<std::string> Library::getAllArtists()                    const { return db_.getAllArtists(); }
std::vector<std::string> Library::getAllAlbums()                     const { return db_.getAllAlbums(); }
std::vector<std::string> Library::getAllGenres()                     const { return db_.getAllGenres(); }

bool Library::removeTrack(int64_t id, bool delete_file) {
    if (delete_file) {
        Track t = db_.getTrack(id);
        if (!t.file_path.empty()) {
            std::error_code ec;
            fs::remove(t.file_path, ec);
        }
    }
    return db_.removeTrack(id);
}

bool Library::createPlaylist(const std::string& name, int64_t& id) { return db_.createPlaylist(name, id); }
bool Library::deletePlaylist(int64_t id)                            { return db_.deletePlaylist(id); }
bool Library::addToPlaylist(int64_t pl, int64_t tr, int pos)        { return db_.addTrackToPlaylist(pl, tr, pos); }
bool Library::removeFromPlaylist(int64_t pl, int64_t tr)            { return db_.removeTrackFromPlaylist(pl, tr); }
std::vector<Track> Library::getPlaylistTracks(int64_t id)           const { return db_.getPlaylistTracks(id); }
std::vector<std::pair<int64_t,std::string>> Library::getAllPlaylists() const { return db_.getAllPlaylists(); }

#include "CDManager.h"
#ifdef WINTUNES_NO_CDIO
#include <filesystem>
#include <algorithm>
#else
#include <cdio/cdio.h>
#include <cdio/cd_types.h>
// Paranoia headers differ by distro; try both known locations
#if __has_include(<cdio/paranoia/paranoia.h>)
#  include <cdio/paranoia/paranoia.h>
#  include <cdio/paranoia/cdda.h>
#elif __has_include(<cdio-paranoia/paranoia.h>)
#  include <cdio-paranoia/paranoia.h>
#  include <cdio-paranoia/cdda.h>
#else
#  include <cdio/paranoia.h>
#  include <cdio/cdda.h>
#endif
#include <cddb/cddb.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

CDManager::CDManager(const std::string& device) : device_(device) {}
CDManager::~CDManager() {}

#ifdef WINTUNES_NO_CDIO
std::vector<std::string> CDManager::detectDrives() { return {}; }
bool CDManager::hasAudioCD() const { return false; }
CDInfo CDManager::readTOC() const {
    last_error_ = "CD support is unavailable: libcdio was not found.";
    return {};
}
CDInfo CDManager::fetchMetadata() const { return readTOC(); }
bool CDManager::ripTrack(int, const std::string&, const std::string&, int,
                         CDProgressCallback) {
    last_error_ = "CD ripping is unavailable: libcdio was not found.";
    return false;
}
int CDManager::ripAllTracks(const std::string&, const std::string&, int,
                            CDProgressCallback, const CDInfo*) {
    last_error_ = "CD ripping is unavailable: libcdio was not found.";
    return 0;
}
bool CDManager::burnCD(const std::vector<std::string>&, int, bool,
                       CDProgressCallback) {
    last_error_ = "CD burning is unavailable on this build.";
    return false;
}
bool CDManager::eject() {
    last_error_ = "CD eject is unavailable on this build.";
    return false;
}
#else
std::vector<std::string> CDManager::detectDrives() {
    std::vector<std::string> drives;

    // Use libcdio to enumerate all CD-ROM devices
    char** cdio_drives = cdio_get_devices_with_cap(nullptr, CDIO_FS_AUDIO, false);
    if (cdio_drives) {
        for (int i = 0; cdio_drives[i]; ++i) {
            drives.emplace_back(cdio_drives[i]);
        }
        cdio_free_device_list(cdio_drives);
    }

    // Also check common device paths (including Apple SuperDrive on USB)
    const std::vector<std::string> common_devs = {
        "/dev/cdrom", "/dev/cdrom0", "/dev/cdrom1",
        "/dev/sr0",   "/dev/sr1",    "/dev/sr2",
        "/dev/dvd",   "/dev/dvdrw"
    };
    for (const auto& dev : common_devs) {
        if (fs::exists(dev)) {
            if (std::find(drives.begin(), drives.end(), dev) == drives.end())
                drives.push_back(dev);
        }
    }

    // Apple SuperDrive is presented as a standard USB CD drive on Linux
    // and will appear as /dev/sr* — already covered above
    return drives;
}

bool CDManager::hasAudioCD() const {
    CdIo_t* cdio = cdio_open(device_.c_str(), DRIVER_UNKNOWN);
    if (!cdio) return false;
    discmode_t mode = cdio_get_discmode(cdio);
    cdio_destroy(cdio);
    return (mode == CDIO_DISC_MODE_CD_DA || mode == CDIO_DISC_MODE_CD_MIXED);
}

CDInfo CDManager::readTOC() const {
    CDInfo info;
    CdIo_t* cdio = cdio_open(device_.c_str(), DRIVER_UNKNOWN);
    if (!cdio) {
        last_error_ = "Cannot open device: " + device_;
        return info;
    }

    track_t first = cdio_get_first_track_num(cdio);
    track_t last  = cdio_get_last_track_num(cdio);
    if (first == CDIO_INVALID_TRACK || last == CDIO_INVALID_TRACK) {
        cdio_destroy(cdio);
        last_error_ = "No tracks found on disc";
        return info;
    }

    for (track_t t = first; t <= last; ++t) {
        if (cdio_get_track_format(cdio, t) != TRACK_FORMAT_AUDIO) continue;
        CDTrackInfo ti;
        ti.track_number = t;
        lsn_t lsn_start = cdio_get_track_lsn(cdio, t);
        lsn_t lsn_end   = cdio_get_track_last_lsn(cdio, t);
        ti.duration_secs = static_cast<int>((lsn_end - lsn_start + 1) / 75);
        ti.title  = "Track " + std::to_string(t);
        info.tracks.push_back(ti);
    }

    cdio_destroy(cdio);
    return info;
}

CDInfo CDManager::fetchMetadata() const {
    CDInfo info = readTOC();

    // Build CDDB query using libcdio + libcddb
    CdIo_t* cdio = cdio_open(device_.c_str(), DRIVER_UNKNOWN);
    if (!cdio) return info;

    cddb_conn_t* conn = cddb_new();
    cddb_disc_t* disc = cddb_disc_new();

    if (!conn || !disc) {
        cdio_destroy(cdio);
        if (conn) cddb_destroy(conn);
        if (disc) cddb_disc_destroy(disc);
        return info;
    }

    cddb_set_server_name(conn, "gnudb.gnudb.org");
    cddb_set_server_port(conn, 8880);
    cddb_set_http_path_query(conn, "/~cddb/cddb.cgi");
    cddb_set_http_path_submit(conn, "/~cddb/submit.cgi");
    cddb_set_email_address(conn, "user@example.com");

    // Populate disc offsets
    track_t first = cdio_get_first_track_num(cdio);
    track_t last  = cdio_get_last_track_num(cdio);

    for (track_t t = first; t <= last; ++t) {
        if (cdio_get_track_format(cdio, t) != TRACK_FORMAT_AUDIO) continue;
        cddb_track_t* ct = cddb_track_new();
        lsn_t lsn = cdio_get_track_lsn(cdio, t);
        cddb_track_set_frame_offset(ct, lsn + 150);  // 150 = CD lead-in offset
        cddb_disc_add_track(disc, ct);
    }

    // Calculate disc length
    lsn_t leadout = cdio_get_track_lsn(cdio, CDIO_CDROM_LEADOUT_TRACK);
    cddb_disc_set_length(disc, (leadout + 150) / 75);

    cddb_disc_calc_discid(disc);

    // Format disc ID as hex
    char disc_id_str[16];
    snprintf(disc_id_str, sizeof(disc_id_str), "%08x", cddb_disc_get_discid(disc));
    info.disc_id = disc_id_str;

    // Query CDDB
    int matches = cddb_query(conn, disc);
    if (matches > 0) {
        cddb_read(conn, disc);
        const char* title  = cddb_disc_get_title(disc);
        const char* artist = cddb_disc_get_artist(disc);
        const char* genre  = cddb_disc_get_genre(disc);
        int year           = cddb_disc_get_year(disc);

        if (title)  info.album_title = title;
        if (artist) info.artist      = artist;
        if (genre)  info.genre       = genre;
        info.year = year;
        info.metadata_ok = true;

        // Fill per-track metadata
        for (int i = 0; i < static_cast<int>(info.tracks.size()); ++i) {
            cddb_track_t* ct = cddb_disc_get_track(disc, i);
            if (!ct) continue;
            const char* ttitle  = cddb_track_get_title(ct);
            const char* tartist = cddb_track_get_artist(ct);
            if (ttitle)  info.tracks[i].title  = ttitle;
            if (tartist) info.tracks[i].artist = tartist;
            else         info.tracks[i].artist = info.artist;
            info.tracks[i].album  = info.album_title;
            info.tracks[i].genre  = info.genre;
            info.tracks[i].year   = info.year;
        }
    }

    cddb_disc_destroy(disc);
    cddb_destroy(conn);
    cdio_destroy(cdio);
    return info;
}

std::string CDManager::buildOutputFilename(const CDInfo& info, int track_num,
                                            const std::string& dir,
                                            const std::string& format) const {
    std::string artist = info.artist.empty() ? "Unknown Artist" : info.artist;
    std::string album  = info.album_title.empty() ? "Unknown Album" : info.album_title;
    std::string title  = "Track " + std::to_string(track_num);

    for (const auto& t : info.tracks) {
        if (t.track_number == track_num) {
            if (!t.title.empty()) title = t.title;
            break;
        }
    }

    // Sanitise names for filesystem
    auto sanitise = [](std::string s) {
        for (char& c : s) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"') c = '_';
        return s;
    };

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d", track_num);
    std::string filename = std::string(buf) + " - " + sanitise(title) + "." + format;
    return (fs::path(dir) / sanitise(artist) / sanitise(album) / filename).string();
}

bool CDManager::encodeWav(const std::string& wav_path,
                           const std::string& out_path,
                           const std::string& format,
                           int quality) const {
    // Create output directory
    fs::create_directories(fs::path(out_path).parent_path());

    std::string cmd;
    if (format == "mp3") {
        // lame: quality 0-9 (0=best), we invert so user 0=worst 10=best
        int lame_q = std::max(0, std::min(9, 9 - quality));
        cmd = "lame -q " + std::to_string(lame_q) + " \"" + wav_path + "\" \"" + out_path + "\" 2>/dev/null";
    } else if (format == "flac") {
        cmd = "flac --compression-level-" + std::to_string(quality) +
              " -o \"" + out_path + "\" \"" + wav_path + "\" 2>/dev/null";
    } else if (format == "ogg") {
        cmd = "oggenc -q " + std::to_string(quality) +
              " -o \"" + out_path + "\" \"" + wav_path + "\" 2>/dev/null";
    } else if (format == "aac" || format == "m4a") {
        // Use ffmpeg for AAC encoding
        cmd = "ffmpeg -i \"" + wav_path + "\" -c:a aac -b:a 256k \"" + out_path + "\" 2>/dev/null";
    } else {
        // WAV — just copy
        cmd = "cp \"" + wav_path + "\" \"" + out_path + "\"";
    }
    return system(cmd.c_str()) == 0;
}

bool CDManager::ripTrack(int track_num, const std::string& output_path,
                          const std::string& format, int quality,
                          CDProgressCallback progress) {
    // Use cdio-paranoia for high-quality ripping
    // cdda_identify takes char*** for messages; pass nullptr to discard them
    cdrom_drive_t* drive = cdda_identify(device_.c_str(), CDDA_MESSAGE_FORGETIT, nullptr);
    if (!drive) {
        last_error_ = "Cannot open CD drive for ripping: " + device_;
        return false;
    }

    if (cdda_open(drive) != 0) {
        last_error_ = "Cannot open CD audio on drive: " + device_;
        cdda_close(drive);
        return false;
    }

    cdrom_paranoia_t* paranoia = paranoia_init(drive);
    paranoia_modeset(paranoia, PARANOIA_MODE_FULL ^ PARANOIA_MODE_NEVERSKIP);

    lsn_t start = cdda_track_firstsector(drive, track_num);
    lsn_t end   = cdda_track_lastsector (drive, track_num);
    int   total  = end - start + 1;

    paranoia_seek(paranoia, start, SEEK_SET);

    // Write to temporary WAV file first
    std::string tmp_wav = "/tmp/wintunes_rip_" + std::to_string(track_num) + ".wav";
    FILE* wav = fopen(tmp_wav.c_str(), "wb");
    if (!wav) {
        last_error_ = "Cannot create temporary WAV file";
        paranoia_free(paranoia);
        cdda_close(drive);
        return false;
    }

    // Write WAV header (will be fixed up after ripping)
    uint32_t data_size   = 0;
    uint32_t sample_rate = 44100;
    uint16_t channels    = 2;
    uint16_t bits        = 16;
    uint32_t byte_rate   = sample_rate * channels * (bits / 8);
    uint16_t block_align = channels * (bits / 8);

    auto write_le32 = [&](uint32_t v) {
        uint8_t b[4] = { uint8_t(v), uint8_t(v>>8), uint8_t(v>>16), uint8_t(v>>24) };
        fwrite(b, 1, 4, wav);
    };
    auto write_le16 = [&](uint16_t v) {
        uint8_t b[2] = { uint8_t(v), uint8_t(v>>8) };
        fwrite(b, 1, 2, wav);
    };

    fwrite("RIFF", 1, 4, wav);
    write_le32(0);                  // placeholder chunk size
    fwrite("WAVE", 1, 4, wav);
    fwrite("fmt ", 1, 4, wav);
    write_le32(16);                 // PCM chunk size
    write_le16(1);                  // PCM format
    write_le16(channels);
    write_le32(sample_rate);
    write_le32(byte_rate);
    write_le16(block_align);
    write_le16(bits);
    fwrite("data", 1, 4, wav);
    write_le32(0);                  // placeholder data size

    for (int sector = 0; sector < total; ++sector) {
        int16_t* samples = paranoia_read(paranoia, nullptr);
        if (!samples) break;
        fwrite(samples, sizeof(int16_t), CD_FRAMEWORDS, wav);
        data_size += CD_FRAMEWORDS * sizeof(int16_t);
        if (progress && sector % 75 == 0)
            progress(track_num, 1, sector, total, "Ripping...");
    }

    // Fix WAV header sizes
    uint32_t riff_size = 36 + data_size;
    fseek(wav, 4, SEEK_SET); fwrite(&riff_size, 4, 1, wav);
    fseek(wav, 40, SEEK_SET); fwrite(&data_size,  4, 1, wav);
    fclose(wav);

    paranoia_free(paranoia);
    cdda_close(drive);

    if (progress) progress(track_num, 1, total, total, "Encoding...");

    bool ok = encodeWav(tmp_wav, output_path, format, quality);
    std::remove(tmp_wav.c_str());

    if (!ok) last_error_ = "Encoding failed for track " + std::to_string(track_num);
    return ok;
}

int CDManager::ripAllTracks(const std::string& output_dir,
                              const std::string& format, int quality,
                              CDProgressCallback progress, const CDInfo* info) {
    CDInfo toc = info ? *info : fetchMetadata();
    int ripped = 0;

    for (const auto& track : toc.tracks) {
        std::string out_path = buildOutputFilename(toc, track.track_number, output_dir, format);
        bool ok = ripTrack(track.track_number, out_path, format, quality,
            [&](int tn, int tt, int s, int ts, const std::string& m) {
                if (progress) progress(track.track_number,
                                       static_cast<int>(toc.tracks.size()), s, ts, m);
            });
        if (ok) ++ripped;
    }
    return ripped;
}

bool CDManager::burnCD(const std::vector<std::string>& audio_files,
                        int write_speed, bool simulate,
                        CDProgressCallback progress) {
    if (audio_files.empty()) {
        last_error_ = "No audio files to burn";
        return false;
    }

    if (progress) progress(0, static_cast<int>(audio_files.size()), 0, 0, "Preparing burn...");

    // Convert all audio files to WAV via ffmpeg, then burn with wodim
    std::vector<std::string> wav_files;
    int i = 0;
    for (const auto& f : audio_files) {
        std::string wav = "/tmp/wintunes_burn_" + std::to_string(i++) + ".wav";
        std::string cmd = "ffmpeg -i \"" + f + "\" -f wav -ar 44100 -ac 2 \"" + wav + "\" 2>/dev/null";
        if (system(cmd.c_str()) != 0) {
            last_error_ = "Failed to convert: " + f;
            for (auto& w : wav_files) std::remove(w.c_str());
            return false;
        }
        wav_files.push_back(wav);
    }

    // Build wodim command
    std::string cmd = "wodim -v dev=" + device_;
    if (write_speed > 0) cmd += " speed=" + std::to_string(write_speed);
    if (simulate)        cmd += " -dummy";
    cmd += " -audio -pad";
    for (const auto& wav : wav_files) cmd += " \"" + wav + "\"";

    if (progress) progress(0, static_cast<int>(audio_files.size()), 0, 0, "Burning disc...");
    bool ok = system(cmd.c_str()) == 0;
    if (!ok) last_error_ = "wodim burn command failed. Make sure wodim is installed.";

    for (auto& wav : wav_files) std::remove(wav.c_str());
    if (progress && ok) progress(static_cast<int>(audio_files.size()),
                                  static_cast<int>(audio_files.size()), 0, 0, "Complete");
    return ok;
}

bool CDManager::eject() {
    std::string cmd = "eject " + device_;
    return system(cmd.c_str()) == 0;
}
#endif

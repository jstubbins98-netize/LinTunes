#pragma once
#include <string>
#include <cstdint>

struct Track {
    int64_t     id           = 0;
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
    std::string file_path;
    std::string file_format;
    std::string comment;
    std::string composer;
    std::string album_artist;
    int         play_count   = 0;
    int         rating       = 0;     // 0-100
    bool        on_ipod      = false;
    std::string ipod_id;              // libgpod track ID string
    bool        compilation  = false;
};

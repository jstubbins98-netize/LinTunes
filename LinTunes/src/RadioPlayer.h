#pragma once

#include <string>

struct libvlc_instance_t;
struct libvlc_media_player_t;

class RadioPlayer {
public:
    RadioPlayer();
    ~RadioPlayer();

    bool init();
    bool play(const std::string& stream_url);
    void stop();
    void setVolume(double volume);

    bool isPlaying() const;
    const std::string& lastError() const { return last_error_; }

private:
    libvlc_instance_t* instance_ = nullptr;
    libvlc_media_player_t* player_ = nullptr;
    std::string last_error_;
};
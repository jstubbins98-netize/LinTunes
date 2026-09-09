#include "RadioPlayer.h"

#include <vlc/vlc.h>
#include <algorithm>

RadioPlayer::RadioPlayer() = default;

RadioPlayer::~RadioPlayer() {
    stop();
    if (player_) libvlc_media_player_release(player_);
    if (instance_) libvlc_release(instance_);
}

bool RadioPlayer::init() {
    if (instance_) return true;

    const char* args[] = {"--no-video", "--quiet"};
    instance_ = libvlc_new(2, args);
    if (!instance_) {
        last_error_ = "Could not initialize libVLC";
        return false;
    }

    player_ = libvlc_media_player_new(instance_);
    if (!player_) {
        last_error_ = "Could not create the libVLC radio player";
        libvlc_release(instance_);
        instance_ = nullptr;
        return false;
    }
    return true;
}

bool RadioPlayer::play(const std::string& stream_url) {
    last_error_.clear();
    if (stream_url.empty()) {
        last_error_ = "Enter an internet radio stream URL";
        return false;
    }
    if (!init()) return false;

    libvlc_media_t* media = libvlc_media_new_location(instance_, stream_url.c_str());
    if (!media) {
        last_error_ = "libVLC could not open that stream URL";
        return false;
    }

    libvlc_media_player_set_media(player_, media);
    libvlc_media_release(media);
    if (libvlc_media_player_play(player_) != 0) {
        last_error_ = "libVLC could not start the radio stream";
        return false;
    }
    return true;
}

void RadioPlayer::stop() {
    if (player_) libvlc_media_player_stop(player_);
}

void RadioPlayer::setVolume(double volume) {
    if (!player_) return;
    const int percent = static_cast<int>(
        std::clamp(volume, 0.0, 1.0) * 100.0);
    libvlc_audio_set_volume(player_, percent);
}

bool RadioPlayer::isPlaying() const {
    return player_ && libvlc_media_player_is_playing(player_);
}
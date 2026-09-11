#include "RadioPlayer.h"

#ifdef MACTUNES_HAVE_VLC
#include <vlc/vlc.h>
#endif
#include <algorithm>

RadioPlayer::RadioPlayer() = default;

RadioPlayer::~RadioPlayer() {
#ifdef MACTUNES_HAVE_VLC
    stop();
    if (player_) libvlc_media_player_release(player_);
    if (instance_) libvlc_release(instance_);
#endif
}

bool RadioPlayer::init() {
#ifndef MACTUNES_HAVE_VLC
    last_error_ = "Internet radio is unavailable: libVLC was not found at build time.";
    return false;
#else
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
#endif
}

bool RadioPlayer::play(const std::string& stream_url) {
#ifndef MACTUNES_HAVE_VLC
    (void)stream_url;
    last_error_ = "Internet radio is unavailable.";
    return false;
#else
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
#endif
}

void RadioPlayer::stop() {
#ifdef MACTUNES_HAVE_VLC
    if (player_) libvlc_media_player_stop(player_);
#endif
}

void RadioPlayer::setVolume(double volume) {
#ifdef MACTUNES_HAVE_VLC
    if (!player_) return;
    const int percent = static_cast<int>(
        std::clamp(volume, 0.0, 1.0) * 100.0);
    libvlc_audio_set_volume(player_, percent);
#else
    (void)volume;
#endif
}

bool RadioPlayer::isPlaying() const {
#ifdef MACTUNES_HAVE_VLC
    return player_ && libvlc_media_player_is_playing(player_);
#else
    return false;
#endif
}
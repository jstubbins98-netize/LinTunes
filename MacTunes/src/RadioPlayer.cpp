#include "RadioPlayer.h"

#ifdef MACTUNES_HAVE_VLC
#include <vlc/vlc.h>
#endif
#ifdef MACTUNES_DYNAMIC_VLC
#include <dlfcn.h>
#include <cstdlib>
#include <filesystem>
#include <vector>

struct libvlc_media_t;

namespace {
void* vlc_library = nullptr;
using NewFn = libvlc_instance_t* (*)(int, const char* const*);
using ReleaseFn = void (*)(libvlc_instance_t*);
using PlayerNewFn = libvlc_media_player_t* (*)(libvlc_instance_t*);
using PlayerReleaseFn = void (*)(libvlc_media_player_t*);
using MediaNewFn = libvlc_media_t* (*)(libvlc_instance_t*, const char*);
using MediaReleaseFn = void (*)(libvlc_media_t*);
using SetMediaFn = void (*)(libvlc_media_player_t*, libvlc_media_t*);
using PlayFn = int (*)(libvlc_media_player_t*);
using StopFn = void (*)(libvlc_media_player_t*);
using VolumeFn = int (*)(libvlc_media_player_t*, int);
using IsPlayingFn = int (*)(libvlc_media_player_t*);

NewFn dynamic_new = nullptr;
ReleaseFn dynamic_release = nullptr;
PlayerNewFn dynamic_player_new = nullptr;
PlayerReleaseFn dynamic_player_release = nullptr;
MediaNewFn dynamic_media_new = nullptr;
MediaReleaseFn dynamic_media_release = nullptr;
SetMediaFn dynamic_set_media = nullptr;
PlayFn dynamic_play = nullptr;
StopFn dynamic_stop = nullptr;
VolumeFn dynamic_volume = nullptr;
IsPlayingFn dynamic_is_playing = nullptr;

template<typename T>
bool loadSymbol(T& target, const char* name) {
    target = reinterpret_cast<T>(dlsym(vlc_library, name));
    return target != nullptr;
}

bool loadVlcApplication(std::string& error) {
    if (vlc_library) return true;

    std::vector<std::filesystem::path> bundles = {
        "/Applications/VLC.app"
    };
    if (const char* home = std::getenv("HOME")) {
        bundles.emplace_back(std::filesystem::path(home) / "Applications" / "VLC.app");
    }

    for (const auto& bundle : bundles) {
        const auto library = bundle / "Contents" / "MacOS" / "lib" / "libvlc.dylib";
        if (!std::filesystem::exists(library)) continue;
        vlc_library = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!vlc_library) continue;
        const auto plugins = bundle / "Contents" / "MacOS" / "plugins";
        if (std::filesystem::exists(plugins)) {
            setenv("VLC_PLUGIN_PATH", plugins.c_str(), 1);
        }
        break;
    }

    if (!vlc_library) {
        vlc_library = dlopen("libvlc.dylib", RTLD_NOW | RTLD_LOCAL);
    }
    if (!vlc_library) {
        error = "Internet radio requires VLC.app in /Applications or ~/Applications.";
        return false;
    }

    const bool complete =
        loadSymbol(dynamic_new, "libvlc_new") &&
        loadSymbol(dynamic_release, "libvlc_release") &&
        loadSymbol(dynamic_player_new, "libvlc_media_player_new") &&
        loadSymbol(dynamic_player_release, "libvlc_media_player_release") &&
        loadSymbol(dynamic_media_new, "libvlc_media_new_location") &&
        loadSymbol(dynamic_media_release, "libvlc_media_release") &&
        loadSymbol(dynamic_set_media, "libvlc_media_player_set_media") &&
        loadSymbol(dynamic_play, "libvlc_media_player_play") &&
        loadSymbol(dynamic_stop, "libvlc_media_player_stop") &&
        loadSymbol(dynamic_volume, "libvlc_audio_set_volume") &&
        loadSymbol(dynamic_is_playing, "libvlc_media_player_is_playing");
    if (!complete) {
        error = "The installed VLC.app does not expose the required libVLC 3 API.";
        dlclose(vlc_library);
        vlc_library = nullptr;
        return false;
    }
    return true;
}
}

#define libvlc_new dynamic_new
#define libvlc_release dynamic_release
#define libvlc_media_player_new dynamic_player_new
#define libvlc_media_player_release dynamic_player_release
#define libvlc_media_new_location dynamic_media_new
#define libvlc_media_release dynamic_media_release
#define libvlc_media_player_set_media dynamic_set_media
#define libvlc_media_player_play dynamic_play
#define libvlc_media_player_stop dynamic_stop
#define libvlc_audio_set_volume dynamic_volume
#define libvlc_media_player_is_playing dynamic_is_playing
#endif
#include <algorithm>

#if defined(MACTUNES_HAVE_VLC) || defined(MACTUNES_DYNAMIC_VLC)
#define MACTUNES_RADIO_ENABLED
#endif

RadioPlayer::RadioPlayer() = default;

RadioPlayer::~RadioPlayer() {
#ifdef MACTUNES_RADIO_ENABLED
    stop();
    if (player_) libvlc_media_player_release(player_);
    if (instance_) libvlc_release(instance_);
#endif
}

bool RadioPlayer::init() {
#ifndef MACTUNES_RADIO_ENABLED
    last_error_ = "Internet radio is unavailable: libVLC was not found at build time.";
    return false;
#else
    if (instance_) return true;

#ifdef MACTUNES_DYNAMIC_VLC
    if (!loadVlcApplication(last_error_)) return false;
#endif

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
#ifndef MACTUNES_RADIO_ENABLED
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
#ifdef MACTUNES_RADIO_ENABLED
    if (player_) libvlc_media_player_stop(player_);
#endif
}

void RadioPlayer::setVolume(double volume) {
#ifdef MACTUNES_RADIO_ENABLED
    if (!player_) return;
    const int percent = static_cast<int>(
        std::clamp(volume, 0.0, 1.0) * 100.0);
    libvlc_audio_set_volume(player_, percent);
#else
    (void)volume;
#endif
}

bool RadioPlayer::isPlaying() const {
#ifdef MACTUNES_RADIO_ENABLED
    return player_ && libvlc_media_player_is_playing(player_);
#else
    return false;
#endif
}
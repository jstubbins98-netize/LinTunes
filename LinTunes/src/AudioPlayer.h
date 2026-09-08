#pragma once
#include "Track.h"
#include <string>
#include <vector>
#include <functional>
#include <gst/gst.h>

enum class PlayerState { Stopped, Playing, Paused };
enum class ShuffleMode  { Off, On };
enum class RepeatMode   { Off, One, All };

using StateChangedCallback    = std::function<void(PlayerState)>;
using PositionChangedCallback = std::function<void(int64_t pos_ms, int64_t duration_ms)>;
using TrackChangedCallback    = std::function<void(const Track&)>;
using TrackEndedCallback      = std::function<void()>;

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool init();          // Call once after GStreamer is initialised
    void shutdown();

    // Queue management
    void setQueue(const std::vector<Track>& tracks, int start_index = 0);
    void appendToQueue(const Track& track);
    void clearQueue();

    const std::vector<Track>& queue()        const { return queue_; }
    int                       currentIndex() const { return current_index_; }
    const Track*              currentTrack() const;

    // Playback control
    bool play();
    bool pause();
    bool stop();
    bool next();
    bool previous();
    bool seekTo(int64_t pos_ms);
    bool openFile(const std::string& path);

    // State
    PlayerState state()       const { return state_; }
    int64_t     position()    const;   // ms
    int64_t     duration()    const;   // ms
    double      volume()      const { return volume_; }
    bool        setVolume(double v);   // 0.0 – 1.0

    // Modes
    ShuffleMode shuffleMode() const { return shuffle_; }
    RepeatMode  repeatMode()  const { return repeat_; }
    void setShuffleMode(ShuffleMode m);
    void setRepeatMode (RepeatMode  m);

    // Callbacks
    void onStateChanged   (StateChangedCallback    cb) { on_state_changed_    = std::move(cb); }
    void onPositionChanged(PositionChangedCallback cb) { on_position_changed_ = std::move(cb); }
    void onTrackChanged   (TrackChangedCallback    cb) { on_track_changed_    = std::move(cb); }
    void onTrackEnded     (TrackEndedCallback      cb) { on_track_ended_      = std::move(cb); }

    // GStreamer bus callback (internal, must be public for C callback linkage)
    void handleBusMessage(GstMessage* msg);

private:
    GstElement*   pipeline_      = nullptr;
    GstElement*   source_        = nullptr;
    PlayerState   state_         = PlayerState::Stopped;
    double        volume_        = 1.0;
    ShuffleMode   shuffle_       = ShuffleMode::Off;
    RepeatMode    repeat_        = RepeatMode::Off;

    std::vector<Track> queue_;
    int                current_index_ = -1;
    std::vector<int>   play_order_;     // shuffle indices

    StateChangedCallback    on_state_changed_;
    PositionChangedCallback on_position_changed_;
    TrackChangedCallback    on_track_changed_;
    TrackEndedCallback      on_track_ended_;

    GSource* timer_source_ = nullptr;

    bool loadTrack(int index);
    void buildPlayOrder();
    static gboolean busWatch(GstBus* bus, GstMessage* msg, gpointer data);
    static gboolean timerTick(gpointer data);
};

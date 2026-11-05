#include "AudioPlayer.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <random>

AudioPlayer::AudioPlayer() {}

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::init() {
    pipeline_ = gst_element_factory_make("playbin", "player");
    if (!pipeline_) {
        std::cerr << "Failed to create GStreamer playbin element\n";
        return false;
    }

    // Set up bus watch
    GstBus* bus = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus, busWatch, this);
    gst_object_unref(bus);

    // Set up position timer (every 500 ms)
    timer_source_ = g_timeout_source_new(500);
    g_source_set_callback(timer_source_, timerTick, this, nullptr);
    g_source_attach(timer_source_, g_main_context_default());

    return true;
}

void AudioPlayer::shutdown() {
    stop();
    if (timer_source_) {
        g_source_destroy(timer_source_);
        g_source_unref(timer_source_);
        timer_source_ = nullptr;
    }
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
}

gboolean AudioPlayer::busWatch(GstBus*, GstMessage* msg, gpointer data) {
    auto* self = static_cast<AudioPlayer*>(data);
    self->handleBusMessage(msg);
    return TRUE;
}

void AudioPlayer::handleBusMessage(GstMessage* msg) {
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        state_ = PlayerState::Stopped;
        if (on_state_changed_) on_state_changed_(state_);
        if (on_track_ended_)   on_track_ended_();
        // Auto-advance
        next();
        break;

    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar*  debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        std::cerr << "GStreamer error: " << (err ? err->message : "unknown") << "\n";
        g_clear_error(&err);
        g_free(debug);
        state_ = PlayerState::Stopped;
        if (on_state_changed_) on_state_changed_(state_);
        break;
    }

    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline_)) {
            GstState old_state, new_state, pending;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
            if (new_state == GST_STATE_PLAYING && state_ != PlayerState::Playing) {
                state_ = PlayerState::Playing;
                if (on_state_changed_) on_state_changed_(state_);
            } else if (new_state == GST_STATE_PAUSED && state_ == PlayerState::Playing) {
                state_ = PlayerState::Paused;
                if (on_state_changed_) on_state_changed_(state_);
            }
        }
        break;

    default:
        break;
    }
}

gboolean AudioPlayer::timerTick(gpointer data) {
    auto* self = static_cast<AudioPlayer*>(data);
    if (self->state_ == PlayerState::Playing && self->on_position_changed_) {
        self->on_position_changed_(self->position(), self->duration());
    }
    return TRUE;
}

void AudioPlayer::setQueue(const std::vector<Track>& tracks, int start_index) {
    queue_         = tracks;
    current_index_ = -1;
    buildPlayOrder();
    if (!tracks.empty()) loadTrack(start_index);
}

void AudioPlayer::appendToQueue(const Track& track) {
    queue_.push_back(track);
    buildPlayOrder();
}

void AudioPlayer::clearQueue() {
    stop();
    queue_.clear();
    current_index_ = -1;
    play_order_.clear();
}

const Track* AudioPlayer::currentTrack() const {
    if (current_index_ < 0 || current_index_ >= (int)queue_.size()) return nullptr;
    return &queue_[current_index_];
}

void AudioPlayer::buildPlayOrder() {
    play_order_.resize(queue_.size());
    std::iota(play_order_.begin(), play_order_.end(), 0);
    if (shuffle_ == ShuffleMode::On) {
        std::mt19937 rng(std::random_device{}());
        std::shuffle(play_order_.begin(), play_order_.end(), rng);
    }
}

bool AudioPlayer::loadTrack(int index) {
    if (index < 0 || index >= (int)queue_.size()) return false;
    stop();
    current_index_ = index;
    const Track& t = queue_[current_index_];
    std::string uri = "file://" + t.file_path;
    g_object_set(pipeline_, "uri", uri.c_str(), nullptr);
    if (on_track_changed_) on_track_changed_(t);
    return true;
}

bool AudioPlayer::openFile(const std::string& path) {
    stop();
    current_index_ = -1;
    queue_.clear();
    play_order_.clear();
    std::string uri = "file://" + path;
    g_object_set(pipeline_, "uri", uri.c_str(), nullptr);
    return play();
}

bool AudioPlayer::play() {
    if (!pipeline_) return false;
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) return false;
    state_ = PlayerState::Playing;
    if (on_state_changed_) on_state_changed_(state_);
    return true;
}

bool AudioPlayer::pause() {
    if (!pipeline_) return false;
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    state_ = PlayerState::Paused;
    if (on_state_changed_) on_state_changed_(state_);
    return true;
}

bool AudioPlayer::stop() {
    if (!pipeline_) return false;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    state_ = PlayerState::Stopped;
    if (on_state_changed_) on_state_changed_(state_);
    return true;
}

bool AudioPlayer::next() {
    if (queue_.empty()) return false;

    // Find position in play_order_
    int pos = -1;
    for (int i = 0; i < (int)play_order_.size(); ++i) {
        if (play_order_[i] == current_index_) { pos = i; break; }
    }

    if (repeat_ == RepeatMode::One) {
        loadTrack(current_index_);
        return play();
    }

    int next_pos = pos + 1;
    if (next_pos >= (int)play_order_.size()) {
        if (repeat_ == RepeatMode::All) next_pos = 0;
        else { stop(); return false; }
    }

    loadTrack(play_order_[next_pos]);
    return play();
}

bool AudioPlayer::previous() {
    if (queue_.empty()) return false;

    // If more than 3 seconds in, just restart current track
    if (position() > 3000) {
        return seekTo(0);
    }

    int pos = -1;
    for (int i = 0; i < (int)play_order_.size(); ++i) {
        if (play_order_[i] == current_index_) { pos = i; break; }
    }

    int prev_pos = pos - 1;
    if (prev_pos < 0) prev_pos = repeat_ == RepeatMode::All ? (int)play_order_.size() - 1 : 0;
    loadTrack(play_order_[prev_pos]);
    return play();
}

bool AudioPlayer::seekTo(int64_t pos_ms) {
    if (!pipeline_) return false;
    return gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        pos_ms * GST_MSECOND);
}

int64_t AudioPlayer::position() const {
    if (!pipeline_) return 0;
    gint64 pos = 0;
    gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pos);
    return pos / GST_MSECOND;
}

int64_t AudioPlayer::duration() const {
    if (!pipeline_) return 0;
    gint64 dur = 0;
    gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &dur);
    return dur / GST_MSECOND;
}

bool AudioPlayer::setVolume(double v) {
    volume_ = std::max(0.0, std::min(1.0, v));
    if (pipeline_) g_object_set(pipeline_, "volume", volume_, nullptr);
    return true;
}

void AudioPlayer::setShuffleMode(ShuffleMode m) {
    shuffle_ = m;
    buildPlayOrder();
}

void AudioPlayer::setRepeatMode(RepeatMode m) {
    repeat_ = m;
}

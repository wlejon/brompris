#include "brompris/mpris.h"

#include <algorithm>

namespace brompris {

MediaPlayer::MediaPlayer()
    : model_("") {}

MediaPlayer::MediaPlayer(std::string id)
    : model_(std::move(id)) {}

MediaPlayer::~MediaPlayer() {
    notify_closed();
}

bool MediaPlayer::play() {
    return controller_ ? controller_->play() : false;
}

bool MediaPlayer::pause() {
    return controller_ ? controller_->pause() : false;
}

bool MediaPlayer::play_pause() {
    return controller_ ? controller_->play_pause() : false;
}

bool MediaPlayer::stop() {
    return controller_ ? controller_->stop() : false;
}

bool MediaPlayer::next() {
    return controller_ ? controller_->next() : false;
}

bool MediaPlayer::previous() {
    return controller_ ? controller_->previous() : false;
}

bool MediaPlayer::seek(int64_t offset_usec) {
    return controller_ ? controller_->seek(offset_usec) : false;
}

bool MediaPlayer::set_position(const std::string& track_id, int64_t position_usec) {
    return controller_ ? controller_->set_position(track_id, position_usec) : false;
}

bool MediaPlayer::set_volume(double volume) {
    return controller_ ? controller_->set_volume(volume) : false;
}

bool MediaPlayer::set_loop_status(LoopStatus status) {
    return controller_ ? controller_->set_loop_status(status) : false;
}

bool MediaPlayer::set_shuffle_status(ShuffleStatus status) {
    return controller_ ? controller_->set_shuffle_status(status) : false;
}

bool MediaPlayer::set_rate(double rate) {
    return controller_ ? controller_->set_rate(rate) : false;
}

bool MediaPlayer::open_uri(const std::string& uri) {
    return controller_ ? controller_->open_uri(uri) : false;
}

bool MediaPlayer::raise() {
    return controller_ ? controller_->raise() : false;
}

bool MediaPlayer::quit() {
    return controller_ ? controller_->quit() : false;
}

void MediaPlayer::add_observer(PlayerObserver* observer) {
    if (!observer) return;
    std::lock_guard<std::mutex> lock(observers_mutex_);
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void MediaPlayer::remove_observer(PlayerObserver* observer) {
    if (!observer) return;
    std::lock_guard<std::mutex> lock(observers_mutex_);
    auto it = std::remove(observers_.begin(), observers_.end(), observer);
    observers_.erase(it, observers_.end());
}

void MediaPlayer::notify_playback_status_changed(PlaybackStatus status) {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_playback_status_changed(*this, status);
    }
}

void MediaPlayer::notify_metadata_changed(const MediaMetadata& meta) {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_metadata_changed(*this, meta);
    }
}

void MediaPlayer::notify_position_seeked(int64_t position_usec) {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_position_seeked(*this, position_usec);
    }
}

void MediaPlayer::notify_volume_changed(double volume) {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_volume_changed(*this, volume);
    }
}

void MediaPlayer::notify_rate_changed(double rate) {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_rate_changed(*this, rate);
    }
}

void MediaPlayer::notify_loop_status_changed(LoopStatus loop) {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_loop_status_changed(*this, loop);
    }
}

void MediaPlayer::notify_shuffle_status_changed(ShuffleStatus shuffle) {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_shuffle_status_changed(*this, shuffle);
    }
}

void MediaPlayer::notify_capabilities_changed(const PlayerCapabilities& caps) {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_capabilities_changed(*this, caps);
    }
}

void MediaPlayer::notify_closed() {
    std::vector<PlayerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_player_closed(*this);
    }
}

}  // namespace brompris

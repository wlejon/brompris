#include "brompris/player_model.h"

#include <algorithm>

namespace brompris {

PlayerModel::PlayerModel(std::string id)
    : id_(std::move(id)) {}

int64_t PlayerModel::estimated_position_usec() const noexcept {
    if (!has_position_) {
        return 0;
    }

    if (status_ != PlaybackStatus::Playing || rate_ == 0.0) {
        int64_t pos = base_position_usec_;
        if (pos < 0) pos = 0;
        if (metadata_.duration_usec > 0 && pos > metadata_.duration_usec) {
            pos = metadata_.duration_usec;
        }
        return pos;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - last_position_update_).count();

    int64_t pos = base_position_usec_ + static_cast<int64_t>(elapsed * rate_);
    if (pos < 0) {
        pos = 0;
    }
    if (metadata_.duration_usec > 0 && pos > metadata_.duration_usec) {
        pos = metadata_.duration_usec;
    }
    return pos;
}

double PlayerModel::estimated_position_seconds() const noexcept {
    return static_cast<double>(estimated_position_usec()) / 1'000'000.0;
}

void PlayerModel::set_playback_status(PlaybackStatus status) {
    if (status_ == status) {
        return;
    }

    if (status_ == PlaybackStatus::Playing) {
        // Leaving Playing: freeze the elapsed position into base_position_usec_
        base_position_usec_ = estimated_position_usec();
        last_position_update_ = std::chrono::steady_clock::now();
    } else if (status == PlaybackStatus::Playing) {
        // Entering Playing: restart timer reference point
        last_position_update_ = std::chrono::steady_clock::now();
    }

    status_ = status;
}

void PlayerModel::set_position(int64_t position_usec) {
    base_position_usec_ = position_usec;
    last_position_update_ = std::chrono::steady_clock::now();
    has_position_ = true;
}

void PlayerModel::set_volume(double vol) {
    volume_ = std::max(0.0, vol);
}

void PlayerModel::set_rate(double rate) {
    if (status_ == PlaybackStatus::Playing) {
        base_position_usec_ = estimated_position_usec();
        last_position_update_ = std::chrono::steady_clock::now();
    }
    rate_ = rate;
}

void PlayerModel::set_metadata(MediaMetadata metadata) {
    if (metadata_.track_id != metadata.track_id) {
        base_position_usec_ = 0;
        last_position_update_ = std::chrono::steady_clock::now();
    }
    metadata_ = std::move(metadata);
}

void PlayerModel::reset() {
    status_ = PlaybackStatus::Stopped;
    loop_ = LoopStatus::None;
    shuffle_ = ShuffleStatus::Off;
    volume_ = 1.0;
    rate_ = 1.0;
    metadata_ = MediaMetadata{};
    capabilities_ = PlayerCapabilities{};
    base_position_usec_ = 0;
    last_position_update_ = std::chrono::steady_clock::time_point{};
    has_position_ = false;
}

}  // namespace brompris

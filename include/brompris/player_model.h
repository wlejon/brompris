#pragma once

#include "brompris/metadata.h"
#include "brompris/observer.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace brompris {

class PlayerModel {
public:
    PlayerModel() = default;
    explicit PlayerModel(std::string id);

    // Getters
    const std::string& id() const noexcept { return id_; }
    const std::string& identity() const noexcept { return identity_; }
    const std::string& desktop_entry() const noexcept { return desktop_entry_; }
    PlaybackStatus playback_status() const noexcept { return status_; }
    LoopStatus loop_status() const noexcept { return loop_; }
    ShuffleStatus shuffle_status() const noexcept { return shuffle_; }
    double volume() const noexcept { return volume_; }
    double rate() const noexcept { return rate_; }
    const MediaMetadata& metadata() const noexcept { return metadata_; }
    const PlayerCapabilities& capabilities() const noexcept { return capabilities_; }
    bool has_position() const noexcept { return has_position_; }
    int64_t base_position_usec() const noexcept { return base_position_usec_; }

    // Position ticker calculations
    int64_t estimated_position_usec() const noexcept;
    double estimated_position_seconds() const noexcept;

    bool is_playing() const noexcept { return status_ == PlaybackStatus::Playing; }
    bool is_paused() const noexcept { return status_ == PlaybackStatus::Paused; }
    bool is_stopped() const noexcept { return status_ == PlaybackStatus::Stopped; }

    // State mutations
    void set_id(std::string id) { id_ = std::move(id); }
    void set_identity(std::string identity) { identity_ = std::move(identity); }
    void set_desktop_entry(std::string desktop_entry) { desktop_entry_ = std::move(desktop_entry); }
    void set_playback_status(PlaybackStatus status);
    void set_loop_status(LoopStatus loop) { loop_ = loop; }
    void set_shuffle_status(ShuffleStatus shuffle) { shuffle_ = shuffle; }
    void set_volume(double vol);
    void set_rate(double rate);
    void set_capabilities(const PlayerCapabilities& caps) { capabilities_ = caps; }
    void set_position(int64_t position_usec);
    void set_metadata(MediaMetadata metadata);

    void reset();

private:
    std::string id_;
    std::string identity_;
    std::string desktop_entry_;
    PlaybackStatus status_ = PlaybackStatus::Stopped;
    LoopStatus loop_ = LoopStatus::None;
    ShuffleStatus shuffle_ = ShuffleStatus::Off;
    double volume_ = 1.0;
    double rate_ = 1.0;
    MediaMetadata metadata_;
    PlayerCapabilities capabilities_;

    // Position tracking ticker
    int64_t base_position_usec_ = 0;
    std::chrono::steady_clock::time_point last_position_update_{};
    bool has_position_ = false;
};

}  // namespace brompris

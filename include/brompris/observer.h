#pragma once

#include "brompris/metadata.h"

#include <memory>
#include <string>

namespace brompris {

class MediaPlayer;

struct PlayerCapabilities {
    bool can_control = false;
    bool can_play = false;
    bool can_pause = false;
    bool can_seek = false;
    bool can_go_next = false;
    bool can_go_previous = false;
    bool can_quit = false;
    bool can_raise = false;
    bool can_set_fullscreen = false;

    bool operator==(const PlayerCapabilities& o) const = default;
};

class PlayerObserver {
public:
    virtual ~PlayerObserver() = default;

    virtual void on_playback_status_changed(MediaPlayer& player, PlaybackStatus status) {
        (void)player; (void)status;
    }
    virtual void on_metadata_changed(MediaPlayer& player, const MediaMetadata& metadata) {
        (void)player; (void)metadata;
    }
    virtual void on_position_seeked(MediaPlayer& player, int64_t position_usec) {
        (void)player; (void)position_usec;
    }
    virtual void on_volume_changed(MediaPlayer& player, double volume) {
        (void)player; (void)volume;
    }
    virtual void on_rate_changed(MediaPlayer& player, double rate) {
        (void)player; (void)rate;
    }
    virtual void on_loop_status_changed(MediaPlayer& player, LoopStatus loop) {
        (void)player; (void)loop;
    }
    virtual void on_shuffle_status_changed(MediaPlayer& player, ShuffleStatus shuffle) {
        (void)player; (void)shuffle;
    }
    virtual void on_capabilities_changed(MediaPlayer& player, const PlayerCapabilities& caps) {
        (void)player; (void)caps;
    }
    virtual void on_player_closed(MediaPlayer& player) {
        (void)player;
    }
};

class ManagerObserver {
public:
    virtual ~ManagerObserver() = default;

    virtual void on_player_added(std::shared_ptr<MediaPlayer> player) {
        (void)player;
    }
    virtual void on_player_removed(const std::string& player_id) {
        (void)player_id;
    }
    virtual void on_active_player_changed(std::shared_ptr<MediaPlayer> player) {
        (void)player;
    }
};

}  // namespace brompris

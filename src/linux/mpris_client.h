#pragma once

#include "brompris/mpris.h"

#if defined(BROMPRIS_HAS_BRODBUS)
#include "brodbus/bus.h"
#include "brodbus/message.h"
#include "brodbus/slot.h"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace brompris {

class MprisPlaybackController : public PlaybackController {
public:
    MprisPlaybackController(std::shared_ptr<brodbus::Bus> bus, std::string destination);
    ~MprisPlaybackController() override = default;

    bool play() override;
    bool pause() override;
    bool play_pause() override;
    bool stop() override;
    bool next() override;
    bool previous() override;
    bool seek(int64_t offset_usec) override;
    bool set_position(const std::string& track_id, int64_t position_usec) override;
    bool set_volume(double volume) override;
    bool set_loop_status(LoopStatus status) override;
    bool set_shuffle_status(ShuffleStatus status) override;
    bool set_rate(double rate) override;
    bool open_uri(const std::string& uri) override;
    bool raise() override;
    bool quit() override;

private:
    bool call_player_method(const std::string& member);
    bool set_player_property(const std::string& prop, const brodbus::PropertyValue& val);

    std::shared_ptr<brodbus::Bus> bus_;
    std::string destination_;
};

class MprisMediaPlayer : public MediaPlayer {
public:
    MprisMediaPlayer(std::shared_ptr<brodbus::Bus> bus,
                     std::string well_known_name,
                     std::string unique_name);
    ~MprisMediaPlayer() override = default;

    const std::string& well_known_name() const noexcept { return well_known_name_; }
    const std::string& unique_name() const noexcept { return unique_name_; }
    void set_unique_name(std::string un) { unique_name_ = std::move(un); }

    std::chrono::steady_clock::time_point last_activity_time() const noexcept {
        return last_activity_time_;
    }
    void record_activity() {
        last_activity_time_ = std::chrono::steady_clock::now();
    }

    void refresh_properties();
    void apply_property(const std::string& name, const brodbus::PropertyValue& val);
    void apply_metadata(const MediaMetadata& meta);
    void handle_seeked(int64_t position_usec);

private:
    void query_root_properties();
    void query_player_properties();

    std::shared_ptr<brodbus::Bus> bus_;
    std::string well_known_name_;
    std::string unique_name_;
    std::chrono::steady_clock::time_point last_activity_time_;
};

class LinuxMediaManager : public MediaManager {
public:
    explicit LinuxMediaManager(std::shared_ptr<brodbus::Bus> bus);
    ~LinuxMediaManager() override;

    std::vector<std::shared_ptr<MediaPlayer>> active_players() const override;
    std::shared_ptr<MediaPlayer> active_player() const override;
    std::shared_ptr<MediaPlayer> find_player(const std::string& id) const override;

    int process() override;
    int wait(uint64_t timeout_usec = 100000) override;
    void update() override;

    void discover_players();
    void add_or_update_player(const std::string& well_known_name, const std::string& unique_name = "");
    void remove_player(const std::string& well_known_name);

private:
    void setup_matches();
    void handle_name_owner_changed(brodbus::Message& msg);
    void handle_properties_changed(brodbus::Message& msg);
    void handle_seeked(brodbus::Message& msg);
    std::shared_ptr<MprisMediaPlayer> find_player_internal(const std::string& sender_or_name) const;
    void update_active_player_if_needed();

    std::shared_ptr<brodbus::Bus> bus_;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<MprisMediaPlayer>> players_by_name_;
    std::map<std::string, std::shared_ptr<MprisMediaPlayer>> players_by_unique_;
    std::shared_ptr<MediaPlayer> current_active_player_;
    std::vector<brodbus::Slot> slots_;
};

}  // namespace brompris

#endif  // BROMPRIS_HAS_BRODBUS

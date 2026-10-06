#pragma once

#include "brompris/controller.h"
#include "brompris/metadata.h"
#include "brompris/observer.h"
#include "brompris/player_model.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(BROMPRIS_HAS_BRODBUS)
namespace brodbus {
class Bus;
}
#endif

namespace brompris {

class MediaPlayer : public std::enable_shared_from_this<MediaPlayer> {
public:
    MediaPlayer();
    explicit MediaPlayer(std::string id);
    virtual ~MediaPlayer();

    // State queries
    const std::string& id() const noexcept { return model_.id(); }
    const std::string& identity() const noexcept { return model_.identity(); }
    const std::string& desktop_entry() const noexcept { return model_.desktop_entry(); }
    PlaybackStatus playback_status() const noexcept { return model_.playback_status(); }
    LoopStatus loop_status() const noexcept { return model_.loop_status(); }
    ShuffleStatus shuffle_status() const noexcept { return model_.shuffle_status(); }
    double volume() const noexcept { return model_.volume(); }
    double rate() const noexcept { return model_.rate(); }
    const MediaMetadata& metadata() const noexcept { return model_.metadata(); }
    const PlayerCapabilities& capabilities() const noexcept { return model_.capabilities(); }
    int64_t position_usec() const noexcept { return model_.estimated_position_usec(); }
    double position_seconds() const noexcept { return model_.estimated_position_seconds(); }

    const PlayerModel& model() const noexcept { return model_; }
    PlayerModel& model() noexcept { return model_; }

    std::shared_ptr<PlaybackController> controller() const noexcept { return controller_; }
    void set_controller(std::shared_ptr<PlaybackController> ctrl) { controller_ = std::move(ctrl); }

    // Direct playback commands forwarded to controller
    bool play();
    bool pause();
    bool play_pause();
    bool stop();
    bool next();
    bool previous();
    bool seek(int64_t offset_usec);
    bool set_position(const std::string& track_id, int64_t position_usec);
    bool set_volume(double volume);
    bool set_loop_status(LoopStatus status);
    bool set_shuffle_status(ShuffleStatus status);
    bool set_rate(double rate);
    bool open_uri(const std::string& uri);
    bool raise();
    bool quit();

    // Observers
    void add_observer(PlayerObserver* observer);
    void remove_observer(PlayerObserver* observer);

    // Event notifications
    void notify_playback_status_changed(PlaybackStatus status);
    void notify_metadata_changed(const MediaMetadata& meta);
    void notify_position_seeked(int64_t position_usec);
    void notify_volume_changed(double volume);
    void notify_rate_changed(double rate);
    void notify_loop_status_changed(LoopStatus loop);
    void notify_shuffle_status_changed(ShuffleStatus shuffle);
    void notify_capabilities_changed(const PlayerCapabilities& caps);
    void notify_closed();

private:
    PlayerModel model_;
    std::shared_ptr<PlaybackController> controller_;
    mutable std::mutex observers_mutex_;
    std::vector<PlayerObserver*> observers_;
};

class MediaManager {
public:
    virtual ~MediaManager() = default;

    virtual std::vector<std::shared_ptr<MediaPlayer>> active_players() const = 0;
    virtual std::shared_ptr<MediaPlayer> active_player() const = 0;
    virtual std::shared_ptr<MediaPlayer> find_player(const std::string& id) const = 0;

    virtual int process() = 0;
    virtual int wait(uint64_t timeout_usec = 100000) = 0;
    virtual void update() = 0;

    void add_observer(ManagerObserver* observer);
    void remove_observer(ManagerObserver* observer);

    // Factory methods
    static std::unique_ptr<MediaManager> create();

#if defined(BROMPRIS_HAS_BRODBUS)
    static std::unique_ptr<MediaManager> create_with_bus(std::shared_ptr<brodbus::Bus> bus);
    static std::unique_ptr<MediaManager> create_with_address(const std::string& dbus_address);
#endif

protected:
    void notify_player_added(std::shared_ptr<MediaPlayer> player);
    void notify_player_removed(const std::string& player_id);
    void notify_active_player_changed(std::shared_ptr<MediaPlayer> player);

private:
    mutable std::mutex observers_mutex_;
    std::vector<ManagerObserver*> observers_;
};

}  // namespace brompris

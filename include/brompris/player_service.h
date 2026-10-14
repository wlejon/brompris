#pragma once

#include "brompris/metadata.h"
#include "brompris/observer.h"
#include "brompris/player_model.h"

#include <cstdint>
#include <functional>
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

struct PlayerServiceConfig {
    std::string name = "player";
    std::string identity = "Media Player";
    std::string desktop_entry;

    std::vector<std::string> supported_uri_schemes = {"file"};
    std::vector<std::string> supported_mime_types = {
        "audio/mpeg", "audio/flac", "audio/ogg", "audio/wav",
        "audio/mp4", "audio/aac", "audio/opus"
    };

    PlayerCapabilities capabilities{
        .can_control = true,
        .can_play = true,
        .can_pause = true,
        .can_seek = true,
        .can_go_next = true,
        .can_go_previous = true,
        .can_quit = false,
        .can_raise = false,
        .can_set_fullscreen = false,
    };

    PlaybackStatus playback_status = PlaybackStatus::Stopped;
    LoopStatus loop_status = LoopStatus::None;
    ShuffleStatus shuffle_status = ShuffleStatus::Off;
    double volume = 1.0;
    double rate = 1.0;
    double minimum_rate = 1.0;
    double maximum_rate = 1.0;
    int64_t position_usec = 0;
    MediaMetadata metadata;
};

using MediaServiceConfig = PlayerServiceConfig;

struct PlayerServiceCallbacks {
    std::function<void()> on_play;
    std::function<void()> on_pause;
    std::function<void()> on_play_pause;
    std::function<void()> on_stop;
    std::function<void()> on_next;
    std::function<void()> on_previous;
    std::function<void(int64_t offset_usec)> on_seek;
    std::function<void(const std::string& track_id, int64_t position_usec)> on_set_position;
    std::function<void(const std::string& uri)> on_open_uri;
    std::function<void()> on_raise;
    std::function<void()> on_quit;
    std::function<void(LoopStatus status)> on_set_loop_status;
    std::function<void(bool shuffle)> on_set_shuffle;
    std::function<void(ShuffleStatus status)> on_set_shuffle_status;
    std::function<void(double volume)> on_set_volume;
    std::function<void(double rate)> on_set_rate;
    std::function<void(bool fullscreen)> on_set_fullscreen;
};

using MediaServiceCallbacks = PlayerServiceCallbacks;

class PlayerServiceObserver {
public:
    virtual ~PlayerServiceObserver() = default;

    virtual void on_play() {}
    virtual void on_pause() {}
    virtual void on_play_pause() {}
    virtual void on_stop() {}
    virtual void on_next() {}
    virtual void on_previous() {}
    virtual void on_seek(int64_t offset_usec) { (void)offset_usec; }
    virtual void on_set_position(const std::string& track_id, int64_t position_usec) {
        (void)track_id; (void)position_usec;
    }
    virtual void on_open_uri(const std::string& uri) { (void)uri; }
    virtual void on_raise() {}
    virtual void on_quit() {}
    virtual void on_set_loop_status(LoopStatus status) { (void)status; }
    virtual void on_set_shuffle(bool shuffle) { (void)shuffle; }
    virtual void on_set_volume(double volume) { (void)volume; }
    virtual void on_set_rate(double rate) { (void)rate; }
    virtual void on_set_fullscreen(bool fullscreen) { (void)fullscreen; }
};

using MediaServiceObserver = PlayerServiceObserver;

class PlayerService {
public:
    explicit PlayerService(const PlayerServiceConfig& config = {},
                           PlayerServiceCallbacks callbacks = {});
    virtual ~PlayerService();

    // Identity & bus queries
    std::string id() const;
    std::string bus_name() const { return id(); }
    std::string name() const;
    std::string identity() const;
    std::string desktop_entry() const;

    // State queries
    PlaybackStatus playback_status() const;
    LoopStatus loop_status() const;
    ShuffleStatus shuffle_status() const;
    bool shuffle() const { return shuffle_status_to_bool(shuffle_status()); }
    double volume() const;
    double rate() const;
    double minimum_rate() const;
    double maximum_rate() const;
    bool fullscreen() const;
    MediaMetadata metadata() const;
    PlayerCapabilities capabilities() const;
    int64_t position_usec() const;
    double position_seconds() const;
    std::vector<std::string> supported_uri_schemes() const;
    std::vector<std::string> supported_mime_types() const;

    virtual bool is_running() const noexcept { return running_; }
    virtual bool is_connected() const noexcept { return false; }

    // State & property updates (thread-safe; emits PropertiesChanged on IPC backends)
    virtual void set_identity(std::string identity);
    virtual void set_desktop_entry(std::string desktop_entry);
    virtual void set_playback_status(PlaybackStatus status);
    virtual void set_loop_status(LoopStatus status);
    virtual void set_shuffle_status(ShuffleStatus status);
    void set_shuffle(bool shuffle) { set_shuffle_status(shuffle_status_from_bool(shuffle)); }
    virtual void set_volume(double volume);
    virtual void set_rate(double rate);
    virtual void set_rate_range(double min_rate, double max_rate);
    virtual void set_fullscreen(bool fullscreen);
    virtual void set_capabilities(const PlayerCapabilities& caps);
    virtual void set_position(int64_t position_usec);
    virtual void set_metadata(MediaMetadata metadata);
    virtual void set_supported_uri_schemes(std::vector<std::string> schemes);
    virtual void set_supported_mime_types(std::vector<std::string> mime_types);

    // Emits Seeked(position_usec) signal and updates base position
    virtual void emit_seeked(int64_t position_usec);
    void seeked(int64_t position_usec) { emit_seeked(position_usec); }

    // Command callback registration
    void set_callbacks(PlayerServiceCallbacks callbacks);
    void on_play(std::function<void()> cb);
    void on_pause(std::function<void()> cb);
    void on_play_pause(std::function<void()> cb);
    void on_stop(std::function<void()> cb);
    void on_next(std::function<void()> cb);
    void on_previous(std::function<void()> cb);
    void on_seek(std::function<void(int64_t)> cb);
    void on_set_position(std::function<void(const std::string&, int64_t)> cb);
    void on_open_uri(std::function<void(const std::string&)> cb);
    void on_raise(std::function<void()> cb);
    void on_quit(std::function<void()> cb);
    void on_set_loop_status(std::function<void(LoopStatus)> cb);
    void on_set_shuffle(std::function<void(bool)> cb);
    void on_set_shuffle_status(std::function<void(ShuffleStatus)> cb);
    void on_set_volume(std::function<void(double)> cb);
    void on_set_rate(std::function<void(double)> cb);
    void on_set_fullscreen(std::function<void(bool)> cb);

    void add_observer(PlayerServiceObserver* observer);
    void remove_observer(PlayerServiceObserver* observer);

    // Lifecycle & event processing
    virtual bool start();
    virtual void stop();
    void close() { stop(); }

    virtual int process() { return 0; }
    virtual int wait(uint64_t timeout_usec = 100000) { (void)timeout_usec; return 0; }
    virtual void update() { process(); }

    // Factory methods
    static std::unique_ptr<PlayerService> create(const PlayerServiceConfig& config = {},
                                                 PlayerServiceCallbacks callbacks = {});
    static std::unique_ptr<PlayerService> create(const std::string& name,
                                                 const std::string& identity = "Media Player",
                                                 const std::string& desktop_entry = "");

#if defined(BROMPRIS_HAS_BRODBUS)
    static std::unique_ptr<PlayerService> create_with_bus(
        std::shared_ptr<brodbus::Bus> bus,
        const PlayerServiceConfig& config = {},
        PlayerServiceCallbacks callbacks = {});
    static std::unique_ptr<PlayerService> create_with_address(
        const std::string& dbus_address,
        const PlayerServiceConfig& config = {},
        PlayerServiceCallbacks callbacks = {});
#endif

    // Helpers to format/normalize MPRIS2 bus name ("org.mpris.MediaPlayer2.<suffix>")
    static std::string normalize_bus_name(const std::string& name);

protected:
    void set_bus_name_internal(std::string bus_name);
    std::string normalize_incoming_track_id(const std::string& incoming_track_id) const;

    void notify_play();
    void notify_pause();
    void notify_play_pause();
    void notify_stop();
    void notify_next();
    void notify_previous();
    void notify_seek(int64_t offset_usec);
    void notify_set_position(const std::string& track_id, int64_t position_usec);
    void notify_open_uri(const std::string& uri);
    void notify_raise();
    void notify_quit();
    void notify_set_loop_status(LoopStatus status);
    void notify_set_shuffle(bool shuffle);
    void notify_set_volume(double volume);
    void notify_set_rate(double rate);
    void notify_set_fullscreen(bool fullscreen);

    mutable std::mutex state_mutex_;
    PlayerModel model_;
    std::string name_;
    std::vector<std::string> supported_uri_schemes_;
    std::vector<std::string> supported_mime_types_;
    double minimum_rate_ = 1.0;
    double maximum_rate_ = 1.0;
    bool fullscreen_ = false;
    bool running_ = false;

private:
    mutable std::mutex callbacks_mutex_;
    PlayerServiceCallbacks callbacks_;
    std::vector<PlayerServiceObserver*> observers_;
};

using MediaService = PlayerService;

}  // namespace brompris

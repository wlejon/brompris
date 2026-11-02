#include "brompris/mpris.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>
#endif

namespace brompris {

#if defined(__APPLE__)

namespace {

NSString* to_nsstring(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

std::string run_osascript(const std::string& script, int* exit_code = nullptr) {
    std::string escaped;
    escaped.reserve(script.size() + 16);
    for (char c : script) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(c);
        }
    }
    std::string cmd = "/usr/bin/osascript -e '" + escaped + "' 2>/dev/null";
    std::string out;
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        if (exit_code) *exit_code = -1;
        return out;
    }
    char buf[2048];
    while (size_t n = std::fread(buf, 1, sizeof(buf), pipe)) {
        out.append(buf, n);
    }
    int status = ::pclose(pipe);
    if (exit_code) *exit_code = status;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
}

std::string escape_applescript_string(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> split_tab_fields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        std::size_t pos = line.find('\t', start);
        if (pos == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

}  // namespace

// ============================================================================
// MacPlayerService — MPNowPlayingInfoCenter & MPRemoteCommandCenter
// ============================================================================

class MacPlayerService final : public PlayerService {
public:
    MacPlayerService(const PlayerServiceConfig& config, PlayerServiceCallbacks callbacks)
        : PlayerService(config, std::move(callbacks)) {
        start();
    }

    ~MacPlayerService() override {
        stop();
    }

    bool is_connected() const noexcept override {
        return running_ && connected_.load();
    }

    bool start() override {
        PlayerService::start();
        if (!connected_.exchange(true)) {
            register_remote_commands();
        }
        publish_now_playing();
        return true;
    }

    void stop() override {
        if (connected_.exchange(false)) {
            unregister_remote_commands();
            @autoreleasepool {
                MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
                center.nowPlayingInfo = nil;
                if (@available(macOS 10.12.2, *)) {
                    center.playbackState = MPNowPlayingPlaybackStateStopped;
                }
            }
        }
        PlayerService::stop();
        cv_.notify_all();
    }

    int process() override {
        return pending_events_.exchange(0);
    }

    int wait(uint64_t timeout_usec = 100000) override {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        bool ready = cv_.wait_for(
            lock, std::chrono::microseconds(timeout_usec),
            [this] { return pending_events_.load() > 0 || !running_; });
        return ready ? pending_events_.exchange(0) : 0;
    }

    void update() override {
        publish_now_playing();
    }

    void set_playback_status(PlaybackStatus status) override {
        PlayerService::set_playback_status(status);
        publish_now_playing();
    }

    void set_loop_status(LoopStatus status) override {
        PlayerService::set_loop_status(status);
        sync_command_states();
    }

    void set_shuffle_status(ShuffleStatus status) override {
        PlayerService::set_shuffle_status(status);
        sync_command_states();
    }

    void set_volume(double volume) override {
        PlayerService::set_volume(volume);
    }

    void set_rate(double rate) override {
        PlayerService::set_rate(rate);
        publish_now_playing();
    }

    void set_rate_range(double min_rate, double max_rate) override {
        PlayerService::set_rate_range(min_rate, max_rate);
        sync_command_states();
    }

    void set_capabilities(const PlayerCapabilities& caps) override {
        PlayerService::set_capabilities(caps);
        sync_command_states();
    }

    void set_position(int64_t position_usec) override {
        PlayerService::set_position(position_usec);
        publish_now_playing();
    }

    void set_metadata(MediaMetadata metadata) override {
        PlayerService::set_metadata(std::move(metadata));
        publish_now_playing();
    }

    void emit_seeked(int64_t position_usec) override {
        PlayerService::emit_seeked(position_usec);
        publish_now_playing();
    }

private:
    void signal_event() {
        pending_events_.fetch_add(1);
        cv_.notify_all();
    }

    void publish_now_playing() {
        if (!connected_.load()) return;

        const MediaMetadata meta = metadata();
        const PlaybackStatus status = playback_status();
        const double cur_rate = rate();
        const double pos_sec = position_seconds();
        const double dur_sec = meta.duration_seconds();

        @autoreleasepool {
            MPNowPlayingInfoCenter* center = [MPNowPlayingInfoCenter defaultCenter];
            NSMutableDictionary* info = [NSMutableDictionary dictionary];

            if (!meta.title.empty()) {
                info[MPMediaItemPropertyTitle] = to_nsstring(meta.title);
            }
            if (!meta.artist.empty()) {
                info[MPMediaItemPropertyArtist] = to_nsstring(meta.artist);
            }
            if (!meta.album.empty()) {
                info[MPMediaItemPropertyAlbumTitle] = to_nsstring(meta.album);
            }
            if (!meta.album_artist.empty()) {
                info[MPMediaItemPropertyAlbumArtist] = to_nsstring(meta.album_artist);
            }
            if (dur_sec > 0.0) {
                info[MPMediaItemPropertyPlaybackDuration] = @(dur_sec);
            }
            if (meta.track_number > 0) {
                info[MPMediaItemPropertyAlbumTrackNumber] = @(meta.track_number);
            }
            if (meta.disc_number > 0) {
                info[MPMediaItemPropertyDiscNumber] = @(meta.disc_number);
            }
            if (!meta.track_id.empty()) {
                info[MPNowPlayingInfoPropertyExternalContentIdentifier] = to_nsstring(meta.track_id);
            }

            info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(std::max(0.0, pos_sec));
            info[MPNowPlayingInfoPropertyDefaultPlaybackRate] = @(cur_rate > 0.0 ? cur_rate : 1.0);
            info[MPNowPlayingInfoPropertyPlaybackRate] =
                @(status == PlaybackStatus::Playing ? cur_rate : 0.0);

            center.nowPlayingInfo = info;

            if (@available(macOS 10.12.2, *)) {
                switch (status) {
                    case PlaybackStatus::Playing:
                        center.playbackState = MPNowPlayingPlaybackStatePlaying;
                        break;
                    case PlaybackStatus::Paused:
                        center.playbackState = MPNowPlayingPlaybackStatePaused;
                        break;
                    case PlaybackStatus::Stopped:
                    default:
                        center.playbackState = MPNowPlayingPlaybackStateStopped;
                        break;
                }
            }
        }
        sync_command_states();
    }

    void sync_command_states() {
        if (!connected_.load()) return;
        const PlayerCapabilities caps = capabilities();
        const LoopStatus loop = loop_status();
        const ShuffleStatus shuf = shuffle_status();

        @autoreleasepool {
            MPRemoteCommandCenter* cc = [MPRemoteCommandCenter sharedCommandCenter];
            cc.playCommand.enabled = caps.can_control && caps.can_play;
            cc.pauseCommand.enabled = caps.can_control && caps.can_pause;
            cc.togglePlayPauseCommand.enabled =
                caps.can_control && (caps.can_play || caps.can_pause);
            cc.stopCommand.enabled = caps.can_control;
            cc.nextTrackCommand.enabled = caps.can_control && caps.can_go_next;
            cc.previousTrackCommand.enabled = caps.can_control && caps.can_go_previous;
            cc.changePlaybackPositionCommand.enabled = caps.can_control && caps.can_seek;
            cc.changeRepeatModeCommand.enabled = caps.can_control;
            cc.changeShuffleModeCommand.enabled = caps.can_control;

            switch (loop) {
                case LoopStatus::Track:
                    cc.changeRepeatModeCommand.currentRepeatType = MPRepeatTypeOne;
                    break;
                case LoopStatus::Playlist:
                    cc.changeRepeatModeCommand.currentRepeatType = MPRepeatTypeAll;
                    break;
                case LoopStatus::None:
                default:
                    cc.changeRepeatModeCommand.currentRepeatType = MPRepeatTypeOff;
                    break;
            }
            cc.changeShuffleModeCommand.currentShuffleType =
                (shuf == ShuffleStatus::On) ? MPShuffleTypeItems : MPShuffleTypeOff;
        }
    }

    void register_remote_commands() {
        @autoreleasepool {
            MPRemoteCommandCenter* cc = [MPRemoteCommandCenter sharedCommandCenter];

            play_target_ = [cc.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
                this->notify_play();
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];

            pause_target_ = [cc.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
                this->notify_pause();
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];

            toggle_target_ = [cc.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
                this->notify_play_pause();
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];

            stop_target_ = [cc.stopCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
                this->notify_stop();
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];

            next_target_ = [cc.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
                this->notify_next();
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];

            prev_target_ = [cc.previousTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
                this->notify_previous();
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];

            pos_target_ = [cc.changePlaybackPositionCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
                auto* pos_ev = static_cast<MPChangePlaybackPositionCommandEvent*>(event);
                int64_t pos_usec = static_cast<int64_t>(std::llround(pos_ev.positionTime * 1'000'000.0));
                std::string tid = this->normalize_incoming_track_id("");
                this->set_position(pos_usec);
                this->notify_set_position(tid, pos_usec);
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];

            repeat_target_ = [cc.changeRepeatModeCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
                auto* rep_ev = static_cast<MPChangeRepeatModeCommandEvent*>(event);
                LoopStatus next_loop = LoopStatus::None;
                if (rep_ev.repeatType == MPRepeatTypeOne) next_loop = LoopStatus::Track;
                else if (rep_ev.repeatType == MPRepeatTypeAll) next_loop = LoopStatus::Playlist;
                this->set_loop_status(next_loop);
                this->notify_set_loop_status(next_loop);
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];

            shuffle_target_ = [cc.changeShuffleModeCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
                auto* shuf_ev = static_cast<MPChangeShuffleModeCommandEvent*>(event);
                bool on = (shuf_ev.shuffleType != MPShuffleTypeOff);
                this->set_shuffle(on);
                this->notify_set_shuffle(on);
                this->signal_event();
                return MPRemoteCommandHandlerStatusSuccess;
            }];
        }
        sync_command_states();
    }

    void unregister_remote_commands() {
        @autoreleasepool {
            MPRemoteCommandCenter* cc = [MPRemoteCommandCenter sharedCommandCenter];
            if (play_target_) [cc.playCommand removeTarget:play_target_];
            if (pause_target_) [cc.pauseCommand removeTarget:pause_target_];
            if (toggle_target_) [cc.togglePlayPauseCommand removeTarget:toggle_target_];
            if (stop_target_) [cc.stopCommand removeTarget:stop_target_];
            if (next_target_) [cc.nextTrackCommand removeTarget:next_target_];
            if (prev_target_) [cc.previousTrackCommand removeTarget:prev_target_];
            if (pos_target_) [cc.changePlaybackPositionCommand removeTarget:pos_target_];
            if (repeat_target_) [cc.changeRepeatModeCommand removeTarget:repeat_target_];
            if (shuffle_target_) [cc.changeShuffleModeCommand removeTarget:shuffle_target_];

            play_target_ = nil;
            pause_target_ = nil;
            toggle_target_ = nil;
            stop_target_ = nil;
            next_target_ = nil;
            prev_target_ = nil;
            pos_target_ = nil;
            repeat_target_ = nil;
            shuffle_target_ = nil;
        }
    }

    std::atomic<bool> connected_{false};
    std::atomic<int> pending_events_{0};
    std::mutex cv_mutex_;
    std::condition_variable cv_;

    id play_target_ = nil;
    id pause_target_ = nil;
    id toggle_target_ = nil;
    id stop_target_ = nil;
    id next_target_ = nil;
    id prev_target_ = nil;
    id pos_target_ = nil;
    id repeat_target_ = nil;
    id shuffle_target_ = nil;
};

// ============================================================================
// MacPlaybackController & MacMediaManager — AppleScript / NSWorkspace bridge
// ============================================================================

class MacPlaybackController final : public PlaybackController {
public:
    explicit MacPlaybackController(std::string app_name)
        : app_name_(std::move(app_name)) {}

    ~MacPlaybackController() override = default;

    bool play() override {
        return tell("play");
    }

    bool pause() override {
        return tell("pause");
    }

    bool play_pause() override {
        return tell("playpause");
    }

    bool stop() override {
        return tell("stop");
    }

    bool next() override {
        return tell("next track");
    }

    bool previous() override {
        return tell("previous track");
    }

    bool seek(int64_t offset_usec) override {
        double delta_sec = static_cast<double>(offset_usec) / 1'000'000.0;
        std::ostringstream oss;
        oss << "set player position to ((player position) + (" << delta_sec << "))";
        return tell(oss.str());
    }

    bool set_position(const std::string& track_id, int64_t position_usec) override {
        (void)track_id;
        double sec = std::max(0.0, static_cast<double>(position_usec) / 1'000'000.0);
        std::ostringstream oss;
        oss << "set player position to " << sec;
        return tell(oss.str());
    }

    bool set_volume(double volume) override {
        int vol_pct = static_cast<int>(std::llround(std::clamp(volume, 0.0, 1.0) * 100.0));
        return tell("set sound volume to " + std::to_string(vol_pct));
    }

    bool set_loop_status(LoopStatus status) override {
        if (app_name_ == "Music") {
            const char* mode = "off";
            if (status == LoopStatus::Track) mode = "one";
            else if (status == LoopStatus::Playlist) mode = "all";
            return tell(std::string("set song repeat to ") + mode);
        }
        bool rep = (status != LoopStatus::None);
        return tell(std::string("set repeating to ") + (rep ? "true" : "false"));
    }

    bool set_shuffle_status(ShuffleStatus status) override {
        bool on = (status == ShuffleStatus::On);
        if (app_name_ == "Music") {
            return tell(std::string("set shuffle enabled to ") + (on ? "true" : "false"));
        }
        return tell(std::string("set shuffling to ") + (on ? "true" : "false"));
    }

    bool set_rate(double rate) override {
        (void)rate;
        return false;
    }

    bool open_uri(const std::string& uri) override {
        return tell("open location \"" + escape_applescript_string(uri) + "\"");
    }

    bool raise() override {
        return tell("activate");
    }

    bool quit() override {
        return tell("quit");
    }

private:
    bool tell(const std::string& cmd) const {
        if (app_name_.empty()) return false;
        int rc = -1;
        run_osascript("tell application \"" + app_name_ + "\" to " + cmd, &rc);
        return rc == 0;
    }

    std::string app_name_;
};

class MacMediaManager final : public MediaManager {
public:
    MacMediaManager() {
        refresh_players(false);
    }

    ~MacMediaManager() override = default;

    std::vector<std::shared_ptr<MediaPlayer>> active_players() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return players_;
    }

    std::shared_ptr<MediaPlayer> active_player() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_player_) return active_player_;
        return players_.empty() ? nullptr : players_.front();
    }

    std::shared_ptr<MediaPlayer> find_player(const std::string& id) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& p : players_) {
            if (p && p->id() == id) return p;
        }
        return nullptr;
    }

    int process() override {
        refresh_players(true);
        return 1;
    }

    int wait(uint64_t timeout_usec = 100000) override {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait_for(lock, std::chrono::microseconds(std::min<uint64_t>(timeout_usec, 50000)));
        refresh_players(true);
        return 1;
    }

    void update() override {
        refresh_players(true);
    }

private:
    struct AppSpec {
        const char* bundle_id;
        const char* app_name;
        const char* mpris_id;
    };

    static bool is_app_running(const char* bundle_id) {
        @autoreleasepool {
            NSString* bid = [NSString stringWithUTF8String:bundle_id];
            NSArray<NSRunningApplication*>* apps =
                [NSRunningApplication runningApplicationsWithBundleIdentifier:bid];
            return apps != nil && apps.count > 0;
        }
    }

    static bool query_app_player(const AppSpec& spec, MediaPlayer& player, bool notify) {
        std::string script =
            "tell application \"" + std::string(spec.app_name) + "\"\n"
            "  set st to player state as string\n"
            "  set vol to sound volume as integer\n"
            "  set pos to 0\n"
            "  try\n"
            "    set pos to player position as real\n"
            "  end try\n"
            "  set tname to \"\"\n"
            "  set tartist to \"\"\n"
            "  set talbum to \"\"\n"
            "  set tdur to 0\n"
            "  try\n"
            "    set tname to name of current track as string\n"
            "    set tartist to artist of current track as string\n"
            "    set talbum to album of current track as string\n"
            "    set tdur to duration of current track as real\n"
            "  end try\n"
            "  return st & \"\t\" & (vol as string) & \"\t\" & (pos as string) & \"\t\" & (tdur as string) & \"\t\" & tname & \"\t\" & tartist & \"\t\" & talbum\n"
            "end tell";

        int rc = -1;
        std::string out = run_osascript(script, &rc);
        if (rc != 0 || out.empty()) return false;

        auto fields = split_tab_fields(out);
        if (fields.size() < 7) return false;

        const auto old_status = player.playback_status();
        const auto old_vol = player.volume();
        const auto old_meta = player.metadata();

        PlaybackStatus status = PlaybackStatus::Stopped;
        if (fields[0].find("playing") != std::string::npos) status = PlaybackStatus::Playing;
        else if (fields[0].find("paused") != std::string::npos) status = PlaybackStatus::Paused;
        player.model().set_playback_status(status);

        double vol = std::clamp(std::atof(fields[1].c_str()) / 100.0, 0.0, 1.0);
        player.model().set_volume(vol);

        double pos_sec = std::max(0.0, std::atof(fields[2].c_str()));
        player.model().set_position(static_cast<int64_t>(std::llround(pos_sec * 1'000'000.0)));

        double dur_val = std::max(0.0, std::atof(fields[3].c_str()));
        // Spotify reports duration in milliseconds, Music reports seconds
        int64_t dur_usec = ( std::string(spec.app_name) == "Spotify" && dur_val > 1000.0 )
            ? static_cast<int64_t>(std::llround(dur_val * 1'000.0))
            : static_cast<int64_t>(std::llround(dur_val * 1'000'000.0));

        MediaMetadata meta;
        meta.title = fields[4];
        meta.artist = fields[5];
        if (!meta.artist.empty()) meta.artists.push_back(meta.artist);
        meta.album = fields[6];
        meta.duration_usec = dur_usec;
        if (!meta.title.empty()) {
            meta.track_id = std::string("/org/mpris/MediaPlayer2/") + spec.app_name;
        }
        player.model().set_metadata(std::move(meta));

        PlayerCapabilities caps;
        caps.can_control = true;
        caps.can_play = true;
        caps.can_pause = true;
        caps.can_seek = true;
        caps.can_go_next = true;
        caps.can_go_previous = true;
        caps.can_raise = true;
        caps.can_quit = true;
        player.model().set_capabilities(caps);

        if (notify) {
            if (player.playback_status() != old_status) {
                player.notify_playback_status_changed(player.playback_status());
            }
            if (std::abs(player.volume() - old_vol) > 1e-4) {
                player.notify_volume_changed(player.volume());
            }
            if (!(player.metadata() == old_meta)) {
                player.notify_metadata_changed(player.metadata());
            }
        }
        return true;
    }

    void refresh_players(bool notify) {
        static constexpr AppSpec kSpecs[] = {
            {"com.apple.Music", "Music", "org.mpris.MediaPlayer2.Music"},
            {"com.spotify.client", "Spotify", "org.mpris.MediaPlayer2.Spotify"},
        };

        std::vector<std::shared_ptr<MediaPlayer>> added;
        std::vector<std::string> removed;
        std::shared_ptr<MediaPlayer> new_active;
        bool active_changed = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<std::shared_ptr<MediaPlayer>> next_players;

            for (const auto& spec : kSpecs) {
                if (!is_app_running(spec.bundle_id)) continue;

                std::shared_ptr<MediaPlayer> existing;
                for (const auto& p : players_) {
                    if (p && p->id() == spec.mpris_id) {
                        existing = p;
                        break;
                    }
                }

                if (existing) {
                    query_app_player(spec, *existing, notify);
                    next_players.push_back(existing);
                } else {
                    auto player = std::make_shared<MediaPlayer>(spec.mpris_id);
                    player->model().set_identity(spec.app_name);
                    player->model().set_desktop_entry(spec.bundle_id);
                    player->set_controller(std::make_shared<MacPlaybackController>(spec.app_name));
                    if (query_app_player(spec, *player, false)) {
                        added.push_back(player);
                        next_players.push_back(player);
                    }
                }
            }

            for (const auto& old_p : players_) {
                if (!old_p) continue;
                bool kept = false;
                for (const auto& np : next_players) {
                    if (np->id() == old_p->id()) {
                        kept = true;
                        break;
                    }
                }
                if (!kept) {
                    removed.push_back(old_p->id());
                }
            }

            players_ = std::move(next_players);

            for (const auto& p : players_) {
                if (p->playback_status() == PlaybackStatus::Playing) {
                    new_active = p;
                    break;
                }
            }
            if (!new_active && !players_.empty()) {
                new_active = players_.front();
            }
            if (new_active != active_player_) {
                active_player_ = new_active;
                active_changed = true;
            }
        }

        if (!notify) return;

        for (const auto& id : removed) {
            notify_player_removed(id);
        }
        for (const auto& p : added) {
            notify_player_added(p);
        }
        if (active_changed) {
            notify_active_player_changed(new_active);
        }
    }

    mutable std::mutex mutex_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<MediaPlayer>> players_;
    std::shared_ptr<MediaPlayer> active_player_;
};

std::unique_ptr<MediaManager> create_mac_media_manager() {
    return std::make_unique<MacMediaManager>();
}

std::unique_ptr<PlayerService> create_mac_player_service(
    const PlayerServiceConfig& config, PlayerServiceCallbacks callbacks) {
    return std::make_unique<MacPlayerService>(config, std::move(callbacks));
}

#else

std::unique_ptr<MediaManager> create_mac_media_manager() {
    return nullptr;
}

std::unique_ptr<PlayerService> create_mac_player_service(
    const PlayerServiceConfig& config, PlayerServiceCallbacks callbacks) {
    return std::make_unique<PlayerService>(config, std::move(callbacks));
}

#endif  // __APPLE__

}  // namespace brompris

#include "brompris/mpris.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#endif  // _WIN32

namespace brompris {

#if defined(_WIN32)

namespace {

using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

void ensure_com_initialized() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr;
}

// Wait on a WinRT IAsyncOperation without triggering C++/WinRT's STA blocking_suspend assert.
template <typename AsyncOp>
auto wait_async(AsyncOp const& op, uint32_t timeout_ms = 3000) -> decltype(op.GetResults()) {
    if (op.Status() == AsyncStatus::Completed) {
        return op.GetResults();
    }
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    op.Completed([done](auto const&, AsyncStatus) {
        try {
            done->set_value();
        } catch (...) {
        }
    });
    if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::ready) {
        return op.GetResults();
    }
    throw winrt::hresult_error(E_ABORT);
}

bool wait_async_bool(winrt::Windows::Foundation::IAsyncOperation<bool> const& op) {
    try {
        return wait_async(op);
    } catch (...) {
        return false;
    }
}

std::string friendly_identity_from_aumid(const std::string& aumid) {
    if (aumid.empty()) return "Windows Media Player";
    std::string s = aumid;
    auto slash = s.find_last_of("\\/");
    if (slash != std::string::npos) {
        s = s.substr(slash + 1);
    }
    auto bang = s.find('!');
    if (bang != std::string::npos) {
        s = s.substr(0, bang);
    }
    if (s.size() > 4) {
        std::string tail = s.substr(s.size() - 4);
        for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tail == ".exe") {
            s.resize(s.size() - 4);
        }
    }
    return s.empty() ? aumid : s;
}

PlaybackStatus map_playback_status(GlobalSystemMediaTransportControlsSessionPlaybackStatus st) {
    switch (st) {
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
            return PlaybackStatus::Playing;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
            return PlaybackStatus::Paused;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Opened:
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing:
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
        default:
            return PlaybackStatus::Stopped;
    }
}

LoopStatus map_loop_status(MediaPlaybackAutoRepeatMode mode) {
    switch (mode) {
        case MediaPlaybackAutoRepeatMode::Track:
            return LoopStatus::Track;
        case MediaPlaybackAutoRepeatMode::List:
            return LoopStatus::Playlist;
        case MediaPlaybackAutoRepeatMode::None:
        default:
            return LoopStatus::None;
    }
}

MediaPlaybackAutoRepeatMode to_winrt_repeat(LoopStatus status) {
    switch (status) {
        case LoopStatus::Track:
            return MediaPlaybackAutoRepeatMode::Track;
        case LoopStatus::Playlist:
            return MediaPlaybackAutoRepeatMode::List;
        case LoopStatus::None:
        default:
            return MediaPlaybackAutoRepeatMode::None;
    }
}

}  // namespace

class WindowsPlaybackController : public PlaybackController {
public:
    explicit WindowsPlaybackController(GlobalSystemMediaTransportControlsSession session)
        : session_(std::move(session)) {}
    ~WindowsPlaybackController() override = default;

    bool play() override {
        if (!session_) return false;
        try {
            return wait_async_bool(session_.TryPlayAsync());
        } catch (...) {
            return false;
        }
    }

    bool pause() override {
        if (!session_) return false;
        try {
            return wait_async_bool(session_.TryPauseAsync());
        } catch (...) {
            return false;
        }
    }

    bool play_pause() override {
        if (!session_) return false;
        try {
            return wait_async_bool(session_.TryTogglePlayPauseAsync());
        } catch (...) {
            return false;
        }
    }

    bool stop() override {
        if (!session_) return false;
        try {
            return wait_async_bool(session_.TryStopAsync());
        } catch (...) {
            return false;
        }
    }

    bool next() override {
        if (!session_) return false;
        try {
            return wait_async_bool(session_.TrySkipNextAsync());
        } catch (...) {
            return false;
        }
    }

    bool previous() override {
        if (!session_) return false;
        try {
            return wait_async_bool(session_.TrySkipPreviousAsync());
        } catch (...) {
            return false;
        }
    }

    bool seek(int64_t offset_usec) override {
        if (!session_) return false;
        try {
            auto tl = session_.GetTimelineProperties();
            int64_t cur_usec =
                std::chrono::duration_cast<std::chrono::microseconds>(tl.Position()).count();
            int64_t target_usec = std::max<int64_t>(0, cur_usec + offset_usec);
            // TryChangePlaybackPositionAsync expects 100ns ticks (1 us = 10 ticks).
            return wait_async_bool(session_.TryChangePlaybackPositionAsync(target_usec * 10));
        } catch (...) {
            return false;
        }
    }

    bool set_position(const std::string& track_id, int64_t position_usec) override {
        (void)track_id;
        if (!session_) return false;
        try {
            int64_t clamped_usec = std::max<int64_t>(0, position_usec);
            return wait_async_bool(session_.TryChangePlaybackPositionAsync(clamped_usec * 10));
        } catch (...) {
            return false;
        }
    }

    bool set_volume(double volume) override {
        (void)volume;
        return false;
    }

    bool set_loop_status(LoopStatus status) override {
        if (!session_) return false;
        try {
            return wait_async_bool(session_.TryChangeAutoRepeatModeAsync(to_winrt_repeat(status)));
        } catch (...) {
            return false;
        }
    }

    bool set_shuffle_status(ShuffleStatus status) override {
        if (!session_) return false;
        try {
            return wait_async_bool(
                session_.TryChangeShuffleActiveAsync(status == ShuffleStatus::On));
        } catch (...) {
            return false;
        }
    }

    bool set_rate(double rate) override {
        if (!session_) return false;
        try {
            return wait_async_bool(session_.TryChangePlaybackRateAsync(rate));
        } catch (...) {
            return false;
        }
    }

    bool open_uri(const std::string& uri) override {
        (void)uri;
        return false;
    }

    bool raise() override { return false; }
    bool quit() override { return false; }

private:
    GlobalSystemMediaTransportControlsSession session_{nullptr};
};

class WindowsMediaManager : public MediaManager {
public:
    WindowsMediaManager() {
        ensure_com_initialized();
        try {
            manager_ = wait_async(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            if (manager_) {
                sessions_token_ = manager_.SessionsChanged(
                    [this](auto const&, auto const&) { mark_dirty(); });
                current_token_ = manager_.CurrentSessionChanged(
                    [this](auto const&, auto const&) { mark_dirty(); });
                refresh_sessions(false);
            }
        } catch (...) {
            manager_ = nullptr;
        }
    }

    ~WindowsMediaManager() override {
        std::lock_guard<std::mutex> lock(mutex_);
        unhook_sessions_locked();
        if (manager_) {
            try {
                if (sessions_token_.value != 0) manager_.SessionsChanged(sessions_token_);
                if (current_token_.value != 0) manager_.CurrentSessionChanged(current_token_);
            } catch (...) {
            }
            manager_ = nullptr;
        }
    }

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
        if (!dirty_.exchange(false)) return 0;
        refresh_sessions(true);
        return 1;
    }

    int wait(uint64_t timeout_usec = 100000) override {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        bool ready = cv_.wait_for(
            lock, std::chrono::microseconds(timeout_usec), [this] { return dirty_.load(); });
        return ready ? 1 : 0;
    }

    void update() override {
        dirty_.store(false);
        refresh_sessions(true);
    }

private:
    struct SessionEntry {
        std::string id;
        GlobalSystemMediaTransportControlsSession session{nullptr};
        std::shared_ptr<MediaPlayer> player;
        winrt::event_token media_token{};
        winrt::event_token playback_token{};
        winrt::event_token timeline_token{};
    };

    void mark_dirty() {
        dirty_.store(true);
        cv_.notify_all();
    }

    void unhook_entry(SessionEntry& e) {
        if (!e.session) return;
        try {
            if (e.media_token.value != 0) e.session.MediaPropertiesChanged(e.media_token);
            if (e.playback_token.value != 0) e.session.PlaybackInfoChanged(e.playback_token);
            if (e.timeline_token.value != 0) e.session.TimelinePropertiesChanged(e.timeline_token);
        } catch (...) {
        }
        e.media_token = {};
        e.playback_token = {};
        e.timeline_token = {};
    }

    void unhook_sessions_locked() {
        for (auto& e : entries_) {
            unhook_entry(e);
        }
        entries_.clear();
    }

    static void populate_player_from_session(MediaPlayer& player,
                                             GlobalSystemMediaTransportControlsSession const& s,
                                             bool notify_changes) {
        const auto old_status = player.playback_status();
        const auto old_loop = player.loop_status();
        const auto old_shuffle = player.shuffle_status();
        const auto old_rate = player.rate();
        const auto old_caps = player.capabilities();
        const auto old_meta = player.metadata();
        const auto old_pos = player.model().base_position_usec();

        // 1. Playback info & capabilities
        try {
            if (auto info = s.GetPlaybackInfo()) {
                player.model().set_playback_status(map_playback_status(info.PlaybackStatus()));
                if (auto shuf = info.IsShuffleActive()) {
                    player.model().set_shuffle_status(
                        shuf.Value() ? ShuffleStatus::On : ShuffleStatus::Off);
                }
                if (auto rep = info.AutoRepeatMode()) {
                    player.model().set_loop_status(map_loop_status(rep.Value()));
                }
                if (auto rate_ref = info.PlaybackRate()) {
                    player.model().set_rate(rate_ref.Value());
                }
                if (auto ctrls = info.Controls()) {
                    PlayerCapabilities caps;
                    caps.can_play = ctrls.IsPlayEnabled() || ctrls.IsPlayPauseToggleEnabled();
                    caps.can_pause = ctrls.IsPauseEnabled() || ctrls.IsPlayPauseToggleEnabled();
                    caps.can_seek = ctrls.IsPlaybackPositionEnabled();
                    caps.can_go_next = ctrls.IsNextEnabled();
                    caps.can_go_previous = ctrls.IsPreviousEnabled();
                    caps.can_control = caps.can_play || caps.can_pause || caps.can_seek ||
                                       caps.can_go_next || caps.can_go_previous ||
                                       ctrls.IsStopEnabled();
                    player.model().set_capabilities(caps);
                }
            }
        } catch (...) {
        }

        // 2. Timeline properties (position and duration)
        int64_t duration_usec = 0;
        int64_t position_usec = 0;
        bool has_timeline = false;
        try {
            if (auto tl = s.GetTimelineProperties()) {
                auto start = tl.StartTime();
                auto end = tl.EndTime();
                auto pos = tl.Position();
                if (end > start) {
                    duration_usec =
                        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                }
                position_usec =
                    std::chrono::duration_cast<std::chrono::microseconds>(pos - start).count();
                if (position_usec < 0) position_usec = 0;
                player.model().set_position(position_usec);
                has_timeline = true;
            }
        } catch (...) {
        }

        // 3. Media properties (title, artist, album, genres, track number)
        try {
            auto props = wait_async(s.TryGetMediaPropertiesAsync(), 1500);
            if (props) {
                MediaMetadata meta;
                meta.title = winrt::to_string(props.Title());
                meta.artist = winrt::to_string(props.Artist());
                if (!meta.artist.empty()) {
                    meta.artists.push_back(meta.artist);
                }
                meta.album = winrt::to_string(props.AlbumTitle());
                meta.album_artist = winrt::to_string(props.AlbumArtist());
                meta.track_number = props.TrackNumber();
                meta.duration_usec = duration_usec;
                if (!meta.title.empty()) {
                    meta.track_id = player.id() + "/" + meta.title;
                }
                try {
                    if (auto genres = props.Genres()) {
                        for (auto const& g : genres) {
                            std::string gs = winrt::to_string(g);
                            if (!gs.empty()) meta.genres.push_back(std::move(gs));
                        }
                    }
                } catch (...) {
                }
                player.model().set_metadata(std::move(meta));
            }
        } catch (...) {
            if (duration_usec > 0) {
                MediaMetadata meta = player.metadata();
                meta.duration_usec = duration_usec;
                player.model().set_metadata(std::move(meta));
            }
        }

        if (!notify_changes) return;

        if (player.playback_status() != old_status) {
            player.notify_playback_status_changed(player.playback_status());
        }
        if (player.loop_status() != old_loop) {
            player.notify_loop_status_changed(player.loop_status());
        }
        if (player.shuffle_status() != old_shuffle) {
            player.notify_shuffle_status_changed(player.shuffle_status());
        }
        if (player.rate() != old_rate) {
            player.notify_rate_changed(player.rate());
        }
        if (!(player.capabilities() == old_caps)) {
            player.notify_capabilities_changed(player.capabilities());
        }
        if (!(player.metadata() == old_meta)) {
            player.notify_metadata_changed(player.metadata());
        }
        if (has_timeline && std::abs(position_usec - old_pos) > 2'000'000) {
            player.notify_position_seeked(position_usec);
        }
    }

    void refresh_sessions(bool notify) {
        ensure_com_initialized();
        if (!manager_) return;

        std::vector<GlobalSystemMediaTransportControlsSession> raw_sessions;
        GlobalSystemMediaTransportControlsSession current_session{nullptr};
        try {
            if (auto view = manager_.GetSessions()) {
                for (auto const& s : view) {
                    if (s) raw_sessions.push_back(s);
                }
            }
            current_session = manager_.GetCurrentSession();
        } catch (...) {
            return;
        }

        std::vector<std::shared_ptr<MediaPlayer>> added_players;
        std::vector<std::string> removed_ids;
        std::shared_ptr<MediaPlayer> new_active;
        bool active_changed = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Assign deterministic unique IDs per session
            std::map<std::string, int> counts;
            struct Candidate {
                std::string id;
                std::string aumid;
                GlobalSystemMediaTransportControlsSession session;
            };
            std::vector<Candidate> candidates;
            candidates.reserve(raw_sessions.size());
            for (auto const& s : raw_sessions) {
                std::string aumid;
                try {
                    aumid = winrt::to_string(s.SourceAppUserModelId());
                } catch (...) {
                }
                if (aumid.empty()) aumid = "smtc.session";
                int n = ++counts[aumid];
                std::string id = (n == 1) ? aumid : (aumid + "#" + std::to_string(n));
                candidates.push_back(Candidate{std::move(id), std::move(aumid), s});
            }

            // Remove sessions that disappeared
            std::vector<SessionEntry> next_entries;
            for (auto& old_e : entries_) {
                bool still_present = false;
                for (auto const& c : candidates) {
                    if (c.id == old_e.id) {
                        still_present = true;
                        break;
                    }
                }
                if (!still_present) {
                    unhook_entry(old_e);
                    removed_ids.push_back(old_e.id);
                } else {
                    next_entries.push_back(std::move(old_e));
                }
            }

            // Update existing or add new sessions
            std::vector<SessionEntry> final_entries;
            std::vector<std::shared_ptr<MediaPlayer>> next_players;
            for (auto const& c : candidates) {
                auto it = std::find_if(next_entries.begin(), next_entries.end(),
                                       [&](const SessionEntry& e) { return e.id == c.id; });
                if (it != next_entries.end()) {
                    it->session = c.session;
                    it->player->set_controller(
                        std::make_shared<WindowsPlaybackController>(c.session));
                    populate_player_from_session(*it->player, c.session, notify);
                    next_players.push_back(it->player);
                    final_entries.push_back(std::move(*it));
                } else {
                    SessionEntry entry;
                    entry.id = c.id;
                    entry.session = c.session;
                    entry.player = std::make_shared<MediaPlayer>(c.id);
                    entry.player->model().set_desktop_entry(c.aumid);
                    entry.player->model().set_identity(friendly_identity_from_aumid(c.aumid));
                    entry.player->set_controller(
                        std::make_shared<WindowsPlaybackController>(c.session));
                    populate_player_from_session(*entry.player, c.session, false);

                    try {
                        entry.media_token = c.session.MediaPropertiesChanged(
                            [this](auto const&, auto const&) { mark_dirty(); });
                        entry.playback_token = c.session.PlaybackInfoChanged(
                            [this](auto const&, auto const&) { mark_dirty(); });
                        entry.timeline_token = c.session.TimelinePropertiesChanged(
                            [this](auto const&, auto const&) { mark_dirty(); });
                    } catch (...) {
                    }

                    added_players.push_back(entry.player);
                    next_players.push_back(entry.player);
                    final_entries.push_back(std::move(entry));
                }
            }

            entries_ = std::move(final_entries);
            players_ = std::move(next_players);

            // Resolve active player from CurrentSession (or first playing/available)
            std::string current_aumid;
            if (current_session) {
                try {
                    current_aumid = winrt::to_string(current_session.SourceAppUserModelId());
                } catch (...) {
                }
            }
            for (const auto& p : players_) {
                if (!current_aumid.empty() && p->desktop_entry() == current_aumid) {
                    new_active = p;
                    break;
                }
            }
            if (!new_active) {
                for (const auto& p : players_) {
                    if (p->playback_status() == PlaybackStatus::Playing) {
                        new_active = p;
                        break;
                    }
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

        for (const auto& id : removed_ids) {
            notify_player_removed(id);
        }
        for (const auto& p : added_players) {
            notify_player_added(p);
        }
        if (active_changed) {
            notify_active_player_changed(new_active);
        }
    }

    mutable std::mutex mutex_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> dirty_{false};

    GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
    winrt::event_token sessions_token_{};
    winrt::event_token current_token_{};

    std::vector<SessionEntry> entries_;
    std::vector<std::shared_ptr<MediaPlayer>> players_;
    std::shared_ptr<MediaPlayer> active_player_;
};

std::unique_ptr<MediaManager> create_windows_media_manager() {
    return std::make_unique<WindowsMediaManager>();
}

#else

std::unique_ptr<MediaManager> create_windows_media_manager() {
    return nullptr;
}

#endif  // _WIN32

}  // namespace brompris

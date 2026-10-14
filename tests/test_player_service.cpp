#include "check.h"
#include "brompris/brompris.h"

#if defined(BROMPRIS_HAS_BRODBUS)
#include "brodbus/bus.h"
#include "brodbus/private_bus.h"
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

using namespace brompris;
using namespace std::chrono_literals;

namespace {

class RecordingPlayerObserver : public PlayerObserver {
public:
    std::mutex mu;
    int status_changes = 0;
    int metadata_changes = 0;
    int seeked_events = 0;
    int volume_changes = 0;
    int rate_changes = 0;
    int loop_changes = 0;
    int shuffle_changes = 0;
    int caps_changes = 0;

    PlaybackStatus last_status = PlaybackStatus::Stopped;
    MediaMetadata last_metadata;
    int64_t last_seek_pos = 0;
    double last_volume = 1.0;
    double last_rate = 1.0;
    LoopStatus last_loop = LoopStatus::None;
    ShuffleStatus last_shuffle = ShuffleStatus::Off;
    PlayerCapabilities last_caps;

    void on_playback_status_changed(MediaPlayer&, PlaybackStatus status) override {
        std::lock_guard lock(mu);
        ++status_changes;
        last_status = status;
    }
    void on_metadata_changed(MediaPlayer&, const MediaMetadata& meta) override {
        std::lock_guard lock(mu);
        ++metadata_changes;
        last_metadata = meta;
    }
    void on_position_seeked(MediaPlayer&, int64_t pos) override {
        std::lock_guard lock(mu);
        ++seeked_events;
        last_seek_pos = pos;
    }
    void on_volume_changed(MediaPlayer&, double vol) override {
        std::lock_guard lock(mu);
        ++volume_changes;
        last_volume = vol;
    }
    void on_rate_changed(MediaPlayer&, double rate) override {
        std::lock_guard lock(mu);
        ++rate_changes;
        last_rate = rate;
    }
    void on_loop_status_changed(MediaPlayer&, LoopStatus loop) override {
        std::lock_guard lock(mu);
        ++loop_changes;
        last_loop = loop;
    }
    void on_shuffle_status_changed(MediaPlayer&, ShuffleStatus shuffle) override {
        std::lock_guard lock(mu);
        ++shuffle_changes;
        last_shuffle = shuffle;
    }
    void on_capabilities_changed(MediaPlayer&, const PlayerCapabilities& caps) override {
        std::lock_guard lock(mu);
        ++caps_changes;
        last_caps = caps;
    }
};

}  // namespace

void test_service_state_unit() {
    CHECK_EQ(PlayerService::normalize_bus_name("helm_music"),
             "org.mpris.MediaPlayer2.helm_music");
    CHECK_EQ(PlayerService::normalize_bus_name("helm.music"),
             "org.mpris.MediaPlayer2.helm.music");
    CHECK_EQ(PlayerService::normalize_bus_name("org.mpris.MediaPlayer2.custom"),
             "org.mpris.MediaPlayer2.custom");

    PlayerServiceConfig cfg;
    cfg.name = "helm_music";
    cfg.identity = "Music";
    cfg.desktop_entry = "helm.music";
    cfg.volume = 0.8;
    cfg.playback_status = PlaybackStatus::Paused;

    PlayerService svc(cfg);
    CHECK_EQ(svc.id(), "org.mpris.MediaPlayer2.helm_music");
    CHECK_EQ(svc.name(), "helm_music");
    CHECK_EQ(svc.identity(), "Music");
    CHECK_EQ(svc.desktop_entry(), "helm.music");
    CHECK_DOUBLE_EQ(svc.volume(), 0.8, 0.001);
    CHECK_EQ(svc.playback_status(), PlaybackStatus::Paused);

    MediaMetadata meta;
    meta.title = "Time";
    meta.artists = {"Pink Floyd"};
    meta.album = "The Dark Side of the Moon";
    meta.album_artists = {"Pink Floyd"};
    meta.duration_usec = 413'000'000;
    meta.track_id = "/org/mpris/MediaPlayer2/Track/4";
    meta.track_number = 4;
    svc.set_metadata(meta);

    CHECK_EQ(svc.metadata().title, "Time");
    CHECK_EQ(svc.metadata().artist, "Pink Floyd");
    CHECK_EQ(svc.metadata().album_artist, "Pink Floyd");
    CHECK_EQ(svc.metadata().track_number, 4);

    svc.set_position(12'000'000);
    CHECK_EQ(svc.position_usec(), 12'000'000);

    svc.set_loop_status(LoopStatus::Playlist);
    CHECK_EQ(svc.loop_status(), LoopStatus::Playlist);

    svc.set_shuffle(true);
    CHECK(svc.shuffle());
    CHECK_EQ(svc.shuffle_status(), ShuffleStatus::On);
}

#if defined(BROMPRIS_HAS_BRODBUS) && defined(__linux__)
void test_service_dbus_roundtrip() {
    brodbus::PrivateBus daemon;
    if (!daemon.is_valid()) {
        bstest::skip("test_player_service", "Private D-Bus daemon could not be started");
    }

    PlayerServiceConfig cfg;
    cfg.name = "helm_music";
    cfg.identity = "Music";
    cfg.desktop_entry = "helm.music";
    cfg.capabilities.can_control = true;
    cfg.capabilities.can_play = true;
    cfg.capabilities.can_pause = true;
    cfg.capabilities.can_seek = true;
    cfg.capabilities.can_go_next = true;
    cfg.capabilities.can_go_previous = true;
    cfg.capabilities.can_raise = true;
    cfg.capabilities.can_quit = true;
    cfg.volume = 0.9;
    cfg.position_usec = 5'000'000;

    MediaMetadata init_meta;
    init_meta.title = "Speak to Me";
    init_meta.artist = "Pink Floyd";
    init_meta.album = "The Dark Side of the Moon";
    init_meta.album_art_url = "file:///music/dsotm.jpg";
    init_meta.duration_usec = 68'000'000;
    init_meta.track_id = "/org/mpris/MediaPlayer2/Track/1";
    init_meta.track_number = 1;
    init_meta.url = "file:///music/01_speak_to_me.flac";
    cfg.metadata = init_meta;

    std::mutex cb_mu;
    int play_count = 0;
    int pause_count = 0;
    int play_pause_count = 0;
    int stop_count = 0;
    int next_count = 0;
    int prev_count = 0;
    int seek_count = 0;
    int64_t last_seek_offset = 0;
    int set_pos_count = 0;
    std::string last_pos_track;
    int64_t last_pos_usec = 0;
    int open_uri_count = 0;
    std::string last_uri;
    int raise_count = 0;
    int quit_count = 0;
    double cb_volume = 0.0;
    double cb_rate = 0.0;
    LoopStatus cb_loop = LoopStatus::None;
    bool cb_shuffle = false;

    PlayerServiceCallbacks cbs;
    cbs.on_play = [&] {
        std::lock_guard lock(cb_mu);
        ++play_count;
    };
    cbs.on_pause = [&] {
        std::lock_guard lock(cb_mu);
        ++pause_count;
    };
    cbs.on_play_pause = [&] {
        std::lock_guard lock(cb_mu);
        ++play_pause_count;
    };
    cbs.on_stop = [&] {
        std::lock_guard lock(cb_mu);
        ++stop_count;
    };
    cbs.on_next = [&] {
        std::lock_guard lock(cb_mu);
        ++next_count;
    };
    cbs.on_previous = [&] {
        std::lock_guard lock(cb_mu);
        ++prev_count;
    };
    cbs.on_seek = [&](int64_t offset) {
        std::lock_guard lock(cb_mu);
        ++seek_count;
        last_seek_offset = offset;
    };
    cbs.on_set_position = [&](const std::string& track_id, int64_t pos) {
        std::lock_guard lock(cb_mu);
        ++set_pos_count;
        last_pos_track = track_id;
        last_pos_usec = pos;
    };
    cbs.on_open_uri = [&](const std::string& uri) {
        std::lock_guard lock(cb_mu);
        ++open_uri_count;
        last_uri = uri;
    };
    cbs.on_raise = [&] {
        std::lock_guard lock(cb_mu);
        ++raise_count;
    };
    cbs.on_quit = [&] {
        std::lock_guard lock(cb_mu);
        ++quit_count;
    };
    cbs.on_set_volume = [&](double v) {
        std::lock_guard lock(cb_mu);
        cb_volume = v;
    };
    cbs.on_set_rate = [&](double r) {
        std::lock_guard lock(cb_mu);
        cb_rate = r;
    };
    cbs.on_set_loop_status = [&](LoopStatus l) {
        std::lock_guard lock(cb_mu);
        cb_loop = l;
    };
    cbs.on_set_shuffle = [&](bool s) {
        std::lock_guard lock(cb_mu);
        cb_shuffle = s;
    };

    auto service = PlayerService::create_with_address(daemon.address(), cfg, cbs);
    REQUIRE(service);
    REQUIRE(service->is_running());
    REQUIRE(service->is_connected());
    CHECK_EQ(service->id(), "org.mpris.MediaPlayer2.helm_music");

    // Create MediaManager on the same test bus (on the SAME thread!)
    auto manager = MediaManager::create_with_address(daemon.address());
    REQUIRE(manager);

    bool found = bstest::wait_until([&] {
        manager->wait(20000);
        return !manager->active_players().empty();
    }, 2000ms);
    REQUIRE(found);

    auto player = manager->find_player("org.mpris.MediaPlayer2.helm_music");
    REQUIRE(player);
    CHECK_EQ(player->identity(), "Music");
    CHECK_EQ(player->desktop_entry(), "helm.music");
    CHECK_DOUBLE_EQ(player->volume(), 0.9, 0.001);
    CHECK_EQ(player->position_usec(), 5'000'000);
    CHECK_EQ(player->metadata().title, "Speak to Me");
    CHECK_EQ(player->metadata().artist, "Pink Floyd");
    CHECK_EQ(player->metadata().album, "The Dark Side of the Moon");
    CHECK_EQ(player->metadata().duration_usec, 68'000'000);
    CHECK_EQ(player->metadata().track_id, "/org/mpris/MediaPlayer2/Track/1");
    CHECK(player->capabilities().can_control);
    CHECK(player->capabilities().can_play);
    CHECK(player->capabilities().can_pause);
    CHECK(player->capabilities().can_seek);
    CHECK(player->capabilities().can_go_next);
    CHECK(player->capabilities().can_go_previous);
    CHECK(player->capabilities().can_raise);
    CHECK(player->capabilities().can_quit);

    RecordingPlayerObserver obs;
    player->add_observer(&obs);

    // 1. Update state on PlayerService and verify MediaPlayer receives PropertiesChanged & Seeked
    service->set_playback_status(PlaybackStatus::Playing);
    bool got_playing = bstest::wait_until([&] {
        manager->wait(20000);
        return player->playback_status() == PlaybackStatus::Playing;
    }, 2000ms);
    CHECK(got_playing);

    MediaMetadata next_meta;
    next_meta.title = "Breathe (In the Air)";
    next_meta.artists = {"Pink Floyd"};
    next_meta.album = "The Dark Side of the Moon";
    next_meta.album_artists = {"Pink Floyd"};
    next_meta.album_art_url = "file:///music/dsotm.jpg";
    next_meta.duration_usec = 169'000'000;
    next_meta.track_id = "/org/mpris/MediaPlayer2/Track/2";
    next_meta.track_number = 2;
    next_meta.url = "file:///music/02_breathe.flac";
    service->set_metadata(next_meta);

    bool got_meta = bstest::wait_until([&] {
        manager->wait(20000);
        return player->metadata().title == "Breathe (In the Air)";
    }, 2000ms);
    CHECK(got_meta);
    CHECK_EQ(player->metadata().artist, "Pink Floyd");
    CHECK_EQ(player->metadata().album_artist, "Pink Floyd");
    CHECK_EQ(player->metadata().track_number, 2);
    CHECK_EQ(player->metadata().duration_usec, 169'000'000);
    CHECK_EQ(player->metadata().url, "file:///music/02_breathe.flac");

    // Emit Seeked from service
    service->emit_seeked(42'000'000);
    bool got_seeked = bstest::wait_until([&] {
        manager->wait(20000);
        std::lock_guard lock(obs.mu);
        return obs.seeked_events > 0 && obs.last_seek_pos == 42'000'000;
    }, 2000ms);
    CHECK(got_seeked);

    // 2. Send commands from MediaPlayer controller to PlayerService
    CHECK(player->play());
    CHECK(player->pause());
    CHECK(player->play_pause());
    CHECK(player->stop());
    CHECK(player->next());
    CHECK(player->previous());
    CHECK(player->seek(-3'000'000));
    CHECK(player->set_position("/org/mpris/MediaPlayer2/Track/2", 30'000'000));
    CHECK(player->open_uri("file:///music/03_on_the_run.flac"));
    CHECK(player->raise());
    CHECK(player->quit());

    {
        std::lock_guard lock(cb_mu);
        CHECK_EQ(play_count, 1);
        CHECK_EQ(pause_count, 1);
        CHECK_EQ(play_pause_count, 1);
        CHECK_EQ(stop_count, 1);
        CHECK_EQ(next_count, 1);
        CHECK_EQ(prev_count, 1);
        CHECK_EQ(seek_count, 1);
        CHECK_EQ(last_seek_offset, -3'000'000);
        CHECK_EQ(set_pos_count, 1);
        CHECK_EQ(last_pos_track, "/org/mpris/MediaPlayer2/Track/2");
        CHECK_EQ(last_pos_usec, 30'000'000);
        CHECK_EQ(open_uri_count, 1);
        CHECK_EQ(last_uri, "file:///music/03_on_the_run.flac");
        CHECK_EQ(raise_count, 1);
        CHECK_EQ(quit_count, 1);
    }

    // 3. Test property writes from MediaPlayer controller (Volume, LoopStatus, Shuffle, Rate)
    CHECK(player->set_volume(0.35));
    CHECK(player->set_loop_status(LoopStatus::Track));
    CHECK(player->set_shuffle_status(ShuffleStatus::On));
    CHECK(player->set_rate(1.5));

    {
        std::lock_guard lock(cb_mu);
        CHECK_DOUBLE_EQ(cb_volume, 0.35, 0.001);
        CHECK_EQ(cb_loop, LoopStatus::Track);
        CHECK(cb_shuffle);
        CHECK_DOUBLE_EQ(cb_rate, 1.5, 0.001);
    }
    CHECK_DOUBLE_EQ(service->volume(), 0.35, 0.001);
    CHECK_EQ(service->loop_status(), LoopStatus::Track);
    CHECK(service->shuffle());
    CHECK_DOUBLE_EQ(service->rate(), 1.5, 0.001);

    // Ensure PropertiesChanged from those writes arrived back at MediaPlayer
    bool got_prop_updates = bstest::wait_until([&] {
        manager->wait(20000);
        return std::abs(player->volume() - 0.35) < 0.01 &&
               player->loop_status() == LoopStatus::Track &&
               player->shuffle_status() == ShuffleStatus::On &&
               std::abs(player->rate() - 1.5) < 0.01;
    }, 2000ms);
    CHECK(got_prop_updates);

    // 4. Test second instance with same name acquires an instance suffix
    auto service2 = PlayerService::create_with_address(daemon.address(), cfg);
    REQUIRE(service2);
    CHECK(service2->is_connected());
    CHECK(service2->id() != service->id());
    CHECK_EQ(service2->id().rfind("org.mpris.MediaPlayer2.helm_music.instance", 0), 0u);
    service2->stop();

    player->remove_observer(&obs);

    // 5. Stop service and verify manager detects player removal
    service->stop();
    bool removed = bstest::wait_until([&] {
        manager->wait(20000);
        return manager->find_player("org.mpris.MediaPlayer2.helm_music") == nullptr;
    }, 2000ms);
    CHECK(removed);
}
#endif

int main() {
    test_service_state_unit();
#if defined(BROMPRIS_HAS_BRODBUS) && defined(__linux__)
    test_service_dbus_roundtrip();
#endif
    return bstest::finish("test_player_service");
}

#include "../src/api/api.h"
#include "check.h"
#include "embed/embed.h"
#include "eval/eval.h"
#include "brompris/brompris.h"

#if defined(BROMPRIS_HAS_BRODBUS)
#include "brodbus/private_bus.h"
#endif

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace brompris;
using namespace std::chrono_literals;

namespace {

class TestPlaybackController : public PlaybackController {
public:
    bool played = false;
    bool paused = false;
    bool play_paused = false;
    bool stopped = false;
    bool next_called = false;
    bool prev_called = false;
    int64_t last_seek_offset = 0;
    int64_t last_position = 0;
    std::string last_track_id;
    double last_volume = 1.0;

    bool play() override { played = true; return true; }
    bool pause() override { paused = true; return true; }
    bool play_pause() override { play_paused = true; return true; }
    bool stop() override { stopped = true; return true; }
    bool next() override { next_called = true; return true; }
    bool previous() override { prev_called = true; return true; }
    bool seek(int64_t offset_usec) override { last_seek_offset = offset_usec; return true; }
    bool set_position(const std::string& track_id, int64_t position_usec) override {
        last_track_id = track_id;
        last_position = position_usec;
        return true;
    }
    bool set_volume(double volume) override { last_volume = volume; return true; }
    bool set_loop_status(LoopStatus) override { return true; }
    bool set_shuffle_status(ShuffleStatus) override { return true; }
    bool set_rate(double) override { return true; }
    bool open_uri(const std::string&) override { return true; }
    bool raise() override { return true; }
    bool quit() override { return true; }
};

class TestMediaManager : public MediaManager {
public:
    std::vector<std::shared_ptr<MediaPlayer>> active_players() const override {
        std::lock_guard lock(mu_);
        return players_;
    }
    std::shared_ptr<MediaPlayer> active_player() const override {
        std::lock_guard lock(mu_);
        return active_;
    }
    std::shared_ptr<MediaPlayer> find_player(const std::string& id) const override {
        std::lock_guard lock(mu_);
        for (const auto& p : players_) {
            if (p && p->id() == id) return p;
        }
        return nullptr;
    }
    int process() override {
        return 0;
    }
    int wait(uint64_t) override {
        return 0;
    }
    void update() override {}

    void add(std::shared_ptr<MediaPlayer> player) {
        {
            std::lock_guard lock(mu_);
            players_.push_back(player);
        }
        notify_player_added(player);
    }

    void remove(const std::string& id) {
        {
            std::lock_guard lock(mu_);
            auto it = std::remove_if(players_.begin(), players_.end(),
                                     [&](const auto& p) { return p && p->id() == id; });
            players_.erase(it, players_.end());
            if (active_ && active_->id() == id) active_ = nullptr;
        }
        notify_player_removed(id);
    }

    void set_active(std::shared_ptr<MediaPlayer> player) {
        {
            std::lock_guard lock(mu_);
            active_ = player;
        }
        notify_active_player_changed(player);
    }

private:
    mutable std::mutex mu_;
    std::vector<std::shared_ptr<MediaPlayer>> players_;
    std::shared_ptr<MediaPlayer> active_;
};

} // namespace

int main() {
    namespace ev = bronze::embed;
    using namespace bronze::eval;

    std::cout << "Starting brompris JavaScript API test..." << std::endl;

    // 1. Install bro.mpris into Bronze realm
    brompris::api::installMpris();

    auto g = ev::globalValue("bro");
    REQUIRE(g.found);
    REQUIRE(ev::isObject(g.value));

    ev::Persistent mpris(ev::getProperty(g.value, "mpris"));
    REQUIRE(ev::isObject(mpris.get()));
    std::cout << "  Mounted bro.mpris successfully." << std::endl;

    // Verify all required methods exist on bro.mpris
    const char* methods[] = {
        "getPlayers", "getActivePlayer", "findPlayer", "getMetadata",
        "play", "pause", "playPause", "stop", "next", "previous",
        "seek", "setPosition", "setVolume", "setLoopStatus", "setShuffle", "setRate",
        "openUri", "raise", "quit",
        "createService", "publish",
        "on", "off", "addEventListener", "removeEventListener"
    };
    for (const char* m : methods) {
        auto fn = ev::getProperty(mpris.get(), m);
        CHECK(ev::isFunction(fn));
    }
    std::cout << "  All required methods exist on bro.mpris [PASS]" << std::endl;

    // 2. Test initial empty state with a fresh manager
    auto testMgr = std::make_shared<TestMediaManager>();
    brompris::api::setMediaManager(testMgr);
    CHECK_EQ(brompris::api::getMediaManager(), testMgr);

    {
        auto r = evalScript(
            "(function() {\n"
            "  const players = bro.mpris.getPlayers();\n"
            "  if (!Array.isArray(players) || players.length !== 0) return false;\n"
            "  if (bro.mpris.getActivePlayer() !== null) return false;\n"
            "  if (bro.mpris.findPlayer('test') !== null) return false;\n"
            "  if (bro.mpris.getMetadata('test') !== null) return false;\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!r.thrown);
        REQUIRE(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  Empty state behavior [PASS]" << std::endl;
    }

    // 3. Create and register real MediaPlayer instances
    auto player1 = std::make_shared<MediaPlayer>("org.mpris.MediaPlayer2.vlc");
    player1->model().set_identity("VLC media player");
    player1->model().set_playback_status(PlaybackStatus::Playing);
    player1->model().set_volume(0.85);
    player1->model().set_position(10000000); // 10 seconds

    PlayerCapabilities caps1;
    caps1.can_control = true;
    caps1.can_play = true;
    caps1.can_pause = true;
    caps1.can_seek = true;
    caps1.can_go_next = true;
    caps1.can_go_previous = true;
    player1->model().set_capabilities(caps1);

    MediaMetadata meta1;
    meta1.title = "Symphony No. 5";
    meta1.artist = "Beethoven";
    meta1.album = "Classical Masterpieces";
    meta1.album_art_url = "file:///music/art.jpg";
    meta1.duration_usec = 420000000; // 420s
    meta1.track_id = "/org/mpris/MediaPlayer2/Track/1";
    player1->model().set_metadata(meta1);
    player1->model().set_position(10000000); // 10 seconds

    auto ctrl1 = std::make_shared<TestPlaybackController>();
    player1->set_controller(ctrl1);

    testMgr->add(player1);
    testMgr->set_active(player1);
    brompris::api::tickMprisAsync();

    // 4. Test player retrieval and metadata
    {
        auto r = evalScript(
            "(function() {\n"
            "  const players = bro.mpris.getPlayers();\n"
            "  if (!Array.isArray(players) || players.length !== 1) return 'players len: ' + (players ? players.length : 'null');\n"
            "  const p = players[0];\n"
            "  if (p.id !== 'org.mpris.MediaPlayer2.vlc') return 'id: ' + p.id;\n"
            "  if (p.identity !== 'VLC media player') return 'identity: ' + p.identity;\n"
            "  if (p.playbackStatus !== 'Playing') return 'status: ' + p.playbackStatus;\n"
            "  if (p.canControl !== true || p.canPlay !== true || p.canPause !== true) return 'caps1';\n"
            "  if (p.canSeek !== true || p.canGoNext !== true || p.canGoPrevious !== true) return 'caps2';\n"
            "  if (Math.abs(p.volume - 0.85) > 0.001) return 'vol: ' + p.volume;\n"
            "  if (p.position < 10000000) return 'pos: ' + p.position;\n"
            "\n"
            "  const active = bro.mpris.getActivePlayer();\n"
            "  if (!active || active.id !== 'org.mpris.MediaPlayer2.vlc') return 'active: ' + (active ? active.id : 'null');\n"
            "\n"
            "  const found = bro.mpris.findPlayer('org.mpris.MediaPlayer2.vlc');\n"
            "  if (!found || found.id !== 'org.mpris.MediaPlayer2.vlc') return 'found: ' + (found ? found.id : 'null');\n"
            "\n"
            "  const notFound = bro.mpris.findPlayer('nonexistent');\n"
            "  if (notFound !== null) return 'notFound should be null';\n"
            "\n"
            "  const meta = bro.mpris.getMetadata('org.mpris.MediaPlayer2.vlc');\n"
            "  if (!meta) return 'meta null';\n"
            "  if (meta.title !== 'Symphony No. 5') return 'title: ' + meta.title;\n"
            "  if (meta.artist !== 'Beethoven') return 'artist: ' + meta.artist;\n"
            "  if (meta.album !== 'Classical Masterpieces') return 'album: ' + meta.album;\n"
            "  if (meta.albumArtUrl !== 'file:///music/art.jpg') return 'art: ' + meta.albumArtUrl;\n"
            "  if (meta.duration !== 420000000) return 'duration: ' + meta.duration;\n"
            "  if (meta.trackId !== '/org/mpris/MediaPlayer2/Track/1') return 'trackId: ' + meta.trackId;\n"
            "\n"
            "  const activeMeta = bro.mpris.getMetadata();\n"
            "  if (!activeMeta || activeMeta.title !== 'Symphony No. 5') return 'activeMeta title';\n"
            "\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!r.thrown);
        if (ev::isString(r.value)) {
            std::cerr << "Section 4 check failed: " << ev::toUtf8(r.value) << std::endl;
        }
        REQUIRE(ev::isBool(r.value) && ev::toBool(r.value));
        std::cout << "  getPlayers(), getActivePlayer(), findPlayer(), getMetadata() [PASS]" << std::endl;
    }

    // 5. Test control commands
    {
        auto rPlay = evalScript("bro.mpris.play();");
        REQUIRE(!rPlay.thrown && ev::toBool(rPlay.value));
        CHECK(ctrl1->played);

        auto rPause = evalScript("bro.mpris.pause();");
        REQUIRE(!rPause.thrown && ev::toBool(rPause.value));
        CHECK(ctrl1->paused);

        auto rPlayPause = evalScript("bro.mpris.playPause();");
        REQUIRE(!rPlayPause.thrown && ev::toBool(rPlayPause.value));
        CHECK(ctrl1->play_paused);

        auto rNext = evalScript("bro.mpris.next();");
        REQUIRE(!rNext.thrown && ev::toBool(rNext.value));
        CHECK(ctrl1->next_called);

        auto rPrev = evalScript("bro.mpris.previous();");
        REQUIRE(!rPrev.thrown && ev::toBool(rPrev.value));
        CHECK(ctrl1->prev_called);

        auto rStop = evalScript("bro.mpris.stop();");
        REQUIRE(!rStop.thrown && ev::toBool(rStop.value));
        CHECK(ctrl1->stopped);

        auto rSeek = evalScript("bro.mpris.seek(5000000);");
        REQUIRE(!rSeek.thrown && ev::toBool(rSeek.value));
        CHECK_EQ(ctrl1->last_seek_offset, 5000000);

        auto rSetPos = evalScript("bro.mpris.setPosition(15000000);");
        REQUIRE(!rSetPos.thrown && ev::toBool(rSetPos.value));
        CHECK_EQ(ctrl1->last_position, 15000000);
        CHECK_EQ(ctrl1->last_track_id, "/org/mpris/MediaPlayer2/Track/1");

        auto rSetVol = evalScript("bro.mpris.setVolume(0.4);");
        REQUIRE(!rSetVol.thrown && ev::toBool(rSetVol.value));
        CHECK_DOUBLE_EQ(ctrl1->last_volume, 0.4, 0.001);

        // Control with explicit ID
        ctrl1->played = false;
        auto rPlayId = evalScript("bro.mpris.play('org.mpris.MediaPlayer2.vlc');");
        REQUIRE(!rPlayId.thrown && ev::toBool(rPlayId.value));
        CHECK(ctrl1->played);

        auto rSeekId = evalScript("bro.mpris.seek('org.mpris.MediaPlayer2.vlc', -2000000);");
        REQUIRE(!rSeekId.thrown && ev::toBool(rSeekId.value));
        CHECK_EQ(ctrl1->last_seek_offset, -2000000);

        auto rWrongId = evalScript("bro.mpris.play('nonexistent');");
        REQUIRE(!rWrongId.thrown);
        CHECK(!ev::toBool(rWrongId.value));

        std::cout << "  Control commands (play, pause, seek, setPosition, setVolume) [PASS]" << std::endl;
    }

    // 6. Test event listeners and tickMprisAsync()
    {
        auto rInitEvents = evalScript(
            "(function() {\n"
            "  globalThis._addedEvents = [];\n"
            "  globalThis._removedEvents = [];\n"
            "  globalThis._activeChangedEvents = [];\n"
            "  globalThis._statusEvents = [];\n"
            "  globalThis._metaEvents = [];\n"
            "  globalThis._seekedEvents = [];\n"
            "\n"
            "  bro.mpris.on('playerAdded', (e) => globalThis._addedEvents.push(e));\n"
            "  bro.mpris.addEventListener('playerRemoved', (e) => globalThis._removedEvents.push(e));\n"
            "  bro.mpris.on('activePlayerChanged', (e) => globalThis._activeChangedEvents.push(e));\n"
            "  bro.mpris.addEventListener('playbackStatus', (e) => globalThis._statusEvents.push(e));\n"
            "  bro.mpris.on('metadata', (e) => globalThis._metaEvents.push(e));\n"
            "  bro.mpris.addEventListener('seeked', (e) => globalThis._seekedEvents.push(e));\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!rInitEvents.thrown && ev::toBool(rInitEvents.value));

        // Trigger notifications on player1
        player1->notify_playback_status_changed(PlaybackStatus::Paused);
        player1->notify_position_seeked(25000000);

        MediaMetadata meta2 = meta1;
        meta2.title = "Updated Symphony";
        player1->notify_metadata_changed(meta2);

        // Before tick, JS arrays should be empty
        auto rBeforeTick = evalScript("globalThis._statusEvents.length === 0;");
        REQUIRE(!rBeforeTick.thrown && ev::toBool(rBeforeTick.value));

        // Pump async events
        brompris::api::tickMprisAsync();

        auto rCheckEvents = evalScript(
            "(function() {\n"
            "  if (globalThis._statusEvents.length !== 1) return false;\n"
            "  if (globalThis._statusEvents[0].playbackStatus !== 'Paused') return false;\n"
            "  if (globalThis._statusEvents[0].id !== 'org.mpris.MediaPlayer2.vlc') return false;\n"
            "\n"
            "  if (globalThis._seekedEvents.length !== 1) return false;\n"
            "  if (globalThis._seekedEvents[0].position !== 25000000) return false;\n"
            "\n"
            "  if (globalThis._metaEvents.length !== 1) return false;\n"
            "  if (globalThis._metaEvents[0].title !== 'Updated Symphony') return false;\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!rCheckEvents.thrown);
        REQUIRE(ev::isBool(rCheckEvents.value) && ev::toBool(rCheckEvents.value));
        std::cout << "  playbackStatus, seeked, metadata events dispatched [PASS]" << std::endl;

        // Add player2
        auto player2 = std::make_shared<MediaPlayer>("org.mpris.MediaPlayer2.mpv");
        player2->model().set_identity("mpv media player");
        player2->model().set_playback_status(PlaybackStatus::Stopped);
        testMgr->add(player2);

        testMgr->set_active(player2);

        brompris::api::tickMprisAsync();

        auto rCheckPlayer2 = evalScript(
            "(function() {\n"
            "  if (globalThis._addedEvents.length !== 1) return false;\n"
            "  if (globalThis._addedEvents[0].id !== 'org.mpris.MediaPlayer2.mpv') return false;\n"
            "\n"
            "  if (globalThis._activeChangedEvents.length !== 1) return false;\n"
            "  if (globalThis._activeChangedEvents[0].id !== 'org.mpris.MediaPlayer2.mpv') return false;\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!rCheckPlayer2.thrown);
        REQUIRE(ev::isBool(rCheckPlayer2.value) && ev::toBool(rCheckPlayer2.value));
        std::cout << "  playerAdded, activePlayerChanged events dispatched [PASS]" << std::endl;

        // Remove player2
        testMgr->remove("org.mpris.MediaPlayer2.mpv");
        brompris::api::tickMprisAsync();

        auto rCheckRemoval = evalScript(
            "(function() {\n"
            "  if (globalThis._removedEvents.length !== 1) return false;\n"
            "  if (globalThis._removedEvents[0].id !== 'org.mpris.MediaPlayer2.mpv') return false;\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!rCheckRemoval.thrown);
        REQUIRE(ev::isBool(rCheckRemoval.value) && ev::toBool(rCheckRemoval.value));
        std::cout << "  playerRemoved event dispatched [PASS]" << std::endl;

        // Test unregistering with off
        evalScript("bro.mpris.off('playbackStatus');");
        player1->notify_playback_status_changed(PlaybackStatus::Playing);
        brompris::api::tickMprisAsync();

        auto rCheckOff = evalScript("globalThis._statusEvents.length === 1;");
        REQUIRE(!rCheckOff.thrown && ev::toBool(rCheckOff.value));
        std::cout << "  off() unregisters listener [PASS]" << std::endl;
    }

    // 7. Test bro.mpris.createService / bro.mpris.publish handle & state updates
    {
        auto rSvc = evalScript(
            "(function() {\n"
            "  const svc = bro.mpris.createService({\n"
            "    name: 'helm_music_unit',\n"
            "    identity: 'Music',\n"
            "    desktopEntry: 'helm.music',\n"
            "    volume: 0.75,\n"
            "    playbackStatus: 'Paused',\n"
            "    metadata: {\n"
            "      title: 'Money',\n"
            "      artists: ['Pink Floyd'],\n"
            "      album: 'The Dark Side of the Moon',\n"
            "      artUrl: 'file:///music/cover.jpg',\n"
            "      lengthUs: 382000000,\n"
            "      trackId: '/org/mpris/MediaPlayer2/Track/6',\n"
            "      trackNumber: 6\n"
            "    }\n"
            "  });\n"
            "  if (!svc) return 'svc null';\n"
            "  if (svc.id !== 'org.mpris.MediaPlayer2.helm_music_unit') return 'id: ' + svc.id;\n"
            "  if (svc.identity !== 'Music') return 'identity: ' + svc.identity;\n"
            "  if (svc.desktopEntry !== 'helm.music') return 'de: ' + svc.desktopEntry;\n"
            "  if (svc.getPlaybackStatus() !== 'Paused') return 'status: ' + svc.getPlaybackStatus();\n"
            "  if (Math.abs(svc.getVolume() - 0.75) > 0.001) return 'vol: ' + svc.getVolume();\n"
            "  const m1 = svc.getMetadata();\n"
            "  if (!m1 || m1.title !== 'Money' || m1.artist !== 'Pink Floyd') return 'm1';\n"
            "\n"
            "  svc.setPlaybackStatus('Playing');\n"
            "  if (svc.getPlaybackStatus() !== 'Playing') return 'setPlaybackStatus';\n"
            "  svc.setPosition(15000000);\n"
            "  if (svc.getPosition() < 15000000) return 'setPosition';\n"
            "  svc.setVolume(0.5);\n"
            "  if (Math.abs(svc.getVolume() - 0.5) > 0.001) return 'setVolume';\n"
            "  svc.setRate(1.25);\n"
            "  if (Math.abs(svc.getRate() - 1.25) > 0.001) return 'setRate';\n"
            "  svc.setLoopStatus('Track');\n"
            "  if (svc.getLoopStatus() !== 'Track') return 'setLoopStatus';\n"
            "  svc.setShuffle(true);\n"
            "  if (svc.getShuffle() !== true) return 'setShuffle';\n"
            "  svc.setCapabilities({ canGoNext: false, canRaise: true });\n"
            "  const caps = svc.getCapabilities();\n"
            "  if (caps.canGoNext !== false || caps.canRaise !== true || caps.canPlay !== true) return 'caps';\n"
            "  svc.seeked(22000000);\n"
            "  svc.close();\n"
            "  if (svc.isRunning() !== false) return 'isRunning after close';\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!rSvc.thrown);
        if (ev::isString(rSvc.value)) {
            std::cerr << "Section 7 service unit check failed: " << ev::toUtf8(rSvc.value) << std::endl;
        }
        REQUIRE(ev::isBool(rSvc.value) && ev::toBool(rSvc.value));
        std::cout << "  bro.mpris.createService() state & getters/setters [PASS]" << std::endl;
    }

#if defined(BROMPRIS_HAS_BRODBUS) && defined(__linux__)
    // 8. Test JS Service <-> Controller round-trip over private D-Bus
    {
        brodbus::PrivateBus daemon;
        if (daemon.is_valid()) {
            brompris::api::setDbusAddress(daemon.address());
            std::shared_ptr<MediaManager> dbusMgr = MediaManager::create_with_address(daemon.address());
            REQUIRE(dbusMgr);
            brompris::api::setMediaManager(dbusMgr);

            auto rPublish = evalScript(
                "(function() {\n"
                "  globalThis._svcCommands = [];\n"
                "  globalThis._svcPlayed = 0;\n"
                "  globalThis._svcPaused = 0;\n"
                "  globalThis._svcNext = 0;\n"
                "  globalThis._svcPrev = 0;\n"
                "  globalThis._svcSeekOffset = 0;\n"
                "  globalThis._svcPos = 0;\n"
                "  globalThis._svcPosTrack = '';\n"
                "  globalThis._svcVol = 0;\n"
                "  globalThis._svcLoop = '';\n"
                "  globalThis._svcShuf = false;\n"
                "\n"
                "  globalThis._svc = bro.mpris.publish({\n"
                "    name: 'helm_music',\n"
                "    identity: 'Music',\n"
                "    desktopEntry: 'helm.music',\n"
                "    playbackStatus: 'Paused',\n"
                "    volume: 0.8,\n"
                "    metadata: {\n"
                "      title: 'Us and Them',\n"
                "      artist: 'Pink Floyd',\n"
                "      album: 'The Dark Side of the Moon',\n"
                "      duration: 469000000,\n"
                "      trackId: '/org/mpris/MediaPlayer2/Track/7'\n"
                "    },\n"
                "    onPlay: () => { globalThis._svcPlayed++; },\n"
                "    onPause: () => { globalThis._svcPaused++; },\n"
                "    onNext: () => { globalThis._svcNext++; },\n"
                "    onPrevious: () => { globalThis._svcPrev++; },\n"
                "    onSeek: (e) => { globalThis._svcSeekOffset = e.offset; },\n"
                "    onSetPosition: (e) => { globalThis._svcPos = e.position; globalThis._svcPosTrack = e.trackId; },\n"
                "    onSetVolume: (e) => { globalThis._svcVol = e.volume; },\n"
                "    onSetLoopStatus: (e) => { globalThis._svcLoop = e.loopStatus; },\n"
                "    onSetShuffle: (e) => { globalThis._svcShuf = e.shuffle; }\n"
                "  });\n"
                "  globalThis._svc.onCommand((cmd) => globalThis._svcCommands.push(cmd.command));\n"
                "  return globalThis._svc && globalThis._svc.isConnected();\n"
                "})()\n"
            );
            REQUIRE(!rPublish.thrown && ev::toBool(rPublish.value));

            bool discovered = bstest::wait_until([&] {
                dbusMgr->wait(20000);
                brompris::api::tickMprisAsync();
                return dbusMgr->find_player("org.mpris.MediaPlayer2.helm_music") != nullptr;
            }, 2000ms);
            REQUIRE(discovered);

            // Verify controller JS sees the published service and can control it
            auto rControl = evalScript(
                "(function() {\n"
                "  const p = bro.mpris.findPlayer('org.mpris.MediaPlayer2.helm_music');\n"
                "  if (!p) return 'player not found';\n"
                "  if (p.identity !== 'Music') return 'identity: ' + p.identity;\n"
                "  const meta = bro.mpris.getMetadata('org.mpris.MediaPlayer2.helm_music');\n"
                "  if (!meta || meta.title !== 'Us and Them') return 'meta title';\n"
                "\n"
                "  if (!bro.mpris.play('org.mpris.MediaPlayer2.helm_music')) return 'play failed';\n"
                "  if (!bro.mpris.pause('org.mpris.MediaPlayer2.helm_music')) return 'pause failed';\n"
                "  if (!bro.mpris.next('org.mpris.MediaPlayer2.helm_music')) return 'next failed';\n"
                "  if (!bro.mpris.previous('org.mpris.MediaPlayer2.helm_music')) return 'prev failed';\n"
                "  if (!bro.mpris.seek('org.mpris.MediaPlayer2.helm_music', 7000000)) return 'seek failed';\n"
                "  if (!bro.mpris.setPosition('org.mpris.MediaPlayer2.helm_music', '/org/mpris/MediaPlayer2/Track/7', 33000000)) return 'setPos failed';\n"
                "  if (!bro.mpris.setVolume('org.mpris.MediaPlayer2.helm_music', 0.45)) return 'setVol failed';\n"
                "  if (!bro.mpris.setLoopStatus('org.mpris.MediaPlayer2.helm_music', 'Playlist')) return 'setLoop failed';\n"
                "  if (!bro.mpris.setShuffle('org.mpris.MediaPlayer2.helm_music', true)) return 'setShuffle failed';\n"
                "  return true;\n"
                "})()\n"
            );
            REQUIRE(!rControl.thrown);
            if (ev::isString(rControl.value)) {
                std::cerr << "Section 8 control check failed: " << ev::toUtf8(rControl.value) << std::endl;
            }
            REQUIRE(ev::isBool(rControl.value) && ev::toBool(rControl.value));

            // Tick to dispatch queued service commands to JS callbacks
            brompris::api::tickMprisAsync();

            auto rVerifyCmds = evalScript(
                "(function() {\n"
                "  if (globalThis._svcPlayed !== 1) return 'played: ' + globalThis._svcPlayed;\n"
                "  if (globalThis._svcPaused !== 1) return 'paused: ' + globalThis._svcPaused;\n"
                "  if (globalThis._svcNext !== 1) return 'next: ' + globalThis._svcNext;\n"
                "  if (globalThis._svcPrev !== 1) return 'prev: ' + globalThis._svcPrev;\n"
                "  if (globalThis._svcSeekOffset !== 7000000) return 'seek: ' + globalThis._svcSeekOffset;\n"
                "  if (globalThis._svcPos !== 33000000) return 'pos: ' + globalThis._svcPos;\n"
                "  if (globalThis._svcPosTrack !== '/org/mpris/MediaPlayer2/Track/7') return 'track: ' + globalThis._svcPosTrack;\n"
                "  if (Math.abs(globalThis._svcVol - 0.45) > 0.001) return 'vol: ' + globalThis._svcVol;\n"
                "  if (globalThis._svcLoop !== 'Playlist') return 'loop: ' + globalThis._svcLoop;\n"
                "  if (globalThis._svcShuf !== true) return 'shuf: ' + globalThis._svcShuf;\n"
                "  if (globalThis._svcCommands.length !== 9) return 'cmd len: ' + globalThis._svcCommands.length;\n"
                "  globalThis._svc.close();\n"
                "  return true;\n"
                "})()\n"
            );
            REQUIRE(!rVerifyCmds.thrown);
            if (ev::isString(rVerifyCmds.value)) {
                std::cerr << "Section 8 verify commands failed: " << ev::toUtf8(rVerifyCmds.value) << std::endl;
            }
            REQUIRE(ev::isBool(rVerifyCmds.value) && ev::toBool(rVerifyCmds.value));

            brompris::api::setDbusAddress("");
            std::cout << "  JS Service <-> Controller D-Bus round-trip [PASS]" << std::endl;
        }
    }
#endif

    // 9. Test shutdownMprisAsync()
    {
        brompris::api::shutdownMprisAsync();
        // After shutdown, events queued should not throw
        brompris::api::tickMprisAsync();
        std::cout << "  shutdownMprisAsync() [PASS]" << std::endl;
    }

    // 10. GC stress simulation loop
    {
        brompris::api::setMediaManager(testMgr);
        brompris::api::setPlayerServiceFactory(
            [](const PlayerServiceConfig& cfg, PlayerServiceCallbacks cbs) {
                return std::make_unique<PlayerService>(cfg, std::move(cbs));
            });
        auto rStress = evalScript(
            "(function() {\n"
            "  for (let i = 0; i < 200; ++i) {\n"
            "    const players = bro.mpris.getPlayers();\n"
            "    const active = bro.mpris.getActivePlayer();\n"
            "    const meta = bro.mpris.getMetadata();\n"
            "    const cb = (e) => {};\n"
            "    bro.mpris.on('playbackStatus', cb);\n"
            "    bro.mpris.off('playbackStatus', cb);\n"
            "    const svc = bro.mpris.createService({ name: 'stress_' + i, identity: 'Stress' });\n"
            "    svc.setPlaybackStatus('Playing');\n"
            "    svc.setMetadata({ title: 'Track ' + i, artist: 'Artist' });\n"
            "    svc.onCommand((c) => {});\n"
            "    svc.close();\n"
            "  }\n"
            "  return true;\n"
            "})()\n"
        );
        REQUIRE(!rStress.thrown && ev::toBool(rStress.value));
        brompris::api::tickMprisAsync();
        brompris::api::shutdownMprisAsync();
        std::cout << "  GC stress loop [PASS]" << std::endl;
    }

    std::cout << "All brompris JavaScript API tests PASSED!" << std::endl;
    return bstest::finish("test_api");
}

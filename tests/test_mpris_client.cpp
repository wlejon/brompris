#include "check.h"
#include "brompris/mpris.h"
#include "brodbus/bus.h"
#include "brodbus/private_bus.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

using namespace brompris;
using namespace std::chrono_literals;

class TestObserver : public PlayerObserver {
public:
    int status_changes = 0;
    int metadata_changes = 0;
    int seeked_events = 0;
    int volume_changes = 0;
    PlaybackStatus last_status = PlaybackStatus::Stopped;
    MediaMetadata last_metadata;
    int64_t last_seek_pos = 0;
    double last_volume = 0.0;

    void on_playback_status_changed(MediaPlayer&, PlaybackStatus status) override {
        ++status_changes;
        last_status = status;
    }
    void on_metadata_changed(MediaPlayer&, const MediaMetadata& meta) override {
        ++metadata_changes;
        last_metadata = meta;
    }
    void on_position_seeked(MediaPlayer&, int64_t pos) override {
        ++seeked_events;
        last_seek_pos = pos;
    }
    void on_volume_changed(MediaPlayer&, double vol) override {
        ++volume_changes;
        last_volume = vol;
    }
};

void test_mpris_integration() {
    brodbus::PrivateBus daemon;
    if (!daemon.is_valid()) {
        bstest::skip("test_mpris_client", "Private D-Bus daemon could not be started");
    }

    std::string err;
    auto server_bus = brodbus::Bus::open_address(daemon.address(), &err);
    REQUIRE(server_bus);
    REQUIRE(server_bus->is_valid());

    const std::string service_name = "org.mpris.MediaPlayer2.brotest";
    REQUIRE(server_bus->request_name(service_name, 0, &err));

    // Server-side method tracking
    std::mutex server_mutex;
    int play_calls = 0;
    int pause_calls = 0;
    int next_calls = 0;
    int prev_calls = 0;
    int seek_calls = 0;
    int64_t last_seek_offset = 0;
    double server_volume = 1.0;

    // Handle method calls directed to our server
    auto server_slot = server_bus->add_match(
        "type='method_call',destination='" + service_name + "'",
        [&](brodbus::Message& msg) {
            std::string member = msg.get_member();
            std::string iface = msg.get_interface();

            if (member == "Play") {
                ++play_calls;
            } else if (member == "Pause") {
                ++pause_calls;
            } else if (member == "Next") {
                ++next_calls;
            } else if (member == "Previous") {
                ++prev_calls;
            } else if (member == "Seek") {
                ++seek_calls;
                msg.read_int64(&last_seek_offset);
            } else if (member == "Set" && iface == "org.freedesktop.DBus.Properties") {
                std::string target_iface, prop;
                msg.read_string(&target_iface);
                msg.read_string(&prop);
                if (prop == "Volume") {
                    brodbus::PropertyValue val;
                    if (msg.read_variant(&val)) {
                        server_volume = val.as_double();
                    }
                }
            } else if (member == "GetAll") {
                auto rep = msg.new_method_return();
                rep.open_container('a', "{sv}");
                rep.close_container();
                server_bus->send(rep);
                return;
            }

            // Reply to method call
            auto rep = msg.new_method_return();
            server_bus->send(rep);
        });

    REQUIRE(server_slot.is_valid());

    // Run server processing in a dedicated background worker thread so synchronous client calls get answered promptly
    std::atomic<bool> server_running{true};
    std::thread server_thread([&] {
        while (server_running) {
            {
                std::lock_guard<std::mutex> lock(server_mutex);
                while (server_bus->process() > 0) {}
            }
            std::this_thread::sleep_for(5ms);
        }
    });

    // Create client manager connected to the private test bus
    auto manager = MediaManager::create_with_address(daemon.address());
    REQUIRE(manager);

    // Initial wait/process to let discovery complete
    bool found_player = bstest::wait_until([&] {
        manager->wait(20000);
        return !manager->active_players().empty();
    }, 2000ms);
    REQUIRE(found_player);

    auto players = manager->active_players();
    REQUIRE_EQ(players.size(), 1u);

    auto player = players[0];
    REQUIRE(player);
    CHECK_EQ(player->id(), service_name);

    TestObserver observer;
    player->add_observer(&observer);

    // 1. Test server emitting PropertiesChanged for PlaybackStatus and Volume
    {
        std::lock_guard<std::mutex> lock(server_mutex);
        bool emit_ok = server_bus->emit_signal(
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            [](brodbus::Message& m) {
                m.append_string("org.mpris.MediaPlayer2.Player");
                m.open_container('a', "{sv}");

                // PlaybackStatus = "Playing"
                m.open_container('e', "sv");
                m.append_string("PlaybackStatus");
                m.append_variant(brodbus::PropertyValue("Playing"));
                m.close_container();

                // Volume = 0.75
                m.open_container('e', "sv");
                m.append_string("Volume");
                m.append_variant(brodbus::PropertyValue(0.75));
                m.close_container();

                m.close_container();  // close 'a'

                // Invalidated properties
                m.open_container('a', "s");
                m.close_container();
            });
        REQUIRE(emit_ok);
    }

    bool received_status = bstest::wait_until([&] {
        manager->wait(20000);
        return observer.status_changes > 0 && observer.volume_changes > 0;
    }, 2000ms);

    CHECK(received_status);
    CHECK_EQ(player->playback_status(), PlaybackStatus::Playing);
    CHECK_DOUBLE_EQ(player->volume(), 0.75, 0.001);
    CHECK_EQ(observer.last_status, PlaybackStatus::Playing);
    CHECK_DOUBLE_EQ(observer.last_volume, 0.75, 0.001);

    // 2. Test server emitting PropertiesChanged with rich Metadata
    {
        std::lock_guard<std::mutex> lock(server_mutex);
        server_bus->emit_signal(
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            [](brodbus::Message& m) {
                m.append_string("org.mpris.MediaPlayer2.Player");
                m.open_container('a', "{sv}");

                m.open_container('e', "sv");
                m.append_string("Metadata");
                m.open_container('v', "a{sv}");
                m.open_container('a', "{sv}");

                m.open_container('e', "sv");
                m.append_string("xesam:title");
                m.append_variant(brodbus::PropertyValue("Echoes"));
                m.close_container();

                m.open_container('e', "sv");
                m.append_string("xesam:artist");
                m.append_variant(brodbus::PropertyValue(std::vector<std::string>{"Pink Floyd"}));
                m.close_container();

                m.open_container('e', "sv");
                m.append_string("mpris:length");
                m.append_variant(brodbus::PropertyValue(int64_t(1400000000LL)));
                m.close_container();

                m.close_container();  // close 'a'
                m.close_container();  // close 'v'
                m.close_container();  // close 'e'

                m.close_container();  // close 'a'
                m.open_container('a', "s");
                m.close_container();
            });
    }

    bool received_meta = bstest::wait_until([&] {
        manager->wait(20000);
        return observer.metadata_changes > 0;
    }, 2000ms);

    CHECK(received_meta);
    CHECK_EQ(player->metadata().title, "Echoes");
    CHECK_EQ(player->metadata().artist, "Pink Floyd");
    CHECK_EQ(player->metadata().duration_usec, 1400000000LL);

    // 3. Test server emitting Seeked signal
    {
        std::lock_guard<std::mutex> lock(server_mutex);
        server_bus->emit_signal(
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            "Seeked",
            [](brodbus::Message& m) {
                m.append_int64(45000000LL);  // 45 seconds
            });
    }

    bool received_seek = bstest::wait_until([&] {
        manager->wait(20000);
        return observer.seeked_events > 0;
    }, 2000ms);

    CHECK(received_seek);
    CHECK_EQ(observer.last_seek_pos, 45000000LL);

    // 4. Test client invoking transport methods on player
    // Pause
    CHECK(player->pause());
    bool got_pause = bstest::wait_until([&] {
        std::lock_guard<std::mutex> lock(server_mutex);
        return pause_calls > 0;
    }, 1000ms);
    CHECK(got_pause);
    CHECK_EQ(pause_calls, 1);

    // Play
    CHECK(player->play());
    bool got_play = bstest::wait_until([&] {
        std::lock_guard<std::mutex> lock(server_mutex);
        return play_calls > 0;
    }, 1000ms);
    CHECK(got_play);
    CHECK_EQ(play_calls, 1);

    // Next & Previous
    CHECK(player->next());
    CHECK(player->previous());
    bool got_nav = bstest::wait_until([&] {
        std::lock_guard<std::mutex> lock(server_mutex);
        return next_calls > 0 && prev_calls > 0;
    }, 1000ms);
    CHECK(got_nav);

    // Seek offset
    CHECK(player->seek(5000000LL));
    bool got_seek = bstest::wait_until([&] {
        std::lock_guard<std::mutex> lock(server_mutex);
        return seek_calls > 0;
    }, 1000ms);
    CHECK(got_seek);
    CHECK_EQ(last_seek_offset, 5000000LL);

    // Set Volume
    CHECK(player->set_volume(0.42));
    bool got_vol = bstest::wait_until([&] {
        std::lock_guard<std::mutex> lock(server_mutex);
        return std::abs(server_volume - 0.42) < 0.001;
    }, 1000ms);
    CHECK(got_vol);
    CHECK_DOUBLE_EQ(server_volume, 0.42, 0.001);

    player->remove_observer(&observer);

    server_running = false;
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

int main() {
    test_mpris_integration();
    return bstest::finish("test_mpris_client");
}

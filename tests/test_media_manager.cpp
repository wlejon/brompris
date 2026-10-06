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

class TestManagerObserver : public ManagerObserver {
public:
    int added_count = 0;
    int removed_count = 0;
    int active_changed_count = 0;
    std::string last_added_id;
    std::string last_removed_id;
    std::string last_active_id;

    void on_player_added(std::shared_ptr<MediaPlayer> player) override {
        ++added_count;
        if (player) last_added_id = player->id();
    }
    void on_player_removed(const std::string& player_id) override {
        ++removed_count;
        last_removed_id = player_id;
    }
    void on_active_player_changed(std::shared_ptr<MediaPlayer> player) override {
        ++active_changed_count;
        if (player) last_active_id = player->id();
    }
};

void test_manager_multi_player() {
    brodbus::PrivateBus daemon;
    if (!daemon.is_valid()) {
        bstest::skip("test_media_manager", "Private D-Bus daemon could not be started");
    }

    std::string err;
    auto server1 = brodbus::Bus::open_address(daemon.address(), &err);
    REQUIRE(server1);
    const std::string p1_name = "org.mpris.MediaPlayer2.player1";
    REQUIRE(server1->request_name(p1_name, 0, &err));

    auto server2 = brodbus::Bus::open_address(daemon.address(), &err);
    REQUIRE(server2);
    const std::string p2_name = "org.mpris.MediaPlayer2.player2";
    REQUIRE(server2->request_name(p2_name, 0, &err));

    auto slot1 = server1->add_match(
        "type='method_call',destination='" + p1_name + "'",
        [&](brodbus::Message& msg) {
            auto rep = msg.new_method_return();
            if (msg.get_member() == "GetAll") {
                rep.open_container('a', "{sv}");
                rep.close_container();
            }
            server1->send(rep);
        });

    auto slot2 = server2->add_match(
        "type='method_call',destination='" + p2_name + "'",
        [&](brodbus::Message& msg) {
            auto rep = msg.new_method_return();
            if (msg.get_member() == "GetAll") {
                rep.open_container('a', "{sv}");
                rep.close_container();
            }
            server2->send(rep);
        });

    std::mutex servers_mutex;
    std::atomic<bool> servers_running{true};
    std::thread servers_thread([&] {
        while (servers_running) {
            {
                std::lock_guard<std::mutex> lock(servers_mutex);
                while (server1->process() > 0) {}
                while (server2->process() > 0) {}
            }
            std::this_thread::sleep_for(5ms);
        }
    });

    auto manager = MediaManager::create_with_address(daemon.address());
    REQUIRE(manager);

    TestManagerObserver obs;
    manager->add_observer(&obs);

    bool discovered_both = bstest::wait_until([&] {
        manager->wait(20000);
        return manager->active_players().size() == 2;
    }, 2000ms);
    REQUIRE(discovered_both);

    auto list = manager->active_players();
    REQUIRE_EQ(list.size(), 2u);

    auto p1 = manager->find_player(p1_name);
    auto p2 = manager->find_player(p2_name);
    REQUIRE(p1);
    REQUIRE(p2);
    CHECK_EQ(p1->id(), p1_name);
    CHECK_EQ(p2->id(), p2_name);

    // Make player2 "Playing"
    {
        std::lock_guard<std::mutex> lock(servers_mutex);
        server2->emit_signal(
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            [](brodbus::Message& m) {
                m.append_string("org.mpris.MediaPlayer2.Player");
                m.open_container('a', "{sv}");
                m.open_container('e', "sv");
                m.append_string("PlaybackStatus");
                m.append_variant(brodbus::PropertyValue("Playing"));
                m.close_container();
                m.close_container();
                m.open_container('a', "s");
                m.close_container();
            });
    }

    bool switched_to_p2 = bstest::wait_until([&] {
        manager->wait(20000);
        auto act = manager->active_player();
        return act && act->id() == p2_name;
    }, 2000ms);

    CHECK(switched_to_p2);
    auto active = manager->active_player();
    REQUIRE(active);
    CHECK_EQ(active->id(), p2_name);
    CHECK_EQ(active->playback_status(), PlaybackStatus::Playing);

    // Now release player1 name and verify manager detects removal
    {
        std::lock_guard<std::mutex> lock(servers_mutex);
        REQUIRE(server1->release_name(p1_name, &err));
    }

    bool detected_removal = bstest::wait_until([&] {
        manager->wait(20000);
        return manager->active_players().size() == 1;
    }, 2000ms);

    CHECK(detected_removal);
    CHECK_EQ(obs.removed_count, 1);
    CHECK_EQ(obs.last_removed_id, p1_name);
    CHECK(manager->find_player(p1_name) == nullptr);
    CHECK(manager->find_player(p2_name) != nullptr);

    manager->remove_observer(&obs);

    servers_running = false;
    if (servers_thread.joinable()) {
        servers_thread.join();
    }
}

int main() {
    test_manager_multi_player();
    return bstest::finish("test_media_manager");
}

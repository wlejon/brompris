#include "api.h"
#include "host_mpris_internal.h"
#include "object_builder.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace brompris::api {

namespace {

class ApiObserver : public ManagerObserver, public PlayerObserver {
public:
    void attach_to_manager(MediaManager* mgr) {
        if (!mgr) return;
        mgr->add_observer(this);
        for (const auto& p : mgr->active_players()) {
            if (p) attach_to_player(p.get());
        }
        auto act = mgr->active_player();
        if (act) attach_to_player(act.get());
    }

    void detach_from_manager(MediaManager* mgr) {
        if (!mgr) return;
        mgr->remove_observer(this);
        detach_all_players();
    }

    void attach_to_player(MediaPlayer* player) {
        if (!player) return;
        std::lock_guard lock(players_mu_);
        if (std::find(observed_players_.begin(), observed_players_.end(), player) == observed_players_.end()) {
            player->add_observer(this);
            observed_players_.push_back(player);
        }
    }

    void detach_player(MediaPlayer* player) {
        if (!player) return;
        std::lock_guard lock(players_mu_);
        auto it = std::remove(observed_players_.begin(), observed_players_.end(), player);
        if (it != observed_players_.end()) {
            player->remove_observer(this);
            observed_players_.erase(it, observed_players_.end());
        }
    }

    void detach_all_players() {
        std::lock_guard lock(players_mu_);
        for (auto* p : observed_players_) {
            if (p) {
                p->remove_observer(this);
            }
        }
        observed_players_.clear();
    }

    // ManagerObserver overrides
    void on_player_added(std::shared_ptr<MediaPlayer> player) override {
        if (!player) return;
        attach_to_player(player.get());
        PlayerAddedEvent ev;
        ev.id = player->id();
        ev.identity = player->identity();
        ev.playback_status = player->playback_status();
        ev.capabilities = player->capabilities();
        ev.volume = player->volume();
        ev.position_usec = player->position_usec();
        queueMprisEvent(std::move(ev));
    }

    void on_player_removed(const std::string& player_id) override {
        PlayerRemovedEvent ev;
        ev.id = player_id;
        queueMprisEvent(std::move(ev));
    }

    void on_active_player_changed(std::shared_ptr<MediaPlayer> player) override {
        ActivePlayerChangedEvent ev;
        if (player) {
            attach_to_player(player.get());
            ev.has_player = true;
            ev.id = player->id();
            ev.identity = player->identity();
            ev.playback_status = player->playback_status();
            ev.capabilities = player->capabilities();
            ev.volume = player->volume();
            ev.position_usec = player->position_usec();
        } else {
            ev.has_player = false;
        }
        queueMprisEvent(std::move(ev));
    }

    // PlayerObserver overrides
    void on_playback_status_changed(MediaPlayer& player, PlaybackStatus status) override {
        PlaybackStatusEvent ev;
        ev.player_id = player.id();
        ev.status = status;
        queueMprisEvent(std::move(ev));
    }

    void on_metadata_changed(MediaPlayer& player, const MediaMetadata& metadata) override {
        MetadataEvent ev;
        ev.player_id = player.id();
        ev.metadata = metadata;
        queueMprisEvent(std::move(ev));
    }

    void on_position_seeked(MediaPlayer& player, int64_t position_usec) override {
        SeekedEvent ev;
        ev.player_id = player.id();
        ev.position_usec = position_usec;
        queueMprisEvent(std::move(ev));
    }

    void on_player_closed(MediaPlayer& player) override {
        detach_player(&player);
    }

private:
    std::mutex players_mu_;
    std::vector<MediaPlayer*> observed_players_;
};

std::mutex g_services_mu;
std::shared_ptr<MediaManager> g_custom_manager;
std::shared_ptr<MediaManager> g_default_manager;
bool g_observer_attached = false;
ApiObserver g_observer;

std::mutex g_events_mu;
std::vector<MprisEventVariant> g_pending_events;

} // namespace

std::shared_ptr<MediaManager> activeManager() {
    std::lock_guard lock(g_services_mu);
    if (g_custom_manager) {
        if (!g_observer_attached) {
            g_observer.attach_to_manager(g_custom_manager.get());
            g_observer_attached = true;
        }
        return g_custom_manager;
    }
    if (!g_default_manager) {
        g_default_manager = MediaManager::create();
        if (g_default_manager) {
            g_observer.attach_to_manager(g_default_manager.get());
            g_observer_attached = true;
        }
    }
    return g_default_manager;
}

void setMediaManager(std::shared_ptr<MediaManager> mgr) {
    std::lock_guard lock(g_services_mu);
    if (g_observer_attached) {
        auto old = g_custom_manager ? g_custom_manager : g_default_manager;
        if (old) {
            g_observer.detach_from_manager(old.get());
        }
        g_observer_attached = false;
    }
    g_custom_manager = std::move(mgr);
    if (g_custom_manager) {
        g_observer.attach_to_manager(g_custom_manager.get());
        g_observer_attached = true;
    }
}

std::shared_ptr<MediaManager> getMediaManager() {
    return activeManager();
}

void queueMprisEvent(MprisEventVariant evItem) {
    std::lock_guard lock(g_events_mu);
    g_pending_events.push_back(std::move(evItem));
}

void clearPendingEvents() {
    std::lock_guard lock(g_events_mu);
    g_pending_events.clear();
}

Value ensureBroMpris() {
    ev::Persistent globalThisVal;
    auto gt = ev::globalValue("globalThis");
    if (gt.found && ev::isObject(gt.value)) {
        globalThisVal.set(gt.value);
    }

    ev::Persistent broP;
    auto bro = ev::globalValue("bro");
    if (bro.found && ev::isObject(bro.value)) broP.set(bro.value);
    if (!ev::isObject(broP.get()) && ev::isObject(globalThisVal.get())) {
        Value candidate = ev::getProperty(globalThisVal.get(), "bro");
        if (ev::isObject(candidate)) broP.set(candidate);
    }
    if (!ev::isObject(broP.get())) {
        broP.set(ev::createObject());
        ev::registerGlobal("bro", broP.get());
        if (ev::isObject(globalThisVal.get())) {
            globalThisVal.set(ev::setProperty(globalThisVal.get(), "bro", broP.get()));
        }
    }

    ev::Persistent mprisP(ev::getProperty(broP.get(), "mpris"));
    if (!ev::isObject(mprisP.get())) {
        mprisP.set(ev::createObject());
        broP.set(ev::setProperty(broP.get(), "mpris", mprisP.get()));
    }
    return mprisP.get();
}

void installMpris() {
    ev::Persistent mprisObj(ensureBroMpris());
    installNativeMpris(mprisObj.get());
    // Eagerly resolve manager so observers attach early if manager exists
    activeManager();
}

void drainMprisEvents() {
    std::vector<MprisEventVariant> events;
    {
        std::lock_guard lock(g_events_mu);
        events.swap(g_pending_events);
    }

    auto mgr = activeManager();

    for (const auto& evVariant : events) {
        std::visit([&](const auto& event) {
            using T = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<T, PlayerAddedEvent>) {
                ObjectBuilder b;
                b.set("type", "playerAdded");
                b.set("id", event.id);
                b.set("identity", event.identity);
                b.set("playbackStatus", std::string(to_string(event.playback_status)));
                b.set("canControl", event.capabilities.can_control);
                b.set("canPlay", event.capabilities.can_play);
                b.set("canPause", event.capabilities.can_pause);
                b.set("canGoNext", event.capabilities.can_go_next);
                b.set("canGoPrevious", event.capabilities.can_go_previous);
                b.set("canSeek", event.capabilities.can_seek);
                b.set("volume", event.volume);
                b.set("position", static_cast<double>(event.position_usec));

                std::shared_ptr<MediaPlayer> p = mgr ? mgr->find_player(event.id) : nullptr;
                Value pObj = p ? buildPlayerObject(*p)
                               : buildPlayerObjectFromSnapshot(event.id, event.identity,
                                                              event.playback_status,
                                                              event.capabilities,
                                                              event.volume,
                                                              event.position_usec);
                b.set("player", pObj);
                dispatchMprisEvent("playerAdded", b.build());
            } else if constexpr (std::is_same_v<T, PlayerRemovedEvent>) {
                ObjectBuilder b;
                b.set("type", "playerRemoved");
                b.set("id", event.id);
                b.set("playerId", event.id);
                dispatchMprisEvent("playerRemoved", b.build());
            } else if constexpr (std::is_same_v<T, ActivePlayerChangedEvent>) {
                ObjectBuilder b;
                b.set("type", "activePlayerChanged");
                if (event.has_player) {
                    b.set("id", event.id);
                    b.set("identity", event.identity);
                    b.set("playbackStatus", std::string(to_string(event.playback_status)));
                    b.set("canControl", event.capabilities.can_control);
                    b.set("canPlay", event.capabilities.can_play);
                    b.set("canPause", event.capabilities.can_pause);
                    b.set("canGoNext", event.capabilities.can_go_next);
                    b.set("canGoPrevious", event.capabilities.can_go_previous);
                    b.set("canSeek", event.capabilities.can_seek);
                    b.set("volume", event.volume);
                    b.set("position", static_cast<double>(event.position_usec));

                    std::shared_ptr<MediaPlayer> p = mgr ? mgr->find_player(event.id) : nullptr;
                    Value pObj = p ? buildPlayerObject(*p)
                                   : buildPlayerObjectFromSnapshot(event.id, event.identity,
                                                                  event.playback_status,
                                                                  event.capabilities,
                                                                  event.volume,
                                                                  event.position_usec);
                    b.set("player", pObj);
                } else {
                    b.set("id", "");
                    b.set("player", ev::null());
                }
                dispatchMprisEvent("activePlayerChanged", b.build());
            } else if constexpr (std::is_same_v<T, PlaybackStatusEvent>) {
                ObjectBuilder b;
                b.set("type", "playbackStatus");
                b.set("id", event.player_id);
                b.set("playerId", event.player_id);
                std::string st = std::string(to_string(event.status));
                b.set("playbackStatus", st);
                b.set("status", st);
                dispatchMprisEvent("playbackStatus", b.build());
            } else if constexpr (std::is_same_v<T, MetadataEvent>) {
                ObjectBuilder b;
                b.set("type", "metadata");
                b.set("id", event.player_id);
                b.set("playerId", event.player_id);
                b.set("title", event.metadata.title);
                b.set("artist", event.metadata.artist);
                b.set("album", event.metadata.album);
                b.set("albumArtUrl", event.metadata.album_art_url);
                b.set("duration", static_cast<double>(event.metadata.duration_usec));
                b.set("trackId", event.metadata.track_id);
                b.set("metadata", buildMetadataObject(event.metadata));
                dispatchMprisEvent("metadata", b.build());
            } else if constexpr (std::is_same_v<T, SeekedEvent>) {
                ObjectBuilder b;
                b.set("type", "seeked");
                b.set("id", event.player_id);
                b.set("playerId", event.player_id);
                b.set("position", static_cast<double>(event.position_usec));
                b.set("positionUsec", static_cast<double>(event.position_usec));
                dispatchMprisEvent("seeked", b.build());
            }
        }, evVariant);
    }

    ev::drainMicrotasks();
}

void tickMprisAsync() {
    auto mgr = activeManager();
    if (mgr) {
        mgr->process();
    }
    drainMprisEvents();
}

void shutdownMprisAsync() {
    {
        std::lock_guard lock(g_services_mu);
        if (g_observer_attached) {
            auto cur = g_custom_manager ? g_custom_manager : g_default_manager;
            if (cur) {
                g_observer.detach_from_manager(cur.get());
            }
            g_observer_attached = false;
        }
    }
    clearSubscriptions();
    clearPendingEvents();
}

} // namespace brompris::api

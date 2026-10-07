#include "host_mpris_internal.h"
#include "arg_reader.h"
#include "object_builder.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <vector>

namespace brompris::api {

namespace {

struct Subscription {
    std::string event;
    std::shared_ptr<ev::Persistent> callback;
};

std::mutex g_sub_mu;
std::vector<Subscription> g_subscriptions;

std::shared_ptr<MediaPlayer> resolveControlTarget(const ArgReader& reader, size_t idIdx = 0) {
    auto mgr = activeManager();
    if (!mgr) return nullptr;
    if (reader.has(idIdx) && reader.isString(idIdx)) {
        std::string id = reader.getString(idIdx);
        if (!id.empty()) {
            return mgr->find_player(id);
        }
    }
    if (!reader.has(idIdx)) {
        return mgr->active_player();
    }
    return nullptr;
}

} // namespace

Value buildPlayerObjectFromSnapshot(const std::string& id,
                                   const std::string& identity,
                                   PlaybackStatus status,
                                   const PlayerCapabilities& caps,
                                   double volume,
                                   int64_t position_usec) {
    ObjectBuilder b;
    b.set("id", id);
    b.set("identity", identity);
    b.set("playbackStatus", std::string(to_string(status)));
    b.set("canControl", caps.can_control);
    b.set("canPlay", caps.can_play);
    b.set("canPause", caps.can_pause);
    b.set("canGoNext", caps.can_go_next);
    b.set("canGoPrevious", caps.can_go_previous);
    b.set("canSeek", caps.can_seek);
    b.set("volume", volume);
    b.set("position", static_cast<double>(position_usec));
    return b.build();
}

Value buildPlayerObject(const MediaPlayer& player) {
    return buildPlayerObjectFromSnapshot(player.id(),
                                        player.identity(),
                                        player.playback_status(),
                                        player.capabilities(),
                                        player.volume(),
                                        player.position_usec());
}

Value buildMetadataObject(const MediaMetadata& meta) {
    ObjectBuilder b;
    b.set("title", meta.title);
    b.set("artist", meta.artist);
    b.set("album", meta.album);
    b.set("albumArtUrl", meta.album_art_url);
    b.set("duration", static_cast<double>(meta.duration_usec));
    b.set("trackId", meta.track_id);
    return b.build();
}

void addMprisEventListener(const std::string& event, Value callback) {
    if (!ev::isFunction(callback)) return;
    std::lock_guard lock(g_sub_mu);
    Subscription sub;
    sub.event = event;
    sub.callback = std::make_shared<ev::Persistent>(callback);
    g_subscriptions.push_back(std::move(sub));
}

void removeMprisEventListener(const std::string& event, Value callback) {
    std::lock_guard lock(g_sub_mu);
    if (!ev::isFunction(callback)) {
        std::erase_if(g_subscriptions, [&](const Subscription& sub) {
            return sub.event == event;
        });
        return;
    }
    std::erase_if(g_subscriptions, [&](const Subscription& sub) {
        if (sub.event != event) return false;
        return sub.callback && sub.callback->get() == callback;
    });
}

void clearSubscriptions() {
    std::lock_guard lock(g_sub_mu);
    g_subscriptions.clear();
}

void dispatchMprisEvent(std::string_view event, Value payload) {
    ev::Persistent payloadRoot(payload);

    std::vector<std::shared_ptr<ev::Persistent>> targets;
    {
        std::lock_guard lock(g_sub_mu);
        for (const auto& sub : g_subscriptions) {
            if (sub.event == event || sub.event == "*" || sub.event == "change") {
                if (sub.callback) {
                    targets.push_back(sub.callback);
                }
            }
        }
    }

    for (const auto& cb : targets) {
        if (!cb || !ev::isFunction(cb->get())) continue;
        const Value arg = payloadRoot.get();
        ev::catchThrow([&]() {
            return ev::call(cb->get(), ev::undefined(), std::span<const Value>(&arg, 1)).value;
        });
    }

    // Check optional property listener on bro.mpris (e.g. onplayerAdded, onplaybackstatus)
    ev::Persistent mprisObj(ensureBroMpris());
    if (ev::isObject(mprisObj.get())) {
        std::string propName = "on" + std::string(event);
        ev::Persistent handler(ev::getProperty(mprisObj.get(), propName));
        if (!ev::isFunction(handler.get())) {
            std::string lowerEvent;
            lowerEvent.reserve(event.size());
            for (char c : event) {
                lowerEvent.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            std::string propNameLower = "on" + lowerEvent;
            if (propNameLower != propName) {
                handler.set(ev::getProperty(mprisObj.get(), propNameLower));
            }
        }
        if (ev::isFunction(handler.get())) {
            const Value arg = payloadRoot.get();
            ev::catchThrow([&]() {
                return ev::call(handler.get(), mprisObj.get(), std::span<const Value>(&arg, 1)).value;
            });
        }
    }
}

void installNativeMpris(Value mprisVal) {
    ObjectBuilder mpris(mprisVal);

    // bro.mpris.getPlayers() -> array of player objects
    mpris.def("getPlayers", 0, [](Value, std::span<const Value>) -> Value {
        auto mgr = activeManager();
        auto players = mgr ? mgr->active_players() : std::vector<std::shared_ptr<MediaPlayer>>{};
        ev::Persistent arr(ev::makeArray(static_cast<uint32_t>(players.size())));
        for (uint32_t i = 0; i < players.size(); ++i) {
            if (players[i]) {
                ev::Persistent pVal(buildPlayerObject(*players[i]));
                ev::setElement(arr.get(), i, pVal.get());
            }
        }
        return arr.get();
    });

    // bro.mpris.getActivePlayer() -> player object or null
    mpris.def("getActivePlayer", 0, [](Value, std::span<const Value>) -> Value {
        auto mgr = activeManager();
        auto p = mgr ? mgr->active_player() : nullptr;
        if (!p) return ev::null();
        return buildPlayerObject(*p);
    });

    // bro.mpris.findPlayer(id) -> player object or null
    mpris.def("findPlayer", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (!reader.has(0) || !reader.isString(0)) return ev::null();
        std::string id = reader.getString(0);
        if (id.empty()) return ev::null();
        auto mgr = activeManager();
        auto p = mgr ? mgr->find_player(id) : nullptr;
        if (!p) return ev::null();
        return buildPlayerObject(*p);
    });

    // bro.mpris.getMetadata(id?) -> metadata object or null
    mpris.def("getMetadata", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto mgr = activeManager();
        if (!mgr) return ev::null();
        std::shared_ptr<MediaPlayer> p;
        if (reader.has(0) && reader.isString(0)) {
            std::string id = reader.getString(0);
            if (!id.empty()) {
                p = mgr->find_player(id);
            }
        }
        if (!p && (!reader.has(0) || reader.getString(0).empty())) {
            p = mgr->active_player();
        }
        if (!p) return ev::null();
        return buildMetadataObject(p->metadata());
    });

    // Control commands
    mpris.def("play", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto p = resolveControlTarget(reader, 0);
        return ev::fromBool(p ? p->play() : false);
    });

    mpris.def("pause", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto p = resolveControlTarget(reader, 0);
        return ev::fromBool(p ? p->pause() : false);
    });

    mpris.def("playPause", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto p = resolveControlTarget(reader, 0);
        return ev::fromBool(p ? p->play_pause() : false);
    });

    mpris.def("stop", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto p = resolveControlTarget(reader, 0);
        return ev::fromBool(p ? p->stop() : false);
    });

    mpris.def("next", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto p = resolveControlTarget(reader, 0);
        return ev::fromBool(p ? p->next() : false);
    });

    mpris.def("previous", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto p = resolveControlTarget(reader, 0);
        return ev::fromBool(p ? p->previous() : false);
    });

    mpris.def("seek", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        std::shared_ptr<MediaPlayer> p;
        int64_t offset = 0;
        auto mgr = activeManager();
        if (reader.count() >= 2 && reader.isString(0)) {
            p = mgr ? mgr->find_player(reader.getString(0)) : nullptr;
            offset = reader.getInt64(1);
        } else if (reader.count() >= 1 && reader.isNumber(0)) {
            p = mgr ? mgr->active_player() : nullptr;
            offset = reader.getInt64(0);
        } else {
            return ev::fromBool(false);
        }
        return ev::fromBool(p ? p->seek(offset) : false);
    });

    mpris.def("setPosition", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        std::shared_ptr<MediaPlayer> p;
        std::string trackId;
        int64_t pos = 0;
        auto mgr = activeManager();
        if (reader.count() >= 3 && reader.isString(0) && reader.isString(1)) {
            p = mgr ? mgr->find_player(reader.getString(0)) : nullptr;
            trackId = reader.getString(1);
            pos = reader.getInt64(2);
        } else if (reader.count() >= 2 && reader.isString(0)) {
            p = mgr ? mgr->find_player(reader.getString(0)) : nullptr;
            if (p) trackId = p->metadata().track_id;
            pos = reader.getInt64(1);
        } else if (reader.count() >= 1 && reader.isNumber(0)) {
            p = mgr ? mgr->active_player() : nullptr;
            if (p) trackId = p->metadata().track_id;
            pos = reader.getInt64(0);
        } else {
            return ev::fromBool(false);
        }
        return ev::fromBool(p ? p->set_position(trackId, pos) : false);
    });

    mpris.def("setVolume", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        std::shared_ptr<MediaPlayer> p;
        double vol = 1.0;
        auto mgr = activeManager();
        if (reader.count() >= 2 && reader.isString(0)) {
            p = mgr ? mgr->find_player(reader.getString(0)) : nullptr;
            vol = reader.getDouble(1);
        } else if (reader.count() >= 1 && reader.isNumber(0)) {
            p = mgr ? mgr->active_player() : nullptr;
            vol = reader.getDouble(0);
        } else {
            return ev::fromBool(false);
        }
        return ev::fromBool(p ? p->set_volume(vol) : false);
    });

    // Event listener registration
    mpris.def("on", 2, [](Value self, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.count() >= 2 && reader.isString(0) && reader.isFunction(1)) {
            addMprisEventListener(reader.getString(0), reader.get(1));
        }
        return self;
    });

    mpris.def("off", 2, [](Value self, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.count() >= 1 && reader.isString(0)) {
            removeMprisEventListener(reader.getString(0),
                                     reader.count() >= 2 ? reader.get(1) : ev::undefined());
        }
        return self;
    });

    mpris.def("addEventListener", 2, [](Value self, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.count() >= 2 && reader.isString(0) && reader.isFunction(1)) {
            addMprisEventListener(reader.getString(0), reader.get(1));
        }
        return self;
    });

    mpris.def("removeEventListener", 2, [](Value self, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.count() >= 1 && reader.isString(0)) {
            removeMprisEventListener(reader.getString(0),
                                     reader.count() >= 2 ? reader.get(1) : ev::undefined());
        }
        return self;
    });
}

} // namespace brompris::api

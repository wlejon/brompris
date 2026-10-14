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

Value buildCapabilitiesObject(const PlayerCapabilities& caps) {
    ObjectBuilder b;
    b.set("canControl", caps.can_control);
    b.set("canPlay", caps.can_play);
    b.set("canPause", caps.can_pause);
    b.set("canSeek", caps.can_seek);
    b.set("canGoNext", caps.can_go_next);
    b.set("canGoPrevious", caps.can_go_previous);
    b.set("canRaise", caps.can_raise);
    b.set("canQuit", caps.can_quit);
    b.set("canSetFullscreen", caps.can_set_fullscreen);
    return b.build();
}

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
    b.set("canRaise", caps.can_raise);
    b.set("canQuit", caps.can_quit);
    b.set("volume", volume);
    b.set("position", static_cast<double>(position_usec));
    return b.build();
}

Value buildPlayerObject(const MediaPlayer& player) {
    ObjectBuilder b(buildPlayerObjectFromSnapshot(player.id(),
                                                  player.identity(),
                                                  player.playback_status(),
                                                  player.capabilities(),
                                                  player.volume(),
                                                  player.position_usec()));
    b.set("desktopEntry", player.desktop_entry());
    b.set("loopStatus", std::string(to_string(player.loop_status())));
    b.set("shuffle", shuffle_status_to_bool(player.shuffle_status()));
    b.set("shuffleStatus", std::string(to_string(player.shuffle_status())));
    b.set("rate", player.rate());
    return b.build();
}

namespace {

Value buildStringArray(const std::vector<std::string>& items) {
    ev::Persistent arr(ev::makeArray(static_cast<uint32_t>(items.size())));
    for (uint32_t i = 0; i < items.size(); ++i) {
        ev::Persistent sVal(ev::fromUtf8(items[i]));
        ev::setElement(arr.get(), i, sVal.get());
    }
    return arr.get();
}

std::vector<std::string> readStringArrayOrSingle(Value val) {
    std::vector<std::string> out;
    if (ev::isString(val)) {
        ev::Persistent pinned(val);
        std::string s = ev::toUtf8(pinned.get());
        if (!s.empty()) out.push_back(std::move(s));
        return out;
    }
    if (ev::isObject(val)) {
        ev::Persistent arrP(val);
        Value lenVal = ev::getProperty(arrP.get(), "length");
        if (ev::isNumber(lenVal)) {
            double lenD = ev::toDouble(lenVal);
            if (lenD > 0.0 && lenD < 65536.0) {
                uint32_t len = static_cast<uint32_t>(lenD);
                out.reserve(len);
                for (uint32_t i = 0; i < len; ++i) {
                    Value elem = ev::getElement(arrP.get(), i);
                    if (ev::isString(elem)) {
                        ev::Persistent elemP(elem);
                        out.push_back(ev::toUtf8(elemP.get()));
                    }
                }
            }
        }
    }
    return out;
}

}  // namespace

Value buildMetadataObject(const MediaMetadata& meta) {
    ObjectBuilder b;
    b.set("title", meta.title);
    b.set("artist", meta.artist);
    b.set("artists", buildStringArray(meta.artists));
    b.set("album", meta.album);
    b.set("albumArtist", meta.album_artist);
    b.set("albumArtists", buildStringArray(meta.album_artists));
    b.set("albumArtUrl", meta.album_art_url);
    b.set("artUrl", meta.album_art_url);
    b.set("duration", static_cast<double>(meta.duration_usec));
    b.set("length", static_cast<double>(meta.duration_usec));
    b.set("lengthUs", static_cast<double>(meta.duration_usec));
    b.set("trackId", meta.track_id);
    b.set("trackNumber", static_cast<int64_t>(meta.track_number));
    b.set("discNumber", static_cast<int64_t>(meta.disc_number));
    b.set("genres", buildStringArray(meta.genres));
    b.set("userRating", meta.user_rating);
    b.set("url", meta.url);
    return b.build();
}

MediaMetadata parseMetadataObject(Value obj) {
    MediaMetadata meta;
    if (!ev::isObject(obj)) return meta;
    ev::Persistent root(obj);

    auto getStr = [&](std::string_view key) -> std::string {
        Value v = ev::getProperty(root.get(), key);
        if (ev::isString(v)) {
            ev::Persistent p(v);
            return ev::toUtf8(p.get());
        }
        return "";
    };
    auto getNum = [&](std::string_view key, double* out) -> bool {
        Value v = ev::getProperty(root.get(), key);
        if (ev::isNumber(v)) {
            double d = ev::toDouble(v);
            if (!std::isnan(d)) {
                *out = d;
                return true;
            }
        }
        return false;
    };

    meta.title = getStr("title");
    meta.album = getStr("album");

    Value artistsVal = ev::getProperty(root.get(), "artists");
    if (!ev::isUndefined(artistsVal) && !ev::isNull(artistsVal)) {
        meta.artists = readStringArrayOrSingle(artistsVal);
    }
    Value artistVal = ev::getProperty(root.get(), "artist");
    if (!ev::isUndefined(artistVal) && !ev::isNull(artistVal)) {
        if (ev::isObject(artistVal)) {
            auto list = readStringArrayOrSingle(artistVal);
            if (meta.artists.empty()) meta.artists = list;
            if (!list.empty()) meta.artist = list.front();
        } else if (ev::isString(artistVal)) {
            ev::Persistent p(artistVal);
            meta.artist = ev::toUtf8(p.get());
            if (meta.artists.empty() && !meta.artist.empty()) {
                meta.artists.push_back(meta.artist);
            }
        }
    }
    if (meta.artist.empty() && !meta.artists.empty()) {
        meta.artist = meta.artists.front();
    }

    Value albumArtistsVal = ev::getProperty(root.get(), "albumArtists");
    if (ev::isUndefined(albumArtistsVal)) {
        albumArtistsVal = ev::getProperty(root.get(), "album_artists");
    }
    if (!ev::isUndefined(albumArtistsVal) && !ev::isNull(albumArtistsVal)) {
        meta.album_artists = readStringArrayOrSingle(albumArtistsVal);
    }
    std::string albumArtist = getStr("albumArtist");
    if (albumArtist.empty()) albumArtist = getStr("album_artist");
    if (!albumArtist.empty()) {
        meta.album_artist = albumArtist;
        if (meta.album_artists.empty()) {
            meta.album_artists.push_back(albumArtist);
        }
    } else if (!meta.album_artists.empty()) {
        meta.album_artist = meta.album_artists.front();
    }

    meta.album_art_url = getStr("albumArtUrl");
    if (meta.album_art_url.empty()) meta.album_art_url = getStr("artUrl");
    if (meta.album_art_url.empty()) meta.album_art_url = getStr("art_url");
    if (meta.album_art_url.empty()) meta.album_art_url = getStr("album_art_url");

    double dur = 0.0;
    if (getNum("duration", &dur) || getNum("durationUsec", &dur) ||
        getNum("duration_usec", &dur) || getNum("length", &dur) ||
        getNum("lengthUs", &dur) || getNum("length_us", &dur)) {
        meta.duration_usec = static_cast<int64_t>(dur);
    } else if (getNum("durationMs", &dur) || getNum("lengthMs", &dur)) {
        meta.duration_usec = static_cast<int64_t>(dur * 1000.0);
    } else if (getNum("durationSeconds", &dur) || getNum("duration_seconds", &dur)) {
        meta.duration_usec = static_cast<int64_t>(dur * 1'000'000.0);
    }

    meta.track_id = getStr("trackId");
    if (meta.track_id.empty()) meta.track_id = getStr("track_id");

    double tn = 0.0;
    if (getNum("trackNumber", &tn) || getNum("track_number", &tn) || getNum("track", &tn)) {
        meta.track_number = static_cast<int>(tn);
    }
    double dn = 0.0;
    if (getNum("discNumber", &dn) || getNum("disc_number", &dn) || getNum("disc", &dn)) {
        meta.disc_number = static_cast<int>(dn);
    }
    double ur = 0.0;
    if (getNum("userRating", &ur) || getNum("user_rating", &ur) || getNum("rating", &ur)) {
        meta.user_rating = ur;
    }

    Value genresVal = ev::getProperty(root.get(), "genres");
    if (ev::isUndefined(genresVal)) genresVal = ev::getProperty(root.get(), "genre");
    if (!ev::isUndefined(genresVal) && !ev::isNull(genresVal)) {
        meta.genres = readStringArrayOrSingle(genresVal);
    }

    meta.url = getStr("url");
    if (meta.url.empty()) meta.url = getStr("uri");

    return meta;
}

PlayerCapabilities parseCapabilitiesObject(Value obj, const PlayerCapabilities& defaults) {
    PlayerCapabilities caps = defaults;
    if (!ev::isObject(obj)) return caps;
    ev::Persistent root(obj);

    auto readBoolProp = [&](std::string_view k1, std::string_view k2, bool& target) {
        Value v = ev::getProperty(root.get(), k1);
        if (ev::isUndefined(v) && !k2.empty()) {
            v = ev::getProperty(root.get(), k2);
        }
        if (!ev::isUndefined(v) && !ev::isNull(v)) {
            target = ev::toBool(v);
        }
    };

    readBoolProp("canControl", "can_control", caps.can_control);
    readBoolProp("canPlay", "can_play", caps.can_play);
    readBoolProp("canPause", "can_pause", caps.can_pause);
    readBoolProp("canSeek", "can_seek", caps.can_seek);
    readBoolProp("canGoNext", "can_go_next", caps.can_go_next);
    readBoolProp("canGoPrevious", "can_go_previous", caps.can_go_previous);
    readBoolProp("canRaise", "can_raise", caps.can_raise);
    readBoolProp("canQuit", "can_quit", caps.can_quit);
    readBoolProp("canSetFullscreen", "can_set_fullscreen", caps.can_set_fullscreen);

    return caps;
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

    mpris.def("setLoopStatus", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        std::shared_ptr<MediaPlayer> p;
        std::string statusStr;
        auto mgr = activeManager();
        if (reader.count() >= 2 && reader.isString(0) && reader.isString(1)) {
            p = mgr ? mgr->find_player(reader.getString(0)) : nullptr;
            statusStr = reader.getString(1);
        } else if (reader.count() >= 1 && reader.isString(0)) {
            p = mgr ? mgr->active_player() : nullptr;
            statusStr = reader.getString(0);
        } else {
            return ev::fromBool(false);
        }
        return ev::fromBool(p ? p->set_loop_status(loop_status_from_string(statusStr)) : false);
    });

    auto setShuffleFn = [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        std::shared_ptr<MediaPlayer> p;
        ShuffleStatus shuf = ShuffleStatus::Off;
        auto mgr = activeManager();
        if (reader.count() >= 2 && reader.isString(0)) {
            p = mgr ? mgr->find_player(reader.getString(0)) : nullptr;
            shuf = reader.isString(1) ? shuffle_status_from_string(reader.getString(1))
                                      : shuffle_status_from_bool(reader.getBool(1));
        } else if (reader.count() >= 1) {
            p = mgr ? mgr->active_player() : nullptr;
            shuf = reader.isString(0) ? shuffle_status_from_string(reader.getString(0))
                                      : shuffle_status_from_bool(reader.getBool(0));
        } else {
            return ev::fromBool(false);
        }
        return ev::fromBool(p ? p->set_shuffle_status(shuf) : false);
    };
    mpris.def("setShuffle", 2, setShuffleFn);
    mpris.def("setShuffleStatus", 2, setShuffleFn);

    mpris.def("setRate", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        std::shared_ptr<MediaPlayer> p;
        double rate = 1.0;
        auto mgr = activeManager();
        if (reader.count() >= 2 && reader.isString(0)) {
            p = mgr ? mgr->find_player(reader.getString(0)) : nullptr;
            rate = reader.getDouble(1, 1.0);
        } else if (reader.count() >= 1 && reader.isNumber(0)) {
            p = mgr ? mgr->active_player() : nullptr;
            rate = reader.getDouble(0, 1.0);
        } else {
            return ev::fromBool(false);
        }
        return ev::fromBool(p ? p->set_rate(rate) : false);
    });

    mpris.def("openUri", 2, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        std::shared_ptr<MediaPlayer> p;
        std::string uri;
        auto mgr = activeManager();
        if (reader.count() >= 2 && reader.isString(0) && reader.isString(1)) {
            p = mgr ? mgr->find_player(reader.getString(0)) : nullptr;
            uri = reader.getString(1);
        } else if (reader.count() >= 1 && reader.isString(0)) {
            p = mgr ? mgr->active_player() : nullptr;
            uri = reader.getString(0);
        } else {
            return ev::fromBool(false);
        }
        return ev::fromBool(p ? p->open_uri(uri) : false);
    });

    mpris.def("raise", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto p = resolveControlTarget(reader, 0);
        return ev::fromBool(p ? p->raise() : false);
    });

    mpris.def("quit", 1, [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        auto p = resolveControlTarget(reader, 0);
        return ev::fromBool(p ? p->quit() : false);
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

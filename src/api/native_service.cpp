#include "host_mpris_internal.h"
#include "arg_reader.h"
#include "object_builder.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace brompris::api {

namespace {

struct ServiceSubscription {
    std::string event;
    std::shared_ptr<ev::Persistent> callback;
};

struct ManagedService {
    uint64_t id = 0;
    std::shared_ptr<PlayerService> service;
    std::vector<ServiceSubscription> subscriptions;
};

std::string normalizeCommandEventName(std::string_view raw) {
    std::string lower;
    lower.reserve(raw.size());
    for (char c : raw) {
        if (c != '_' && c != '-') {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "*" || lower == "command" || lower == "all") return "*";
    if (lower == "play") return "play";
    if (lower == "pause") return "pause";
    if (lower == "playpause") return "playPause";
    if (lower == "stop") return "stop";
    if (lower == "next") return "next";
    if (lower == "previous" || lower == "prev") return "previous";
    if (lower == "seek") return "seek";
    if (lower == "setposition" || lower == "position") return "setPosition";
    if (lower == "openuri" || lower == "uri") return "openUri";
    if (lower == "raise") return "raise";
    if (lower == "quit") return "quit";
    if (lower == "volume" || lower == "setvolume" || lower == "volumechange") return "volume";
    if (lower == "loopstatus" || lower == "setloopstatus" || lower == "loop" ||
        lower == "loopstatuschange") {
        return "loopStatus";
    }
    if (lower == "shuffle" || lower == "setshuffle" || lower == "shufflestatus" ||
        lower == "setshufflestatus" || lower == "shufflechange") {
        return "shuffle";
    }
    if (lower == "rate" || lower == "setrate" || lower == "ratechange") return "rate";
    if (lower == "fullscreen" || lower == "setfullscreen") return "fullscreen";
    return std::string(raw);
}

// Declared before any PlayerService storage so the mutex outlives services at exit.
std::mutex g_managed_services_mu;
std::map<uint64_t, std::shared_ptr<ManagedService>> g_managed_services;
std::atomic<uint64_t> g_next_service_id{1};

std::shared_ptr<ManagedService> findManagedService(uint64_t id) {
    std::lock_guard lock(g_managed_services_mu);
    auto it = g_managed_services.find(id);
    return it != g_managed_services.end() ? it->second : nullptr;
}

void addServiceListener(uint64_t id, std::string_view event, Value cb) {
    if (!ev::isFunction(cb)) return;
    std::lock_guard lock(g_managed_services_mu);
    auto it = g_managed_services.find(id);
    if (it == g_managed_services.end() || !it->second) return;
    ServiceSubscription sub;
    sub.event = normalizeCommandEventName(event);
    sub.callback = std::make_shared<ev::Persistent>(cb);
    it->second->subscriptions.push_back(std::move(sub));
}

void removeServiceListener(uint64_t id, std::string_view event, Value cb) {
    std::lock_guard lock(g_managed_services_mu);
    auto it = g_managed_services.find(id);
    if (it == g_managed_services.end() || !it->second) return;
    std::string norm = normalizeCommandEventName(event);
    auto& subs = it->second->subscriptions;
    if (!ev::isFunction(cb)) {
        std::erase_if(subs, [&](const ServiceSubscription& s) { return s.event == norm; });
        return;
    }
    std::erase_if(subs, [&](const ServiceSubscription& s) {
        return s.event == norm && s.callback && s.callback->get() == cb;
    });
}

void closeManagedService(uint64_t id) {
    std::shared_ptr<ManagedService> entry;
    {
        std::lock_guard lock(g_managed_services_mu);
        auto it = g_managed_services.find(id);
        if (it != g_managed_services.end()) {
            entry = std::move(it->second);
            g_managed_services.erase(it);
        }
    }
    if (entry) {
        if (entry->service) {
            entry->service->stop();
        }
        entry->subscriptions.clear();
    }
}

PlayerServiceCallbacks makeServiceCallbacks(uint64_t serviceId) {
    PlayerServiceCallbacks cbs;
    cbs.on_play = [serviceId]() {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "play";
        queueMprisEvent(std::move(ev));
    };
    cbs.on_pause = [serviceId]() {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "pause";
        queueMprisEvent(std::move(ev));
    };
    cbs.on_play_pause = [serviceId]() {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "playPause";
        queueMprisEvent(std::move(ev));
    };
    cbs.on_stop = [serviceId]() {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "stop";
        queueMprisEvent(std::move(ev));
    };
    cbs.on_next = [serviceId]() {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "next";
        queueMprisEvent(std::move(ev));
    };
    cbs.on_previous = [serviceId]() {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "previous";
        queueMprisEvent(std::move(ev));
    };
    cbs.on_seek = [serviceId](int64_t offset_usec) {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "seek";
        ev.offset_usec = offset_usec;
        queueMprisEvent(std::move(ev));
    };
    cbs.on_set_position = [serviceId](const std::string& track_id, int64_t pos_usec) {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "setPosition";
        ev.track_id = track_id;
        ev.position_usec = pos_usec;
        queueMprisEvent(std::move(ev));
    };
    cbs.on_open_uri = [serviceId](const std::string& uri) {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "openUri";
        ev.uri = uri;
        queueMprisEvent(std::move(ev));
    };
    cbs.on_raise = [serviceId]() {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "raise";
        queueMprisEvent(std::move(ev));
    };
    cbs.on_quit = [serviceId]() {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "quit";
        queueMprisEvent(std::move(ev));
    };
    cbs.on_set_loop_status = [serviceId](LoopStatus status) {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "loopStatus";
        ev.loop_status = status;
        queueMprisEvent(std::move(ev));
    };
    cbs.on_set_shuffle = [serviceId](bool shuffle) {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "shuffle";
        ev.shuffle = shuffle;
        queueMprisEvent(std::move(ev));
    };
    cbs.on_set_volume = [serviceId](double volume) {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "volume";
        ev.volume = volume;
        queueMprisEvent(std::move(ev));
    };
    cbs.on_set_rate = [serviceId](double rate) {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "rate";
        ev.rate = rate;
        queueMprisEvent(std::move(ev));
    };
    cbs.on_set_fullscreen = [serviceId](bool fs) {
        ServiceCommandEvent ev;
        ev.service_id = serviceId;
        ev.command = "fullscreen";
        ev.fullscreen = fs;
        queueMprisEvent(std::move(ev));
    };
    return cbs;
}

Value buildServiceHandle(uint64_t serviceId, const std::shared_ptr<PlayerService>& svc) {
    ObjectBuilder b;
    b.set("id", svc->id());
    b.set("busName", svc->bus_name());
    b.set("name", svc->name());
    b.set("identity", svc->identity());
    b.set("desktopEntry", svc->desktop_entry());

    b.def("isRunning", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        return ev::fromBool(m && m->service && m->service->is_running());
    });

    b.def("isConnected", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        return ev::fromBool(m && m->service && m->service->is_connected());
    });

    b.def("setPlaybackStatus", 1, [serviceId](Value self, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0)) return ev::fromBool(false);
        PlaybackStatus st = PlaybackStatus::Stopped;
        if (reader.isString(0)) {
            st = playback_status_from_string(reader.getString(0));
        }
        m->service->set_playback_status(st);
        if (ev::isObject(self)) {
            ObjectBuilder sb(self);
            sb.set("playbackStatus", std::string(to_string(st)));
        }
        return ev::fromBool(true);
    });

    b.def("getPlaybackStatus", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromUtf8("Stopped");
        return ev::fromUtf8(to_string(m->service->playback_status()));
    });

    b.def("setMetadata", 1, [serviceId](Value, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0) || ev::isNull(reader.get(0))) {
            m->service->set_metadata(MediaMetadata{});
            return ev::fromBool(true);
        }
        if (!reader.isObject(0)) return ev::fromBool(false);
        m->service->set_metadata(parseMetadataObject(reader.get(0)));
        return ev::fromBool(true);
    });

    b.def("getMetadata", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::null();
        return buildMetadataObject(m->service->metadata());
    });

    b.def("setPosition", 1, [serviceId](Value, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0)) return ev::fromBool(false);
        m->service->set_position(reader.getInt64(0));
        return ev::fromBool(true);
    });

    b.def("getPosition", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromDouble(0.0);
        return ev::fromDouble(static_cast<double>(m->service->position_usec()));
    });

    b.def("setVolume", 1, [serviceId](Value, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0)) return ev::fromBool(false);
        m->service->set_volume(reader.getDouble(0, 1.0));
        return ev::fromBool(true);
    });

    b.def("getVolume", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromDouble(1.0);
        return ev::fromDouble(m->service->volume());
    });

    b.def("setRate", 1, [serviceId](Value, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0)) return ev::fromBool(false);
        m->service->set_rate(reader.getDouble(0, 1.0));
        return ev::fromBool(true);
    });

    b.def("getRate", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromDouble(1.0);
        return ev::fromDouble(m->service->rate());
    });

    b.def("setLoopStatus", 1, [serviceId](Value, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0) || !reader.isString(0)) return ev::fromBool(false);
        m->service->set_loop_status(loop_status_from_string(reader.getString(0)));
        return ev::fromBool(true);
    });

    b.def("getLoopStatus", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromUtf8("None");
        return ev::fromUtf8(to_string(m->service->loop_status()));
    });

    auto setShuffleFn = [serviceId](Value, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0)) return ev::fromBool(false);
        ShuffleStatus shuf = reader.isString(0)
                                 ? shuffle_status_from_string(reader.getString(0))
                                 : shuffle_status_from_bool(reader.getBool(0));
        m->service->set_shuffle_status(shuf);
        return ev::fromBool(true);
    };
    b.def("setShuffle", 1, setShuffleFn);
    b.def("setShuffleStatus", 1, setShuffleFn);

    b.def("getShuffle", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        return ev::fromBool(m->service->shuffle());
    });

    b.def("setCapabilities", 1, [serviceId](Value, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0) || !reader.isObject(0)) return ev::fromBool(false);
        auto current = m->service->capabilities();
        m->service->set_capabilities(parseCapabilitiesObject(reader.get(0), current));
        return ev::fromBool(true);
    });

    b.def("getCapabilities", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return buildCapabilitiesObject({});
        return buildCapabilitiesObject(m->service->capabilities());
    });

    b.def("setIdentity", 1, [serviceId](Value self, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0) || !reader.isString(0)) return ev::fromBool(false);
        std::string ident = reader.getString(0);
        m->service->set_identity(ident);
        if (ev::isObject(self)) {
            ObjectBuilder sb(self);
            sb.set("identity", ident);
        }
        return ev::fromBool(true);
    });

    b.def("setDesktopEntry", 1, [serviceId](Value self, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        if (!reader.has(0) || !reader.isString(0)) return ev::fromBool(false);
        std::string de = reader.getString(0);
        m->service->set_desktop_entry(de);
        if (ev::isObject(self)) {
            ObjectBuilder sb(self);
            sb.set("desktopEntry", de);
        }
        return ev::fromBool(true);
    });

    auto seekedFn = [serviceId](Value, std::span<const Value> args) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        ArgReader reader(args);
        int64_t pos = reader.has(0) ? reader.getInt64(0) : m->service->position_usec();
        m->service->emit_seeked(pos);
        return ev::fromBool(true);
    };
    b.def("seeked", 1, seekedFn);
    b.def("emitSeeked", 1, seekedFn);

    b.def("onCommand", 1, [serviceId](Value self, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.has(0) && reader.isFunction(0)) {
            addServiceListener(serviceId, "*", reader.get(0));
        }
        return self;
    });

    auto onFn = [serviceId](Value self, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.count() >= 2 && reader.isString(0) && reader.isFunction(1)) {
            addServiceListener(serviceId, reader.getString(0), reader.get(1));
        }
        return self;
    };
    b.def("on", 2, onFn);
    b.def("addEventListener", 2, onFn);

    auto offFn = [serviceId](Value self, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        if (reader.count() >= 1 && reader.isString(0)) {
            removeServiceListener(serviceId,
                                  reader.getString(0),
                                  reader.count() >= 2 ? reader.get(1) : ev::undefined());
        }
        return self;
    };
    b.def("off", 2, offFn);
    b.def("removeEventListener", 2, offFn);

    b.def("start", 0, [serviceId](Value, std::span<const Value>) -> Value {
        auto m = findManagedService(serviceId);
        if (!m || !m->service) return ev::fromBool(false);
        return ev::fromBool(m->service->start());
    });

    auto closeFn = [serviceId](Value, std::span<const Value>) -> Value {
        closeManagedService(serviceId);
        return ev::fromBool(true);
    };
    b.def("close", 0, closeFn);
    b.def("stop", 0, closeFn);
    b.def("destroy", 0, closeFn);

    return b.build();
}

}  // namespace

void dispatchServiceCommand(const ServiceCommandEvent& cmd) {
    std::vector<std::shared_ptr<ev::Persistent>> targets;
    {
        std::lock_guard lock(g_managed_services_mu);
        auto it = g_managed_services.find(cmd.service_id);
        if (it == g_managed_services.end() || !it->second) return;
        for (const auto& sub : it->second->subscriptions) {
            if ((sub.event == cmd.command || sub.event == "*") && sub.callback) {
                targets.push_back(sub.callback);
            }
        }
    }

    if (targets.empty()) return;

    ObjectBuilder b;
    b.set("type", cmd.command);
    b.set("command", cmd.command);

    ev::Persistent arg1(ev::undefined());
    ev::Persistent arg2(ev::undefined());

    if (cmd.command == "seek") {
        double off = static_cast<double>(cmd.offset_usec);
        b.set("offset", off);
        b.set("offsetUsec", off);
        b.set("offsetUs", off);
        b.def("valueOf", 0, [off](Value, std::span<const Value>) -> Value {
            return ev::fromDouble(off);
        });
        arg1.set(ev::fromDouble(off));
    } else if (cmd.command == "setPosition") {
        double pos = static_cast<double>(cmd.position_usec);
        b.set("trackId", cmd.track_id);
        b.set("position", pos);
        b.set("positionUsec", pos);
        b.set("positionUs", pos);
        b.def("valueOf", 0, [pos](Value, std::span<const Value>) -> Value {
            return ev::fromDouble(pos);
        });
        arg1.set(ev::fromDouble(pos));
        arg2.set(ev::fromUtf8(cmd.track_id));
    } else if (cmd.command == "openUri") {
        b.set("uri", cmd.uri);
        b.set("url", cmd.uri);
        std::string uriCopy = cmd.uri;
        b.def("valueOf", 0, [uriCopy](Value, std::span<const Value>) -> Value {
            return ev::fromUtf8(uriCopy);
        });
        arg1.set(ev::fromUtf8(cmd.uri));
    } else if (cmd.command == "volume") {
        double vol = cmd.volume;
        b.set("volume", vol);
        b.set("value", vol);
        b.def("valueOf", 0, [vol](Value, std::span<const Value>) -> Value {
            return ev::fromDouble(vol);
        });
        arg1.set(ev::fromDouble(vol));
    } else if (cmd.command == "rate") {
        double r = cmd.rate;
        b.set("rate", r);
        b.set("value", r);
        b.def("valueOf", 0, [r](Value, std::span<const Value>) -> Value {
            return ev::fromDouble(r);
        });
        arg1.set(ev::fromDouble(r));
    } else if (cmd.command == "loopStatus") {
        std::string ls = std::string(to_string(cmd.loop_status));
        b.set("loopStatus", ls);
        b.set("status", ls);
        b.set("value", ls);
        b.def("valueOf", 0, [ls](Value, std::span<const Value>) -> Value {
            return ev::fromUtf8(ls);
        });
        arg1.set(ev::fromUtf8(ls));
    } else if (cmd.command == "shuffle") {
        bool shuf = cmd.shuffle;
        b.set("shuffle", shuf);
        b.set("shuffleStatus", std::string(to_string(shuffle_status_from_bool(shuf))));
        b.set("value", shuf);
        b.def("valueOf", 0, [shuf](Value, std::span<const Value>) -> Value {
            return ev::fromBool(shuf);
        });
        arg1.set(ev::fromBool(shuf));
    } else if (cmd.command == "fullscreen") {
        bool fs = cmd.fullscreen;
        b.set("fullscreen", fs);
        b.set("value", fs);
        b.def("valueOf", 0, [fs](Value, std::span<const Value>) -> Value {
            return ev::fromBool(fs);
        });
        arg1.set(ev::fromBool(fs));
    }

    ev::Persistent payloadRoot(b.build());

    for (const auto& cb : targets) {
        if (!cb || !ev::isFunction(cb->get())) continue;
        Value callArgs[3] = {payloadRoot.get(), arg1.get(), arg2.get()};
        size_t argc = 1;
        if (!ev::isUndefined(arg2.get())) {
            argc = 3;
        } else if (!ev::isUndefined(arg1.get())) {
            argc = 2;
        }
        ev::catchThrow([&]() {
            return ev::call(cb->get(), ev::undefined(), std::span<const Value>(callArgs, argc)).value;
        });
    }
}

void tickServices() {
    std::vector<std::shared_ptr<PlayerService>> services;
    {
        std::lock_guard lock(g_managed_services_mu);
        services.reserve(g_managed_services.size());
        for (const auto& [_, entry] : g_managed_services) {
            if (entry && entry->service) {
                services.push_back(entry->service);
            }
        }
    }
    for (const auto& svc : services) {
        svc->process();
    }
}

void shutdownServices() {
    std::map<uint64_t, std::shared_ptr<ManagedService>> services;
    {
        std::lock_guard lock(g_managed_services_mu);
        services.swap(g_managed_services);
    }
    for (auto& [_, entry] : services) {
        if (entry) {
            if (entry->service) {
                entry->service->stop();
            }
            entry->subscriptions.clear();
        }
    }
}

void installNativeMprisService(Value mprisVal) {
    ObjectBuilder mpris(mprisVal);

    auto createServiceFn = [](Value, std::span<const Value> args) -> Value {
        ArgReader reader(args);
        PlayerServiceConfig cfg;
        ev::Persistent optsRoot(ev::undefined());

        if (reader.has(0) && reader.isString(0)) {
            cfg.name = reader.getString(0, "player");
            cfg.identity = (reader.has(1) && reader.isString(1)) ? reader.getString(1) : cfg.name;
            if (reader.has(2) && reader.isString(2)) {
                cfg.desktop_entry = reader.getString(2);
            }
        } else if (reader.has(0) && reader.isObject(0)) {
            optsRoot.set(reader.get(0));
            auto getStr = [&](std::string_view k) -> std::string {
                Value v = ev::getProperty(optsRoot.get(), k);
                if (ev::isString(v)) {
                    ev::Persistent p(v);
                    return ev::toUtf8(p.get());
                }
                return "";
            };

            std::string name = getStr("name");
            if (name.empty()) name = getStr("busName");
            if (name.empty()) name = getStr("serviceName");
            if (name.empty()) name = getStr("id");
            if (!name.empty()) cfg.name = name;

            std::string ident = getStr("identity");
            cfg.identity = !ident.empty() ? ident : cfg.name;

            std::string de = getStr("desktopEntry");
            if (de.empty()) de = getStr("desktop_entry");
            if (de.empty()) de = getStr("appId");
            cfg.desktop_entry = de;

            // Top-level and nested capabilities
            cfg.capabilities = parseCapabilitiesObject(optsRoot.get(), cfg.capabilities);
            Value capsVal = ev::getProperty(optsRoot.get(), "capabilities");
            if (ev::isObject(capsVal)) {
                cfg.capabilities = parseCapabilitiesObject(capsVal, cfg.capabilities);
            }

            std::string pbStatus = getStr("playbackStatus");
            if (pbStatus.empty()) pbStatus = getStr("playback_status");
            if (pbStatus.empty()) pbStatus = getStr("status");
            if (!pbStatus.empty()) {
                cfg.playback_status = playback_status_from_string(pbStatus);
            }

            std::string loopStatus = getStr("loopStatus");
            if (loopStatus.empty()) loopStatus = getStr("loop_status");
            if (!loopStatus.empty()) {
                cfg.loop_status = loop_status_from_string(loopStatus);
            }

            Value shufVal = ev::getProperty(optsRoot.get(), "shuffle");
            if (ev::isUndefined(shufVal)) {
                shufVal = ev::getProperty(optsRoot.get(), "shuffleStatus");
            }
            if (!ev::isUndefined(shufVal) && !ev::isNull(shufVal)) {
                if (ev::isString(shufVal)) {
                    ev::Persistent p(shufVal);
                    cfg.shuffle_status = shuffle_status_from_string(ev::toUtf8(p.get()));
                } else {
                    cfg.shuffle_status = shuffle_status_from_bool(ev::toBool(shufVal));
                }
            }

            Value volVal = ev::getProperty(optsRoot.get(), "volume");
            if (ev::isNumber(volVal)) {
                cfg.volume = ev::toDouble(volVal);
            }

            Value rateVal = ev::getProperty(optsRoot.get(), "rate");
            if (ev::isNumber(rateVal)) {
                cfg.rate = ev::toDouble(rateVal);
            }

            Value posVal = ev::getProperty(optsRoot.get(), "position");
            if (ev::isUndefined(posVal)) posVal = ev::getProperty(optsRoot.get(), "positionUsec");
            if (ev::isUndefined(posVal)) posVal = ev::getProperty(optsRoot.get(), "position_usec");
            if (ev::isNumber(posVal)) {
                cfg.position_usec = static_cast<int64_t>(ev::toDouble(posVal));
            }

            Value metaVal = ev::getProperty(optsRoot.get(), "metadata");
            if (ev::isObject(metaVal)) {
                cfg.metadata = parseMetadataObject(metaVal);
            }
        }

        const uint64_t serviceId = g_next_service_id++;
        auto nativeSvc = createApiPlayerService(cfg, makeServiceCallbacks(serviceId));
        if (!nativeSvc) {
            return ev::null();
        }

        auto managed = std::make_shared<ManagedService>();
        managed->id = serviceId;
        managed->service = std::shared_ptr<PlayerService>(std::move(nativeSvc));
        {
            std::lock_guard lock(g_managed_services_mu);
            g_managed_services[serviceId] = managed;
        }

        // Register any callback properties passed in options
        if (ev::isObject(optsRoot.get())) {
            struct OptCb {
                const char* prop;
                const char* event;
            };
            constexpr OptCb kOptCbs[] = {
                {"onCommand", "*"},
                {"onPlay", "play"},
                {"onPause", "pause"},
                {"onPlayPause", "playPause"},
                {"onStop", "stop"},
                {"onNext", "next"},
                {"onPrevious", "previous"},
                {"onSeek", "seek"},
                {"onSetPosition", "setPosition"},
                {"onOpenUri", "openUri"},
                {"onRaise", "raise"},
                {"onQuit", "quit"},
                {"onVolume", "volume"},
                {"onSetVolume", "volume"},
                {"onVolumeChange", "volume"},
                {"onLoopStatus", "loopStatus"},
                {"onSetLoopStatus", "loopStatus"},
                {"onLoopStatusChange", "loopStatus"},
                {"onShuffle", "shuffle"},
                {"onSetShuffle", "shuffle"},
                {"onShuffleChange", "shuffle"},
                {"onRate", "rate"},
                {"onSetRate", "rate"},
                {"onRateChange", "rate"},
            };
            for (const auto& item : kOptCbs) {
                Value fn = ev::getProperty(optsRoot.get(), item.prop);
                if (ev::isFunction(fn)) {
                    addServiceListener(serviceId, item.event, fn);
                }
            }
        }

        return buildServiceHandle(serviceId, managed->service);
    };

    mpris.def("createService", 1, createServiceFn);
    mpris.def("publish", 1, createServiceFn);
    mpris.def("createPlayerService", 1, createServiceFn);
}

}  // namespace brompris::api

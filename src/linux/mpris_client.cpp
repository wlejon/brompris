#include "src/linux/mpris_client.h"

#if defined(BROMPRIS_HAS_BRODBUS)

#include <cmath>
#include <iostream>

namespace brompris {

namespace {

bool read_mpris_metadata_container(brodbus::Message& msg, MediaMetadata& out) {
    if (msg.enter_container('v', nullptr) < 0) {
        return false;
    }

    std::map<std::string, brodbus::PropertyValue> meta_map;
    if (msg.enter_container('a', "{sv}") >= 0) {
        while (!msg.at_end()) {
            if (msg.enter_container('e', "sv") >= 0) {
                std::string k;
                brodbus::PropertyValue v;
                if (msg.read_string(&k) && msg.read_variant(&v)) {
                    meta_map[k] = std::move(v);
                }
                msg.exit_container();
            } else {
                break;
            }
        }
        msg.exit_container();
    }
    msg.exit_container();

    out = parse_mpris_metadata(meta_map);
    return true;
}

}  // namespace

// ============================================================================
// MprisPlaybackController
// ============================================================================

MprisPlaybackController::MprisPlaybackController(
    std::shared_ptr<brodbus::Bus> bus, std::string destination)
    : bus_(std::move(bus)), destination_(std::move(destination)) {}

bool MprisPlaybackController::call_player_method(const std::string& member) {
    if (!bus_ || destination_.empty()) return false;
    brodbus::Message msg = bus_->new_method_call(
        destination_, "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player", member);
    if (!msg) return false;
    brodbus::Error err;
    brodbus::Message rep = bus_->call(msg, 1000000, &err);
    return rep.is_valid() && !err.is_set();
}

bool MprisPlaybackController::set_player_property(
    const std::string& prop, const brodbus::PropertyValue& val) {
    if (!bus_ || destination_.empty()) return false;
    brodbus::Message msg = bus_->new_method_call(
        destination_, "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties", "Set");
    if (!msg) return false;
    if (!msg.append_string("org.mpris.MediaPlayer2.Player")) return false;
    if (!msg.append_string(prop)) return false;
    if (!msg.append_variant(val)) return false;
    brodbus::Error err;
    brodbus::Message rep = bus_->call(msg, 1000000, &err);
    return rep.is_valid() && !err.is_set();
}

bool MprisPlaybackController::play() {
    return call_player_method("Play");
}

bool MprisPlaybackController::pause() {
    return call_player_method("Pause");
}

bool MprisPlaybackController::play_pause() {
    return call_player_method("PlayPause");
}

bool MprisPlaybackController::stop() {
    return call_player_method("Stop");
}

bool MprisPlaybackController::next() {
    return call_player_method("Next");
}

bool MprisPlaybackController::previous() {
    return call_player_method("Previous");
}

bool MprisPlaybackController::seek(int64_t offset_usec) {
    if (!bus_ || destination_.empty()) return false;
    brodbus::Message msg = bus_->new_method_call(
        destination_, "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player", "Seek");
    if (!msg) return false;
    if (!msg.append_int64(offset_usec)) return false;
    brodbus::Error err;
    brodbus::Message rep = bus_->call(msg, 1000000, &err);
    return rep.is_valid() && !err.is_set();
}

bool MprisPlaybackController::set_position(
    const std::string& track_id, int64_t position_usec) {
    if (!bus_ || destination_.empty()) return false;
    brodbus::Message msg = bus_->new_method_call(
        destination_, "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player", "SetPosition");
    if (!msg) return false;

    std::string tid = track_id;
    if (tid.empty()) {
        tid = "/org/mpris/MediaPlayer2/TrackList/NoTrack";
    } else if (tid.front() != '/') {
        tid = "/org/mpris/MediaPlayer2/TrackList/" + tid;
    }

    if (!msg.append_object_path(brodbus::ObjectPath(tid))) return false;
    if (!msg.append_int64(position_usec)) return false;

    brodbus::Error err;
    brodbus::Message rep = bus_->call(msg, 1000000, &err);
    return rep.is_valid() && !err.is_set();
}

bool MprisPlaybackController::set_volume(double volume) {
    return set_player_property("Volume", brodbus::PropertyValue(volume));
}

bool MprisPlaybackController::set_loop_status(LoopStatus status) {
    return set_player_property(
        "LoopStatus", brodbus::PropertyValue(std::string(to_string(status))));
}

bool MprisPlaybackController::set_shuffle_status(ShuffleStatus status) {
    return set_player_property(
        "Shuffle", brodbus::PropertyValue(shuffle_status_to_bool(status)));
}

bool MprisPlaybackController::set_rate(double rate) {
    return set_player_property("Rate", brodbus::PropertyValue(rate));
}

bool MprisPlaybackController::open_uri(const std::string& uri) {
    if (!bus_ || destination_.empty()) return false;
    brodbus::Message msg = bus_->new_method_call(
        destination_, "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player", "OpenUri");
    if (!msg) return false;
    if (!msg.append_string(uri)) return false;
    brodbus::Error err;
    brodbus::Message rep = bus_->call(msg, 1000000, &err);
    return rep.is_valid() && !err.is_set();
}

bool MprisPlaybackController::raise() {
    if (!bus_ || destination_.empty()) return false;
    brodbus::Message msg = bus_->new_method_call(
        destination_, "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2", "Raise");
    if (!msg) return false;
    brodbus::Error err;
    brodbus::Message rep = bus_->call(msg, 1000000, &err);
    return rep.is_valid() && !err.is_set();
}

bool MprisPlaybackController::quit() {
    if (!bus_ || destination_.empty()) return false;
    brodbus::Message msg = bus_->new_method_call(
        destination_, "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2", "Quit");
    if (!msg) return false;
    brodbus::Error err;
    brodbus::Message rep = bus_->call(msg, 1000000, &err);
    return rep.is_valid() && !err.is_set();
}

// ============================================================================
// MprisMediaPlayer
// ============================================================================

MprisMediaPlayer::MprisMediaPlayer(
    std::shared_ptr<brodbus::Bus> bus,
    std::string well_known_name,
    std::string unique_name)
    : bus_(std::move(bus)),
      well_known_name_(std::move(well_known_name)),
      unique_name_(std::move(unique_name)),
      last_activity_time_(std::chrono::steady_clock::now()) {
    model().set_id(well_known_name_);
    set_controller(std::make_shared<MprisPlaybackController>(bus_, well_known_name_));
    refresh_properties();
}

void MprisMediaPlayer::query_root_properties() {
    if (!bus_ || well_known_name_.empty()) return;

    brodbus::Message call = bus_->new_method_call(
        well_known_name_, "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties", "GetAll");
    if (!call) return;
    call.append_string("org.mpris.MediaPlayer2");

    brodbus::Message rep = bus_->call(call, 100000);
    if (!rep) return;

    if (rep.enter_container('a', "{sv}") >= 0) {
        while (!rep.at_end()) {
            if (rep.enter_container('e', "sv") >= 0) {
                std::string k;
                brodbus::PropertyValue v;
                if (rep.read_string(&k) && rep.read_variant(&v)) {
                    apply_property(k, v);
                }
                rep.exit_container();
            } else {
                break;
            }
        }
        rep.exit_container();
    }
}

void MprisMediaPlayer::query_player_properties() {
    if (!bus_ || well_known_name_.empty()) return;

    brodbus::Message call = bus_->new_method_call(
        well_known_name_, "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties", "GetAll");
    if (!call) return;
    call.append_string("org.mpris.MediaPlayer2.Player");

    brodbus::Message rep = bus_->call(call, 100000);
    if (rep && rep.enter_container('a', "{sv}") >= 0) {
        while (!rep.at_end()) {
            if (rep.enter_container('e', "sv") >= 0) {
                std::string k;
                if (rep.read_string(&k)) {
                    if (k == "Metadata") {
                        MediaMetadata meta;
                        if (read_mpris_metadata_container(rep, meta)) {
                            apply_metadata(meta);
                        }
                    } else {
                        brodbus::PropertyValue v;
                        if (rep.read_variant(&v)) {
                            apply_property(k, v);
                        }
                    }
                }
                rep.exit_container();
            } else {
                break;
            }
        }
        rep.exit_container();
    }

    // Query Position individually if not populated by GetAll
    if (!model().has_position()) {
        brodbus::Message get_pos = bus_->new_method_call(
            well_known_name_, "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties", "Get");
        if (get_pos) {
            get_pos.append_string("org.mpris.MediaPlayer2.Player");
            get_pos.append_string("Position");
            brodbus::Message pos_rep = bus_->call(get_pos, 50000);
            if (pos_rep) {
                brodbus::PropertyValue val;
                if (pos_rep.read_variant(&val)) {
                    apply_property("Position", val);
                }
            }
        }
    }
}

void MprisMediaPlayer::refresh_properties() {
    query_root_properties();
    query_player_properties();
}

void MprisMediaPlayer::apply_property(
    const std::string& name, const brodbus::PropertyValue& val) {
    if (name == "PlaybackStatus") {
        auto s = playback_status_from_string(val.as_string());
        if (model().playback_status() != s) {
            model().set_playback_status(s);
            record_activity();
            notify_playback_status_changed(s);
        }
    } else if (name == "LoopStatus") {
        auto l = loop_status_from_string(val.as_string());
        if (model().loop_status() != l) {
            model().set_loop_status(l);
            notify_loop_status_changed(l);
        }
    } else if (name == "Shuffle") {
        auto shuf = shuffle_status_from_bool(val.as_bool());
        if (model().shuffle_status() != shuf) {
            model().set_shuffle_status(shuf);
            notify_shuffle_status_changed(shuf);
        }
    } else if (name == "Volume") {
        double v = val.as_double(1.0);
        if (std::abs(model().volume() - v) > 0.001) {
            model().set_volume(v);
            notify_volume_changed(v);
        }
    } else if (name == "Rate") {
        double r = val.as_double(1.0);
        if (std::abs(model().rate() - r) > 0.001) {
            model().set_rate(r);
            notify_rate_changed(r);
        }
    } else if (name == "Position") {
        int64_t pos = val.is<uint64_t>() ? static_cast<int64_t>(val.as_uint64()) : val.as_int64();
        model().set_position(pos);
        notify_position_seeked(pos);
    } else if (name == "Identity") {
        model().set_identity(val.as_string());
    } else if (name == "DesktopEntry") {
        model().set_desktop_entry(val.as_string());
    } else {
        // Capabilities
        PlayerCapabilities caps = model().capabilities();
        bool changed = false;

        if (name == "CanControl") {
            caps.can_control = val.as_bool();
            changed = true;
        } else if (name == "CanPlay") {
            caps.can_play = val.as_bool();
            changed = true;
        } else if (name == "CanPause") {
            caps.can_pause = val.as_bool();
            changed = true;
        } else if (name == "CanSeek") {
            caps.can_seek = val.as_bool();
            changed = true;
        } else if (name == "CanGoNext") {
            caps.can_go_next = val.as_bool();
            changed = true;
        } else if (name == "CanGoPrevious") {
            caps.can_go_previous = val.as_bool();
            changed = true;
        } else if (name == "CanQuit") {
            caps.can_quit = val.as_bool();
            changed = true;
        } else if (name == "CanRaise") {
            caps.can_raise = val.as_bool();
            changed = true;
        } else if (name == "CanSetFullscreen") {
            caps.can_set_fullscreen = val.as_bool();
            changed = true;
        }

        if (changed && !(model().capabilities() == caps)) {
            model().set_capabilities(caps);
            notify_capabilities_changed(caps);
        }
    }
}

void MprisMediaPlayer::apply_metadata(const MediaMetadata& meta) {
    if (!(model().metadata() == meta)) {
        model().set_metadata(meta);
        record_activity();
        notify_metadata_changed(meta);
    }
}

void MprisMediaPlayer::handle_seeked(int64_t position_usec) {
    model().set_position(position_usec);
    record_activity();
    notify_position_seeked(position_usec);
}

// ============================================================================
// LinuxMediaManager
// ============================================================================

LinuxMediaManager::LinuxMediaManager(std::shared_ptr<brodbus::Bus> bus)
    : bus_(std::move(bus)) {
    if (bus_ && bus_->is_valid()) {
        setup_matches();
        discover_players();
        update_active_player_if_needed();
    }
}

LinuxMediaManager::~LinuxMediaManager() {
    slots_.clear();
}

void LinuxMediaManager::setup_matches() {
    if (!bus_ || !bus_->is_valid()) return;

    std::string err;

    // 1. Monitor NameOwnerChanged to detect media player lifetime
    slots_.push_back(bus_->add_match(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
        [this](brodbus::Message& msg) {
            handle_name_owner_changed(msg);
        }, &err));

    // 2. Monitor PropertiesChanged on /org/mpris/MediaPlayer2
    slots_.push_back(bus_->add_match(
        "type='signal',path='/org/mpris/MediaPlayer2',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
        [this](brodbus::Message& msg) {
            handle_properties_changed(msg);
        }, &err));

    // 3. Monitor Seeked on /org/mpris/MediaPlayer2
    slots_.push_back(bus_->add_match(
        "type='signal',path='/org/mpris/MediaPlayer2',interface='org.mpris.MediaPlayer2.Player',member='Seeked'",
        [this](brodbus::Message& msg) {
            handle_seeked(msg);
        }, &err));
}

void LinuxMediaManager::discover_players() {
    if (!bus_ || !bus_->is_valid()) return;

    std::vector<std::string> names;
    bool ok = bus_->call_method(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ListNames",
        nullptr,
        [&](brodbus::Message& reply) {
            reply.read_string_list(&names);
        });

    if (!ok) return;

    for (const auto& name : names) {
        if (name.rfind("org.mpris.MediaPlayer2.", 0) == 0) {
            std::string unique;
            bus_->call_method(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                "GetNameOwner",
                [&](brodbus::Message& m) { m.append_string(name); },
                [&](brodbus::Message& r) { r.read_string(&unique); });

            add_or_update_player(name, unique);
        }
    }
}

void LinuxMediaManager::add_or_update_player(
    const std::string& well_known_name, const std::string& unique_name) {
    std::shared_ptr<MprisMediaPlayer> player;
    bool is_new = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = players_by_name_.find(well_known_name);
        if (it != players_by_name_.end()) {
            player = it->second;
            if (!unique_name.empty()) {
                player->set_unique_name(unique_name);
                players_by_unique_[unique_name] = player;
            }
            player->refresh_properties();
        } else {
            player = std::make_shared<MprisMediaPlayer>(bus_, well_known_name, unique_name);
            players_by_name_[well_known_name] = player;
            if (!unique_name.empty()) {
                players_by_unique_[unique_name] = player;
            }
            is_new = true;
        }
    }

    if (is_new) {
        notify_player_added(player);
    }
    update_active_player_if_needed();
}

void LinuxMediaManager::remove_player(const std::string& well_known_name) {
    std::shared_ptr<MprisMediaPlayer> player;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = players_by_name_.find(well_known_name);
        if (it != players_by_name_.end()) {
            player = it->second;
            if (!player->unique_name().empty()) {
                players_by_unique_.erase(player->unique_name());
            }
            players_by_name_.erase(it);
        }
    }

    if (player) {
        notify_player_removed(well_known_name);
        update_active_player_if_needed();
    }
}

std::shared_ptr<MprisMediaPlayer> LinuxMediaManager::find_player_internal(
    const std::string& sender_or_name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it_u = players_by_unique_.find(sender_or_name);
    if (it_u != players_by_unique_.end()) {
        return it_u->second;
    }

    auto it_n = players_by_name_.find(sender_or_name);
    if (it_n != players_by_name_.end()) {
        return it_n->second;
    }

    if (players_by_name_.size() == 1) {
        return players_by_name_.begin()->second;
    }

    return nullptr;
}

void LinuxMediaManager::handle_name_owner_changed(brodbus::Message& msg) {
    std::string name, old_owner, new_owner;
    if (!msg.read_string(&name)) return;
    if (!msg.read_string(&old_owner)) return;
    if (!msg.read_string(&new_owner)) return;

    if (name.rfind("org.mpris.MediaPlayer2.", 0) != 0) {
        return;
    }

    if (new_owner.empty()) {
        remove_player(name);
    } else {
        add_or_update_player(name, new_owner);
    }
}

void LinuxMediaManager::handle_properties_changed(brodbus::Message& msg) {
    std::string sender = msg.get_sender();
    auto player = find_player_internal(sender);
    if (!player) return;

    std::string interface_name;
    if (!msg.read_string(&interface_name)) return;

    if (msg.enter_container('a', "{sv}") >= 0) {
        while (!msg.at_end()) {
            if (msg.enter_container('e', "sv") >= 0) {
                std::string k;
                if (msg.read_string(&k)) {
                    if (k == "Metadata") {
                        MediaMetadata meta;
                        if (read_mpris_metadata_container(msg, meta)) {
                            player->apply_metadata(meta);
                        }
                    } else {
                        brodbus::PropertyValue v;
                        if (msg.read_variant(&v)) {
                            player->apply_property(k, v);
                        }
                    }
                }
                msg.exit_container();
            } else {
                break;
            }
        }
        msg.exit_container();
    }

    update_active_player_if_needed();
}

void LinuxMediaManager::handle_seeked(brodbus::Message& msg) {
    std::string sender = msg.get_sender();
    auto player = find_player_internal(sender);
    if (!player) return;

    int64_t position_usec = 0;
    if (msg.read_int64(&position_usec)) {
        player->handle_seeked(position_usec);
    }
}

std::vector<std::shared_ptr<MediaPlayer>> LinuxMediaManager::active_players() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<MediaPlayer>> list;
    list.reserve(players_by_name_.size());
    for (const auto& [_, p] : players_by_name_) {
        list.push_back(p);
    }
    return list;
}

std::shared_ptr<MediaPlayer> LinuxMediaManager::active_player() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (players_by_name_.empty()) return nullptr;

    std::shared_ptr<MprisMediaPlayer> best = nullptr;

    for (const auto& [_, player] : players_by_name_) {
        if (!best) {
            best = player;
            continue;
        }

        bool best_playing = (best->playback_status() == PlaybackStatus::Playing);
        bool cur_playing = (player->playback_status() == PlaybackStatus::Playing);
        if (cur_playing && !best_playing) {
            best = player;
            continue;
        }
        if (!cur_playing && best_playing) {
            continue;
        }

        bool best_paused = (best->playback_status() == PlaybackStatus::Paused);
        bool cur_paused = (player->playback_status() == PlaybackStatus::Paused);
        if (cur_paused && !best_paused) {
            best = player;
            continue;
        }
        if (!cur_paused && best_paused) {
            continue;
        }

        if (player->last_activity_time() > best->last_activity_time()) {
            best = player;
        }
    }

    return best;
}

std::shared_ptr<MediaPlayer> LinuxMediaManager::find_player(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_by_name_.find(id);
    if (it != players_by_name_.end()) {
        return it->second;
    }
    auto it_u = players_by_unique_.find(id);
    if (it_u != players_by_unique_.end()) {
        return it_u->second;
    }
    return nullptr;
}

void LinuxMediaManager::update_active_player_if_needed() {
    auto active = active_player();
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active != current_active_player_) {
            current_active_player_ = active;
            changed = true;
        }
    }
    if (changed) {
        notify_active_player_changed(active);
    }
}

int LinuxMediaManager::process() {
    if (!bus_ || !bus_->is_valid()) return -1;
    int processed = 0;
    while (bus_->process() > 0) {
        ++processed;
    }
    update_active_player_if_needed();
    return processed;
}

int LinuxMediaManager::wait(uint64_t timeout_usec) {
    if (!bus_ || !bus_->is_valid()) return -1;
    int r = bus_->wait(timeout_usec);
    process();
    return r;
}

void LinuxMediaManager::update() {
    process();
}

// Platform factory functions
std::unique_ptr<MediaManager> create_linux_media_manager() {
    std::string err;
    auto bus = brodbus::Bus::open_user(&err);
    if (!bus) return nullptr;
    return std::make_unique<LinuxMediaManager>(std::move(bus));
}

std::unique_ptr<MediaManager> create_linux_media_manager_with_bus(
    std::shared_ptr<brodbus::Bus> bus) {
    if (!bus) return nullptr;
    return std::make_unique<LinuxMediaManager>(std::move(bus));
}

std::unique_ptr<MediaManager> create_linux_media_manager_with_address(
    const std::string& address) {
    std::string err;
    auto bus = brodbus::Bus::open_address(address, &err);
    if (!bus) return nullptr;
    return std::make_unique<LinuxMediaManager>(std::move(bus));
}

}  // namespace brompris

#endif  // BROMPRIS_HAS_BRODBUS

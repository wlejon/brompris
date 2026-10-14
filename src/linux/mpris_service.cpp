#include "src/linux/mpris_service.h"

#if defined(BROMPRIS_HAS_BRODBUS) && defined(__linux__)

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cmath>
#include <utility>

namespace brompris {

namespace {

constexpr const char* kMprisObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kIfaceIntrospectable = "org.freedesktop.DBus.Introspectable";
constexpr const char* kIfaceProperties = "org.freedesktop.DBus.Properties";
constexpr const char* kIfaceRoot = "org.mpris.MediaPlayer2";
constexpr const char* kIfacePlayer = "org.mpris.MediaPlayer2.Player";

constexpr const char* kIntrospectionXml =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    " \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "    <method name=\"Introspect\">\n"
    "      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
    "    <method name=\"Get\">\n"
    "      <arg name=\"interface\" direction=\"in\" type=\"s\"/>\n"
    "      <arg name=\"property\" direction=\"in\" type=\"s\"/>\n"
    "      <arg name=\"value\" direction=\"out\" type=\"v\"/>\n"
    "    </method>\n"
    "    <method name=\"GetAll\">\n"
    "      <arg name=\"interface\" direction=\"in\" type=\"s\"/>\n"
    "      <arg name=\"properties\" direction=\"out\" type=\"a{sv}\"/>\n"
    "    </method>\n"
    "    <method name=\"Set\">\n"
    "      <arg name=\"interface\" direction=\"in\" type=\"s\"/>\n"
    "      <arg name=\"property\" direction=\"in\" type=\"s\"/>\n"
    "      <arg name=\"value\" direction=\"in\" type=\"v\"/>\n"
    "    </method>\n"
    "    <signal name=\"PropertiesChanged\">\n"
    "      <arg name=\"interface\" type=\"s\"/>\n"
    "      <arg name=\"changed_properties\" type=\"a{sv}\"/>\n"
    "      <arg name=\"invalidated_properties\" type=\"as\"/>\n"
    "    </signal>\n"
    "  </interface>\n"
    "  <interface name=\"org.mpris.MediaPlayer2\">\n"
    "    <method name=\"Raise\"/>\n"
    "    <method name=\"Quit\"/>\n"
    "    <property name=\"CanQuit\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"Fullscreen\" type=\"b\" access=\"readwrite\"/>\n"
    "    <property name=\"CanSetFullscreen\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanRaise\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"HasTrackList\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"Identity\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"DesktopEntry\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"SupportedUriSchemes\" type=\"as\" access=\"read\"/>\n"
    "    <property name=\"SupportedMimeTypes\" type=\"as\" access=\"read\"/>\n"
    "  </interface>\n"
    "  <interface name=\"org.mpris.MediaPlayer2.Player\">\n"
    "    <method name=\"Next\"/>\n"
    "    <method name=\"Previous\"/>\n"
    "    <method name=\"Pause\"/>\n"
    "    <method name=\"PlayPause\"/>\n"
    "    <method name=\"Stop\"/>\n"
    "    <method name=\"Play\"/>\n"
    "    <method name=\"Seek\">\n"
    "      <arg name=\"Offset\" direction=\"in\" type=\"x\"/>\n"
    "    </method>\n"
    "    <method name=\"SetPosition\">\n"
    "      <arg name=\"TrackId\" direction=\"in\" type=\"o\"/>\n"
    "      <arg name=\"Position\" direction=\"in\" type=\"x\"/>\n"
    "    </method>\n"
    "    <method name=\"OpenUri\">\n"
    "      <arg name=\"Uri\" direction=\"in\" type=\"s\"/>\n"
    "    </method>\n"
    "    <signal name=\"Seeked\">\n"
    "      <arg name=\"Position\" type=\"x\"/>\n"
    "    </signal>\n"
    "    <property name=\"PlaybackStatus\" type=\"s\" access=\"read\"/>\n"
    "    <property name=\"LoopStatus\" type=\"s\" access=\"readwrite\"/>\n"
    "    <property name=\"Rate\" type=\"d\" access=\"readwrite\"/>\n"
    "    <property name=\"Shuffle\" type=\"b\" access=\"readwrite\"/>\n"
    "    <property name=\"Metadata\" type=\"a{sv}\" access=\"read\"/>\n"
    "    <property name=\"Volume\" type=\"d\" access=\"readwrite\"/>\n"
    "    <property name=\"Position\" type=\"x\" access=\"read\"/>\n"
    "    <property name=\"MinimumRate\" type=\"d\" access=\"read\"/>\n"
    "    <property name=\"MaximumRate\" type=\"d\" access=\"read\"/>\n"
    "    <property name=\"CanGoNext\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanGoPrevious\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanPlay\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanPause\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanSeek\" type=\"b\" access=\"read\"/>\n"
    "    <property name=\"CanControl\" type=\"b\" access=\"read\"/>\n"
    "  </interface>\n"
    "</node>\n";

bool append_metadata_variant(brodbus::Message& msg, const MediaMetadata& meta) {
    if (msg.open_container('v', "a{sv}") < 0) return false;
    if (msg.open_container('a', "{sv}") < 0) {
        msg.close_container();
        return false;
    }
    auto dict = to_mpris_properties(meta);
    for (const auto& [k, v] : dict) {
        if (msg.open_container('e', "sv") < 0) break;
        msg.append_string(k);
        msg.append_variant(v);
        msg.close_container();
    }
    msg.close_container();
    return msg.close_container() >= 0;
}

void append_dict_entry(brodbus::Message& msg,
                       const std::string& key,
                       const brodbus::PropertyValue& val) {
    if (msg.open_container('e', "sv") >= 0) {
        msg.append_string(key);
        msg.append_variant(val);
        msg.close_container();
    }
}

std::atomic<uint32_t> g_instance_counter{0};

}  // namespace

LinuxPlayerService::LinuxPlayerService(std::shared_ptr<brodbus::Bus> bus,
                                       const PlayerServiceConfig& config,
                                       PlayerServiceCallbacks callbacks)
    : PlayerService(config, std::move(callbacks)),
      bus_(std::move(bus)),
      bus_name_(id()) {
    start();
}

LinuxPlayerService::~LinuxPlayerService() {
    stop();
    if (worker_thread_.joinable()) {
        if (worker_thread_.get_id() != std::this_thread::get_id()) {
            worker_thread_.join();
        } else {
            worker_thread_.detach();
        }
    }
    if (wakeup_fd_ >= 0) {
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
}

bool LinuxPlayerService::start() {
    if (!bus_ || !bus_->is_valid()) {
        running_ = false;
        return false;
    }
    if (name_acquired_.load()) {
        return true;
    }

    if (wakeup_fd_ < 0) {
        wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
        if (!object_slot_.is_valid()) {
            sd_bus_slot* raw_slot = nullptr;
            int r = sd_bus_add_object(
                bus_->raw(), &raw_slot, kMprisObjectPath,
                &LinuxPlayerService::on_bus_message, this);
            if (r < 0) {
                running_ = false;
                return false;
            }
            object_slot_.reset(raw_slot);
        }
    }

    if (!worker_running_.load()) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        worker_running_.store(true);
        worker_thread_ = std::thread(&LinuxPlayerService::worker_loop, this);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
        std::string base_name = normalize_bus_name(name());
        std::string candidate = base_name;
        std::string err;

        bool acquired = bus_->request_name(candidate, 0, &err);
        if (!acquired) {
            candidate = base_name + ".instance" + std::to_string(::getpid());
            acquired = bus_->request_name(candidate, 0, &err);
        }
        if (!acquired) {
            uint32_t seq = ++g_instance_counter;
            candidate = base_name + ".instance" + std::to_string(::getpid()) +
                        "_" + std::to_string(seq);
            acquired = bus_->request_name(candidate, 0, &err);
        }
        if (!acquired) {
            running_ = false;
            return false;
        }

        bus_name_ = candidate;
        set_bus_name_internal(bus_name_);
        name_acquired_.store(true);
        running_ = true;
        bus_->flush();
    }

    return true;
}

void LinuxPlayerService::stop() {
    running_ = false;
    if (worker_running_.exchange(false)) {
        wake_worker();
    }
    if (worker_thread_.joinable() &&
        worker_thread_.get_id() != std::this_thread::get_id()) {
        worker_thread_.join();
    }

    std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
    object_slot_.reset();
    if (name_acquired_.exchange(false) && bus_ && bus_->is_valid()) {
        std::string err;
        bus_->release_name(bus_name_, &err);
        bus_->flush();
    }
}

void LinuxPlayerService::wake_worker() {
    if (wakeup_fd_ >= 0) {
        uint64_t val = 1;
        ssize_t n = ::write(wakeup_fd_, &val, sizeof(val));
        (void)n;
    }
}

void LinuxPlayerService::worker_loop() {
    while (worker_running_.load()) {
        {
            std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
            if (!bus_ || !bus_->is_valid()) break;
            while (bus_->process() > 0) {}
        }

        if (!worker_running_.load()) break;

        int bus_fd = -1;
        {
            std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
            if (bus_ && bus_->is_valid()) {
                bus_fd = bus_->get_fd();
            }
        }
        if (bus_fd < 0) break;

        struct pollfd fds[2]{};
        nfds_t nfds = 0;
        fds[nfds].fd = bus_fd;
        fds[nfds].events = POLLIN;
        ++nfds;

        if (wakeup_fd_ >= 0) {
            fds[nfds].fd = wakeup_fd_;
            fds[nfds].events = POLLIN;
            ++nfds;
        }

        int pr = ::poll(fds, nfds, 25);
        if (pr > 0 && nfds > 1 && (fds[1].revents & POLLIN)) {
            uint64_t val = 0;
            ssize_t n = ::read(wakeup_fd_, &val, sizeof(val));
            (void)n;
        }
    }
}

int LinuxPlayerService::process() {
    std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
    if (!bus_ || !bus_->is_valid()) return -1;
    int count = 0;
    while (bus_->process() > 0) {
        ++count;
    }
    return count;
}

int LinuxPlayerService::wait(uint64_t timeout_usec) {
    (void)timeout_usec;
    return process();
}

int LinuxPlayerService::on_bus_message(
    sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<LinuxPlayerService*>(userdata);
    if (!self || !m) return 0;
    brodbus::Message msg(m, false);
    return self->handle_method_call(msg) ? 1 : 0;
}

bool LinuxPlayerService::append_property_variant(
    brodbus::Message& msg, const std::string& iface, const std::string& prop) const {
    const bool allow_root = iface.empty() || iface == kIfaceRoot;
    const bool allow_player = iface.empty() || iface == kIfacePlayer;

    if (allow_root) {
        if (prop == "CanQuit") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_quit));
        }
        if (prop == "Fullscreen") {
            return msg.append_variant(brodbus::PropertyValue(fullscreen()));
        }
        if (prop == "CanSetFullscreen") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_set_fullscreen));
        }
        if (prop == "CanRaise") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_raise));
        }
        if (prop == "HasTrackList") {
            return msg.append_variant(brodbus::PropertyValue(false));
        }
        if (prop == "Identity") {
            return msg.append_variant(brodbus::PropertyValue(identity()));
        }
        if (prop == "DesktopEntry") {
            return msg.append_variant(brodbus::PropertyValue(desktop_entry()));
        }
        if (prop == "SupportedUriSchemes") {
            return msg.append_variant(brodbus::PropertyValue(supported_uri_schemes()));
        }
        if (prop == "SupportedMimeTypes") {
            return msg.append_variant(brodbus::PropertyValue(supported_mime_types()));
        }
    }

    if (allow_player) {
        if (prop == "PlaybackStatus") {
            return msg.append_variant(
                brodbus::PropertyValue(std::string(to_string(playback_status()))));
        }
        if (prop == "LoopStatus") {
            return msg.append_variant(
                brodbus::PropertyValue(std::string(to_string(loop_status()))));
        }
        if (prop == "Rate") {
            return msg.append_variant(brodbus::PropertyValue(rate()));
        }
        if (prop == "Shuffle") {
            return msg.append_variant(brodbus::PropertyValue(shuffle()));
        }
        if (prop == "Metadata") {
            return append_metadata_variant(msg, metadata());
        }
        if (prop == "Volume") {
            return msg.append_variant(brodbus::PropertyValue(volume()));
        }
        if (prop == "Position") {
            return msg.append_variant(brodbus::PropertyValue(position_usec()));
        }
        if (prop == "MinimumRate") {
            return msg.append_variant(brodbus::PropertyValue(minimum_rate()));
        }
        if (prop == "MaximumRate") {
            return msg.append_variant(brodbus::PropertyValue(maximum_rate()));
        }
        if (prop == "CanGoNext") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_go_next));
        }
        if (prop == "CanGoPrevious") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_go_previous));
        }
        if (prop == "CanPlay") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_play));
        }
        if (prop == "CanPause") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_pause));
        }
        if (prop == "CanSeek") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_seek));
        }
        if (prop == "CanControl") {
            return msg.append_variant(brodbus::PropertyValue(capabilities().can_control));
        }
    }

    return false;
}

void LinuxPlayerService::append_root_properties_entries(brodbus::Message& msg) const {
    const auto caps = capabilities();
    append_dict_entry(msg, "CanQuit", brodbus::PropertyValue(caps.can_quit));
    append_dict_entry(msg, "Fullscreen", brodbus::PropertyValue(fullscreen()));
    append_dict_entry(msg, "CanSetFullscreen", brodbus::PropertyValue(caps.can_set_fullscreen));
    append_dict_entry(msg, "CanRaise", brodbus::PropertyValue(caps.can_raise));
    append_dict_entry(msg, "HasTrackList", brodbus::PropertyValue(false));
    append_dict_entry(msg, "Identity", brodbus::PropertyValue(identity()));
    append_dict_entry(msg, "DesktopEntry", brodbus::PropertyValue(desktop_entry()));
    append_dict_entry(msg, "SupportedUriSchemes", brodbus::PropertyValue(supported_uri_schemes()));
    append_dict_entry(msg, "SupportedMimeTypes", brodbus::PropertyValue(supported_mime_types()));
}

void LinuxPlayerService::append_player_properties_entries(brodbus::Message& msg) const {
    const auto caps = capabilities();
    append_dict_entry(
        msg, "PlaybackStatus", brodbus::PropertyValue(std::string(to_string(playback_status()))));
    append_dict_entry(
        msg, "LoopStatus", brodbus::PropertyValue(std::string(to_string(loop_status()))));
    append_dict_entry(msg, "Rate", brodbus::PropertyValue(rate()));
    append_dict_entry(msg, "Shuffle", brodbus::PropertyValue(shuffle()));

    if (msg.open_container('e', "sv") >= 0) {
        msg.append_string("Metadata");
        append_metadata_variant(msg, metadata());
        msg.close_container();
    }

    append_dict_entry(msg, "Volume", brodbus::PropertyValue(volume()));
    append_dict_entry(msg, "Position", brodbus::PropertyValue(position_usec()));
    append_dict_entry(msg, "MinimumRate", brodbus::PropertyValue(minimum_rate()));
    append_dict_entry(msg, "MaximumRate", brodbus::PropertyValue(maximum_rate()));
    append_dict_entry(msg, "CanGoNext", brodbus::PropertyValue(caps.can_go_next));
    append_dict_entry(msg, "CanGoPrevious", brodbus::PropertyValue(caps.can_go_previous));
    append_dict_entry(msg, "CanPlay", brodbus::PropertyValue(caps.can_play));
    append_dict_entry(msg, "CanPause", brodbus::PropertyValue(caps.can_pause));
    append_dict_entry(msg, "CanSeek", brodbus::PropertyValue(caps.can_seek));
    append_dict_entry(msg, "CanControl", brodbus::PropertyValue(caps.can_control));
}

void LinuxPlayerService::append_all_properties(
    brodbus::Message& msg, const std::string& iface) const {
    if (msg.open_container('a', "{sv}") < 0) return;
    if (iface.empty() || iface == kIfaceRoot) {
        append_root_properties_entries(msg);
    }
    if (iface.empty() || iface == kIfacePlayer) {
        append_player_properties_entries(msg);
    }
    msg.close_container();
}

bool LinuxPlayerService::handle_method_call(brodbus::Message& msg) {
    const std::string iface = msg.get_interface();
    const std::string member = msg.get_member();

    if ((iface == kIfaceIntrospectable || iface.empty()) && member == "Introspect") {
        auto rep = msg.new_method_return();
        rep.append_string(kIntrospectionXml);
        bus_->send(rep);
        bus_->flush();
        return true;
    }

    if (iface == kIfaceProperties ||
        (iface.empty() && (member == "Get" || member == "GetAll" || member == "Set"))) {
        if (member == "Get") {
            std::string target_iface, prop;
            if (!msg.read_string(&target_iface) || !msg.read_string(&prop)) {
                auto err = msg.new_method_error(
                    "org.freedesktop.DBus.Error.InvalidArgs", "Invalid Get arguments");
                bus_->send(err);
                bus_->flush();
                return true;
            }
            auto rep = msg.new_method_return();
            if (append_property_variant(rep, target_iface, prop)) {
                bus_->send(rep);
            } else {
                auto err = msg.new_method_error(
                    "org.freedesktop.DBus.Error.UnknownProperty",
                    "Unknown property: " + prop);
                bus_->send(err);
            }
            bus_->flush();
            return true;
        }

        if (member == "GetAll") {
            std::string target_iface;
            msg.read_string(&target_iface);
            auto rep = msg.new_method_return();
            append_all_properties(rep, target_iface);
            bus_->send(rep);
            bus_->flush();
            return true;
        }

        if (member == "Set") {
            std::string target_iface, prop;
            brodbus::PropertyValue val;
            if (!msg.read_string(&target_iface) || !msg.read_string(&prop) ||
                !msg.read_variant(&val)) {
                auto err = msg.new_method_error(
                    "org.freedesktop.DBus.Error.InvalidArgs", "Invalid Set arguments");
                bus_->send(err);
                bus_->flush();
                return true;
            }

            if ((target_iface == kIfacePlayer || target_iface.empty()) && prop == "LoopStatus") {
                LoopStatus ls = loop_status_from_string(val.as_string());
                set_loop_status(ls);
                notify_set_loop_status(ls);
                auto rep = msg.new_method_return();
                bus_->send(rep);
                bus_->flush();
                return true;
            }
            if ((target_iface == kIfacePlayer || target_iface.empty()) && prop == "Rate") {
                double r = val.as_double(1.0);
                set_rate(r);
                notify_set_rate(r);
                auto rep = msg.new_method_return();
                bus_->send(rep);
                bus_->flush();
                return true;
            }
            if ((target_iface == kIfacePlayer || target_iface.empty()) && prop == "Shuffle") {
                bool shuf = val.as_bool();
                set_shuffle(shuf);
                notify_set_shuffle(shuf);
                auto rep = msg.new_method_return();
                bus_->send(rep);
                bus_->flush();
                return true;
            }
            if ((target_iface == kIfacePlayer || target_iface.empty()) && prop == "Volume") {
                double vol = std::max(0.0, val.as_double(1.0));
                set_volume(vol);
                notify_set_volume(vol);
                auto rep = msg.new_method_return();
                bus_->send(rep);
                bus_->flush();
                return true;
            }
            if ((target_iface == kIfaceRoot || target_iface.empty()) && prop == "Fullscreen") {
                bool fs = val.as_bool();
                set_fullscreen(fs);
                notify_set_fullscreen(fs);
                auto rep = msg.new_method_return();
                bus_->send(rep);
                bus_->flush();
                return true;
            }

            auto err = msg.new_method_error(
                "org.freedesktop.DBus.Error.PropertyReadOnly",
                "Property is read-only or unknown: " + prop);
            bus_->send(err);
            bus_->flush();
            return true;
        }
    }

    if (iface == kIfaceRoot || iface.empty()) {
        if (member == "Raise") {
            notify_raise();
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "Quit") {
            notify_quit();
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
    }

    if (iface == kIfacePlayer || iface.empty()) {
        if (member == "Play") {
            notify_play();
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "Pause") {
            notify_pause();
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "PlayPause") {
            notify_play_pause();
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "Stop") {
            notify_stop();
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "Next") {
            notify_next();
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "Previous") {
            notify_previous();
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "Seek") {
            int64_t offset_usec = 0;
            msg.read_int64(&offset_usec);
            notify_seek(offset_usec);
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "SetPosition") {
            std::string track_id;
            char t = 0;
            msg.peek_type(&t, nullptr);
            if (t == 'o') {
                msg.read_object_path(&track_id);
            } else {
                msg.read_string(&track_id);
            }
            int64_t pos_usec = 0;
            msg.read_int64(&pos_usec);
            notify_set_position(normalize_incoming_track_id(track_id), pos_usec);
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
        if (member == "OpenUri") {
            std::string uri;
            msg.read_string(&uri);
            notify_open_uri(uri);
            auto rep = msg.new_method_return();
            bus_->send(rep);
            bus_->flush();
            return true;
        }
    }

    return false;
}

void LinuxPlayerService::emit_property_changed(
    const std::string& iface, const std::string& prop, const brodbus::PropertyValue& val) {
    std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
    if (!bus_ || !bus_->is_valid() || !name_acquired_.load()) return;

    bus_->emit_signal(
        kMprisObjectPath, kIfaceProperties, "PropertiesChanged",
        [&](brodbus::Message& m) {
            m.append_string(iface);
            m.open_container('a', "{sv}");
            append_dict_entry(m, prop, val);
            m.close_container();
            m.open_container('a', "s");
            m.close_container();
        });
    bus_->flush();
}

void LinuxPlayerService::emit_metadata_changed(const MediaMetadata& meta) {
    std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
    if (!bus_ || !bus_->is_valid() || !name_acquired_.load()) return;

    bus_->emit_signal(
        kMprisObjectPath, kIfaceProperties, "PropertiesChanged",
        [&](brodbus::Message& m) {
            m.append_string(kIfacePlayer);
            m.open_container('a', "{sv}");
            if (m.open_container('e', "sv") >= 0) {
                m.append_string("Metadata");
                append_metadata_variant(m, meta);
                m.close_container();
            }
            m.close_container();
            m.open_container('a', "s");
            m.close_container();
        });
    bus_->flush();
}

void LinuxPlayerService::emit_capabilities_changed(
    const PlayerCapabilities& old_caps, const PlayerCapabilities& new_caps) {
    std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
    if (!bus_ || !bus_->is_valid() || !name_acquired_.load()) return;

    const bool player_caps_changed =
        old_caps.can_control != new_caps.can_control ||
        old_caps.can_play != new_caps.can_play ||
        old_caps.can_pause != new_caps.can_pause ||
        old_caps.can_seek != new_caps.can_seek ||
        old_caps.can_go_next != new_caps.can_go_next ||
        old_caps.can_go_previous != new_caps.can_go_previous;

    const bool root_caps_changed =
        old_caps.can_quit != new_caps.can_quit ||
        old_caps.can_raise != new_caps.can_raise ||
        old_caps.can_set_fullscreen != new_caps.can_set_fullscreen;

    if (player_caps_changed) {
        bus_->emit_signal(
            kMprisObjectPath, kIfaceProperties, "PropertiesChanged",
            [&](brodbus::Message& m) {
                m.append_string(kIfacePlayer);
                m.open_container('a', "{sv}");
                append_dict_entry(m, "CanControl", brodbus::PropertyValue(new_caps.can_control));
                append_dict_entry(m, "CanPlay", brodbus::PropertyValue(new_caps.can_play));
                append_dict_entry(m, "CanPause", brodbus::PropertyValue(new_caps.can_pause));
                append_dict_entry(m, "CanSeek", brodbus::PropertyValue(new_caps.can_seek));
                append_dict_entry(m, "CanGoNext", brodbus::PropertyValue(new_caps.can_go_next));
                append_dict_entry(m, "CanGoPrevious", brodbus::PropertyValue(new_caps.can_go_previous));
                m.close_container();
                m.open_container('a', "s");
                m.close_container();
            });
    }

    if (root_caps_changed) {
        bus_->emit_signal(
            kMprisObjectPath, kIfaceProperties, "PropertiesChanged",
            [&](brodbus::Message& m) {
                m.append_string(kIfaceRoot);
                m.open_container('a', "{sv}");
                append_dict_entry(m, "CanQuit", brodbus::PropertyValue(new_caps.can_quit));
                append_dict_entry(m, "CanRaise", brodbus::PropertyValue(new_caps.can_raise));
                append_dict_entry(m, "CanSetFullscreen", brodbus::PropertyValue(new_caps.can_set_fullscreen));
                m.close_container();
                m.open_container('a', "s");
                m.close_container();
            });
    }

    if (player_caps_changed || root_caps_changed) {
        bus_->flush();
    }
}

void LinuxPlayerService::set_identity(std::string new_identity) {
    if (identity() == new_identity) return;
    PlayerService::set_identity(new_identity);
    emit_property_changed(kIfaceRoot, "Identity", brodbus::PropertyValue(new_identity));
}

void LinuxPlayerService::set_desktop_entry(std::string new_desktop_entry) {
    if (desktop_entry() == new_desktop_entry) return;
    PlayerService::set_desktop_entry(new_desktop_entry);
    emit_property_changed(kIfaceRoot, "DesktopEntry", brodbus::PropertyValue(new_desktop_entry));
}

void LinuxPlayerService::set_playback_status(PlaybackStatus status) {
    if (playback_status() == status) return;
    PlayerService::set_playback_status(status);
    emit_property_changed(
        kIfacePlayer, "PlaybackStatus", brodbus::PropertyValue(std::string(to_string(status))));
}

void LinuxPlayerService::set_loop_status(LoopStatus status) {
    if (loop_status() == status) return;
    PlayerService::set_loop_status(status);
    emit_property_changed(
        kIfacePlayer, "LoopStatus", brodbus::PropertyValue(std::string(to_string(status))));
}

void LinuxPlayerService::set_shuffle_status(ShuffleStatus status) {
    if (shuffle_status() == status) return;
    PlayerService::set_shuffle_status(status);
    emit_property_changed(
        kIfacePlayer, "Shuffle", brodbus::PropertyValue(shuffle_status_to_bool(status)));
}

void LinuxPlayerService::set_volume(double new_volume) {
    double clamped = std::max(0.0, new_volume);
    if (std::abs(volume() - clamped) < 0.0001) return;
    PlayerService::set_volume(clamped);
    emit_property_changed(kIfacePlayer, "Volume", brodbus::PropertyValue(clamped));
}

void LinuxPlayerService::set_rate(double new_rate) {
    if (std::abs(rate() - new_rate) < 0.0001) return;
    PlayerService::set_rate(new_rate);
    emit_property_changed(kIfacePlayer, "Rate", brodbus::PropertyValue(new_rate));
}

void LinuxPlayerService::set_rate_range(double min_rate, double max_rate) {
    PlayerService::set_rate_range(min_rate, max_rate);
    emit_property_changed(kIfacePlayer, "MinimumRate", brodbus::PropertyValue(min_rate));
    emit_property_changed(kIfacePlayer, "MaximumRate", brodbus::PropertyValue(max_rate));
}

void LinuxPlayerService::set_fullscreen(bool fs) {
    if (fullscreen() == fs) return;
    PlayerService::set_fullscreen(fs);
    emit_property_changed(kIfaceRoot, "Fullscreen", brodbus::PropertyValue(fs));
}

void LinuxPlayerService::set_capabilities(const PlayerCapabilities& caps) {
    PlayerCapabilities old_caps = capabilities();
    if (old_caps == caps) return;
    PlayerService::set_capabilities(caps);
    emit_capabilities_changed(old_caps, caps);
}

void LinuxPlayerService::set_metadata(MediaMetadata new_metadata) {
    PlayerService::set_metadata(std::move(new_metadata));
    emit_metadata_changed(metadata());
}

void LinuxPlayerService::set_supported_uri_schemes(std::vector<std::string> schemes) {
    PlayerService::set_supported_uri_schemes(schemes);
    emit_property_changed(kIfaceRoot, "SupportedUriSchemes", brodbus::PropertyValue(schemes));
}

void LinuxPlayerService::set_supported_mime_types(std::vector<std::string> mime_types) {
    PlayerService::set_supported_mime_types(mime_types);
    emit_property_changed(kIfaceRoot, "SupportedMimeTypes", brodbus::PropertyValue(mime_types));
}

void LinuxPlayerService::emit_seeked(int64_t position_usec) {
    PlayerService::emit_seeked(position_usec);
    std::lock_guard<std::recursive_mutex> lock(bus_mutex_);
    if (!bus_ || !bus_->is_valid() || !name_acquired_.load()) return;

    bus_->emit_signal(
        kMprisObjectPath, kIfacePlayer, "Seeked",
        [&](brodbus::Message& m) {
            m.append_int64(position_usec);
        });
    bus_->flush();
}

// Platform factory functions
std::unique_ptr<PlayerService> create_linux_player_service(
    const PlayerServiceConfig& config, PlayerServiceCallbacks callbacks) {
    std::string err;
    auto bus = brodbus::Bus::open_user(&err);
    if (!bus || !bus->is_valid()) return nullptr;
    auto svc = std::make_unique<LinuxPlayerService>(
        std::shared_ptr<brodbus::Bus>(std::move(bus)), config, std::move(callbacks));
    if (!svc->is_connected()) return nullptr;
    return svc;
}

std::unique_ptr<PlayerService> create_linux_player_service_with_bus(
    std::shared_ptr<brodbus::Bus> bus,
    const PlayerServiceConfig& config,
    PlayerServiceCallbacks callbacks) {
    if (!bus || !bus->is_valid()) return nullptr;
    auto svc = std::make_unique<LinuxPlayerService>(
        std::move(bus), config, std::move(callbacks));
    if (!svc->is_connected()) return nullptr;
    return svc;
}

std::unique_ptr<PlayerService> create_linux_player_service_with_address(
    const std::string& address,
    const PlayerServiceConfig& config,
    PlayerServiceCallbacks callbacks) {
    if (address.empty()) return nullptr;
    std::string err;
    auto bus = brodbus::Bus::open_address(address, &err);
    if (!bus || !bus->is_valid()) return nullptr;
    auto svc = std::make_unique<LinuxPlayerService>(
        std::shared_ptr<brodbus::Bus>(std::move(bus)), config, std::move(callbacks));
    if (!svc->is_connected()) return nullptr;
    return svc;
}

}  // namespace brompris

#endif  // BROMPRIS_HAS_BRODBUS && __linux__

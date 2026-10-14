#include "brompris/player_service.h"

#include <algorithm>
#include <cctype>

namespace brompris {

#if defined(__linux__)
std::unique_ptr<PlayerService> create_linux_player_service(
    const PlayerServiceConfig& config, PlayerServiceCallbacks callbacks);
#if defined(BROMPRIS_HAS_BRODBUS)
std::unique_ptr<PlayerService> create_linux_player_service_with_bus(
    std::shared_ptr<brodbus::Bus> bus,
    const PlayerServiceConfig& config,
    PlayerServiceCallbacks callbacks);
std::unique_ptr<PlayerService> create_linux_player_service_with_address(
    const std::string& address,
    const PlayerServiceConfig& config,
    PlayerServiceCallbacks callbacks);
#endif
#elif defined(_WIN32)
std::unique_ptr<PlayerService> create_windows_player_service(
    const PlayerServiceConfig& config, PlayerServiceCallbacks callbacks);
#elif defined(__APPLE__)
std::unique_ptr<PlayerService> create_mac_player_service(
    const PlayerServiceConfig& config, PlayerServiceCallbacks callbacks);
#endif

namespace {

void normalize_metadata_fields(MediaMetadata& meta) {
    if (meta.artists.empty() && !meta.artist.empty()) {
        meta.artists.push_back(meta.artist);
    } else if (meta.artist.empty() && !meta.artists.empty()) {
        meta.artist = meta.artists.front();
    }

    if (meta.album_artists.empty() && !meta.album_artist.empty()) {
        meta.album_artists.push_back(meta.album_artist);
    } else if (meta.album_artist.empty() && !meta.album_artists.empty()) {
        meta.album_artist = meta.album_artists.front();
    }
}

std::string sanitize_bus_suffix(const std::string& raw) {
    if (raw.empty()) return "player";

    std::string out;
    out.reserve(raw.size());
    bool segment_start = true;
    for (char c : raw) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (c == '.') {
            if (!segment_start) {
                out.push_back('.');
                segment_start = true;
            }
        } else if (std::isalpha(uc) || c == '_') {
            out.push_back(c);
            segment_start = false;
        } else if (std::isdigit(uc) || c == '-') {
            if (segment_start) {
                out.push_back('_');
            }
            out.push_back(c);
            segment_start = false;
        } else {
            out.push_back('_');
            segment_start = false;
        }
    }

    while (!out.empty() && out.back() == '.') {
        out.pop_back();
    }
    return out.empty() ? "player" : out;
}

}  // namespace

std::string PlayerService::normalize_bus_name(const std::string& name) {
    constexpr std::string_view kPrefix = "org.mpris.MediaPlayer2.";
    if (name.rfind(kPrefix, 0) == 0 && name.size() > kPrefix.size()) {
        return std::string(kPrefix) + sanitize_bus_suffix(name.substr(kPrefix.size()));
    }
    return std::string(kPrefix) + sanitize_bus_suffix(name);
}

PlayerService::PlayerService(const PlayerServiceConfig& config,
                             PlayerServiceCallbacks callbacks)
    : model_(normalize_bus_name(config.name)),
      name_(config.name.empty() ? "player" : config.name),
      supported_uri_schemes_(config.supported_uri_schemes),
      supported_mime_types_(config.supported_mime_types),
      minimum_rate_(config.minimum_rate),
      maximum_rate_(config.maximum_rate),
      running_(true),
      callbacks_(std::move(callbacks)) {
    model_.set_identity(config.identity.empty() ? name_ : config.identity);
    model_.set_desktop_entry(config.desktop_entry);
    model_.set_capabilities(config.capabilities);
    model_.set_loop_status(config.loop_status);
    model_.set_shuffle_status(config.shuffle_status);
    model_.set_volume(config.volume);
    model_.set_rate(config.rate);

    MediaMetadata meta = config.metadata;
    normalize_metadata_fields(meta);
    model_.set_metadata(std::move(meta));
    model_.set_position(config.position_usec);
    model_.set_playback_status(config.playback_status);
}

PlayerService::~PlayerService() {
    running_ = false;
}

std::string PlayerService::id() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.id();
}

std::string PlayerService::name() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return name_;
}

std::string PlayerService::identity() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.identity();
}

std::string PlayerService::desktop_entry() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.desktop_entry();
}

PlaybackStatus PlayerService::playback_status() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.playback_status();
}

LoopStatus PlayerService::loop_status() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.loop_status();
}

ShuffleStatus PlayerService::shuffle_status() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.shuffle_status();
}

double PlayerService::volume() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.volume();
}

double PlayerService::rate() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.rate();
}

double PlayerService::minimum_rate() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return minimum_rate_;
}

double PlayerService::maximum_rate() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return maximum_rate_;
}

bool PlayerService::fullscreen() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return fullscreen_;
}

MediaMetadata PlayerService::metadata() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.metadata();
}

PlayerCapabilities PlayerService::capabilities() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.capabilities();
}

int64_t PlayerService::position_usec() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.estimated_position_usec();
}

double PlayerService::position_seconds() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return model_.estimated_position_seconds();
}

std::vector<std::string> PlayerService::supported_uri_schemes() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return supported_uri_schemes_;
}

std::vector<std::string> PlayerService::supported_mime_types() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return supported_mime_types_;
}

void PlayerService::set_bus_name_internal(std::string bus_name) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_id(std::move(bus_name));
}

std::string PlayerService::normalize_incoming_track_id(const std::string& incoming_track_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const std::string& cur = model_.metadata().track_id;
    if (cur.empty()) {
        if (incoming_track_id == "/org/mpris/MediaPlayer2/TrackList/NoTrack") {
            return "";
        }
        return incoming_track_id;
    }
    if (incoming_track_id == cur) {
        return cur;
    }
    constexpr std::string_view kTrackPrefix = "/org/mpris/MediaPlayer2/TrackList/";
    if (incoming_track_id.rfind(kTrackPrefix, 0) == 0) {
        std::string stripped = incoming_track_id.substr(kTrackPrefix.size());
        if (stripped == cur) {
            return cur;
        }
        // Check if cur was sanitized when sent over D-Bus
        std::string sanitized_cur;
        sanitized_cur.reserve(cur.size());
        for (char c : cur) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc) || c == '_' || c == '/') {
                sanitized_cur.push_back(c);
            } else {
                sanitized_cur.push_back('_');
            }
        }
        if (stripped == sanitized_cur || incoming_track_id == sanitized_cur) {
            return cur;
        }
    }
    return incoming_track_id;
}

void PlayerService::set_identity(std::string identity) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_identity(std::move(identity));
}

void PlayerService::set_desktop_entry(std::string desktop_entry) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_desktop_entry(std::move(desktop_entry));
}

void PlayerService::set_playback_status(PlaybackStatus status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_playback_status(status);
}

void PlayerService::set_loop_status(LoopStatus status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_loop_status(status);
}

void PlayerService::set_shuffle_status(ShuffleStatus status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_shuffle_status(status);
}

void PlayerService::set_volume(double volume) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_volume(volume);
}

void PlayerService::set_rate(double rate) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_rate(rate);
}

void PlayerService::set_rate_range(double min_rate, double max_rate) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    minimum_rate_ = min_rate;
    maximum_rate_ = max_rate;
}

void PlayerService::set_fullscreen(bool fullscreen) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    fullscreen_ = fullscreen;
}

void PlayerService::set_capabilities(const PlayerCapabilities& caps) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_capabilities(caps);
}

void PlayerService::set_position(int64_t position_usec) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_position(position_usec);
}

void PlayerService::set_metadata(MediaMetadata metadata) {
    normalize_metadata_fields(metadata);
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_metadata(std::move(metadata));
}

void PlayerService::set_supported_uri_schemes(std::vector<std::string> schemes) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    supported_uri_schemes_ = std::move(schemes);
}

void PlayerService::set_supported_mime_types(std::vector<std::string> mime_types) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    supported_mime_types_ = std::move(mime_types);
}

void PlayerService::emit_seeked(int64_t position_usec) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    model_.set_position(position_usec);
}

void PlayerService::set_callbacks(PlayerServiceCallbacks callbacks) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_ = std::move(callbacks);
}

void PlayerService::on_play(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_play = std::move(cb);
}

void PlayerService::on_pause(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_pause = std::move(cb);
}

void PlayerService::on_play_pause(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_play_pause = std::move(cb);
}

void PlayerService::on_stop(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_stop = std::move(cb);
}

void PlayerService::on_next(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_next = std::move(cb);
}

void PlayerService::on_previous(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_previous = std::move(cb);
}

void PlayerService::on_seek(std::function<void(int64_t)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_seek = std::move(cb);
}

void PlayerService::on_set_position(std::function<void(const std::string&, int64_t)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_set_position = std::move(cb);
}

void PlayerService::on_open_uri(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_open_uri = std::move(cb);
}

void PlayerService::on_raise(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_raise = std::move(cb);
}

void PlayerService::on_quit(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_quit = std::move(cb);
}

void PlayerService::on_set_loop_status(std::function<void(LoopStatus)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_set_loop_status = std::move(cb);
}

void PlayerService::on_set_shuffle(std::function<void(bool)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_set_shuffle = std::move(cb);
}

void PlayerService::on_set_shuffle_status(std::function<void(ShuffleStatus)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_set_shuffle_status = std::move(cb);
}

void PlayerService::on_set_volume(std::function<void(double)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_set_volume = std::move(cb);
}

void PlayerService::on_set_rate(std::function<void(double)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_set_rate = std::move(cb);
}

void PlayerService::on_set_fullscreen(std::function<void(bool)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.on_set_fullscreen = std::move(cb);
}

void PlayerService::add_observer(PlayerServiceObserver* observer) {
    if (!observer) return;
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void PlayerService::remove_observer(PlayerServiceObserver* observer) {
    if (!observer) return;
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    auto it = std::remove(observers_.begin(), observers_.end(), observer);
    observers_.erase(it, observers_.end());
}

bool PlayerService::start() {
    running_ = true;
    return true;
}

void PlayerService::stop() {
    running_ = false;
}

void PlayerService::notify_play() {
    std::function<void()> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_play;
        obs = observers_;
    }
    if (cb) cb();
    for (auto* o : obs) o->on_play();
}

void PlayerService::notify_pause() {
    std::function<void()> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_pause;
        obs = observers_;
    }
    if (cb) cb();
    for (auto* o : obs) o->on_pause();
}

void PlayerService::notify_play_pause() {
    std::function<void()> cb;
    std::function<void()> fallback_cb;
    std::vector<PlayerServiceObserver*> obs;
    bool playing = (playback_status() == PlaybackStatus::Playing);
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_play_pause;
        fallback_cb = playing ? callbacks_.on_pause : callbacks_.on_play;
        obs = observers_;
    }
    if (cb) {
        cb();
    } else if (fallback_cb) {
        fallback_cb();
    }
    for (auto* o : obs) o->on_play_pause();
}

void PlayerService::notify_stop() {
    std::function<void()> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_stop;
        obs = observers_;
    }
    if (cb) cb();
    for (auto* o : obs) o->on_stop();
}

void PlayerService::notify_next() {
    std::function<void()> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_next;
        obs = observers_;
    }
    if (cb) cb();
    for (auto* o : obs) o->on_next();
}

void PlayerService::notify_previous() {
    std::function<void()> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_previous;
        obs = observers_;
    }
    if (cb) cb();
    for (auto* o : obs) o->on_previous();
}

void PlayerService::notify_seek(int64_t offset_usec) {
    std::function<void(int64_t)> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_seek;
        obs = observers_;
    }
    if (cb) cb(offset_usec);
    for (auto* o : obs) o->on_seek(offset_usec);
}

void PlayerService::notify_set_position(const std::string& track_id, int64_t position_usec) {
    std::function<void(const std::string&, int64_t)> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_set_position;
        obs = observers_;
    }
    if (cb) cb(track_id, position_usec);
    for (auto* o : obs) o->on_set_position(track_id, position_usec);
}

void PlayerService::notify_open_uri(const std::string& uri) {
    std::function<void(const std::string&)> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_open_uri;
        obs = observers_;
    }
    if (cb) cb(uri);
    for (auto* o : obs) o->on_open_uri(uri);
}

void PlayerService::notify_raise() {
    std::function<void()> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_raise;
        obs = observers_;
    }
    if (cb) cb();
    for (auto* o : obs) o->on_raise();
}

void PlayerService::notify_quit() {
    std::function<void()> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_quit;
        obs = observers_;
    }
    if (cb) cb();
    for (auto* o : obs) o->on_quit();
}

void PlayerService::notify_set_loop_status(LoopStatus status) {
    std::function<void(LoopStatus)> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_set_loop_status;
        obs = observers_;
    }
    if (cb) cb(status);
    for (auto* o : obs) o->on_set_loop_status(status);
}

void PlayerService::notify_set_shuffle(bool shuffle) {
    std::function<void(bool)> cb_bool;
    std::function<void(ShuffleStatus)> cb_status;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb_bool = callbacks_.on_set_shuffle;
        cb_status = callbacks_.on_set_shuffle_status;
        obs = observers_;
    }
    if (cb_bool) cb_bool(shuffle);
    if (cb_status) cb_status(shuffle_status_from_bool(shuffle));
    for (auto* o : obs) o->on_set_shuffle(shuffle);
}

void PlayerService::notify_set_volume(double volume) {
    std::function<void(double)> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_set_volume;
        obs = observers_;
    }
    if (cb) cb(volume);
    for (auto* o : obs) o->on_set_volume(volume);
}

void PlayerService::notify_set_rate(double rate) {
    std::function<void(double)> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_set_rate;
        obs = observers_;
    }
    if (cb) cb(rate);
    for (auto* o : obs) o->on_set_rate(rate);
}

void PlayerService::notify_set_fullscreen(bool fullscreen) {
    std::function<void(bool)> cb;
    std::vector<PlayerServiceObserver*> obs;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        cb = callbacks_.on_set_fullscreen;
        obs = observers_;
    }
    if (cb) cb(fullscreen);
    for (auto* o : obs) o->on_set_fullscreen(fullscreen);
}

std::unique_ptr<PlayerService> PlayerService::create(
    const PlayerServiceConfig& config, PlayerServiceCallbacks callbacks) {
#if defined(__linux__)
    auto svc = create_linux_player_service(config, callbacks);
    if (svc) return svc;
    return std::make_unique<PlayerService>(config, std::move(callbacks));
#elif defined(_WIN32)
    auto svc = create_windows_player_service(config, callbacks);
    if (svc) return svc;
    return std::make_unique<PlayerService>(config, std::move(callbacks));
#elif defined(__APPLE__)
    auto svc = create_mac_player_service(config, callbacks);
    if (svc) return svc;
    return std::make_unique<PlayerService>(config, std::move(callbacks));
#else
    return std::make_unique<PlayerService>(config, std::move(callbacks));
#endif
}

std::unique_ptr<PlayerService> PlayerService::create(
    const std::string& name,
    const std::string& identity,
    const std::string& desktop_entry) {
    PlayerServiceConfig cfg;
    cfg.name = name;
    cfg.identity = identity;
    cfg.desktop_entry = desktop_entry;
    return create(cfg);
}

#if defined(BROMPRIS_HAS_BRODBUS)
std::unique_ptr<PlayerService> PlayerService::create_with_bus(
    std::shared_ptr<brodbus::Bus> bus,
    const PlayerServiceConfig& config,
    PlayerServiceCallbacks callbacks) {
#if defined(__linux__)
    return create_linux_player_service_with_bus(std::move(bus), config, std::move(callbacks));
#else
    (void)bus;
    (void)config;
    (void)callbacks;
    return nullptr;
#endif
}

std::unique_ptr<PlayerService> PlayerService::create_with_address(
    const std::string& dbus_address,
    const PlayerServiceConfig& config,
    PlayerServiceCallbacks callbacks) {
#if defined(__linux__)
    return create_linux_player_service_with_address(dbus_address, config, std::move(callbacks));
#else
    (void)dbus_address;
    (void)config;
    (void)callbacks;
    return nullptr;
#endif
}
#endif

}  // namespace brompris

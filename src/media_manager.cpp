#include "brompris/mpris.h"

#include <algorithm>

namespace brompris {

// Forward declarations of platform manager factories
#if defined(__linux__)
std::unique_ptr<MediaManager> create_linux_media_manager();
#if defined(BROMPRIS_HAS_BRODBUS)
std::unique_ptr<MediaManager> create_linux_media_manager_with_bus(std::shared_ptr<brodbus::Bus> bus);
std::unique_ptr<MediaManager> create_linux_media_manager_with_address(const std::string& address);
#endif
#elif defined(_WIN32)
std::unique_ptr<MediaManager> create_windows_media_manager();
#elif defined(__APPLE__)
std::unique_ptr<MediaManager> create_mac_media_manager();
#endif

void MediaManager::add_observer(ManagerObserver* observer) {
    if (!observer) return;
    std::lock_guard<std::mutex> lock(observers_mutex_);
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void MediaManager::remove_observer(ManagerObserver* observer) {
    if (!observer) return;
    std::lock_guard<std::mutex> lock(observers_mutex_);
    auto it = std::remove(observers_.begin(), observers_.end(), observer);
    observers_.erase(it, observers_.end());
}

void MediaManager::notify_player_added(std::shared_ptr<MediaPlayer> player) {
    std::vector<ManagerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_player_added(player);
    }
}

void MediaManager::notify_player_removed(const std::string& player_id) {
    std::vector<ManagerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_player_removed(player_id);
    }
}

void MediaManager::notify_active_player_changed(std::shared_ptr<MediaPlayer> player) {
    std::vector<ManagerObserver*> copy;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        copy = observers_;
    }
    for (auto* obs : copy) {
        obs->on_active_player_changed(player);
    }
}

std::unique_ptr<MediaManager> MediaManager::create() {
#if defined(__linux__)
    return create_linux_media_manager();
#elif defined(_WIN32)
    return create_windows_media_manager();
#elif defined(__APPLE__)
    return create_mac_media_manager();
#else
    return nullptr;
#endif
}

#if defined(BROMPRIS_HAS_BRODBUS)
std::unique_ptr<MediaManager> MediaManager::create_with_bus(std::shared_ptr<brodbus::Bus> bus) {
#if defined(__linux__)
    return create_linux_media_manager_with_bus(std::move(bus));
#else
    (void)bus;
    return nullptr;
#endif
}

std::unique_ptr<MediaManager> MediaManager::create_with_address(const std::string& dbus_address) {
#if defined(__linux__)
    return create_linux_media_manager_with_address(dbus_address);
#else
    (void)dbus_address;
    return nullptr;
#endif
}
#endif

}  // namespace brompris

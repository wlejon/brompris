#pragma once

#include "brompris/player_service.h"

#if defined(BROMPRIS_HAS_BRODBUS)
#include "brodbus/bus.h"
#include "brodbus/message.h"
#include "brodbus/slot.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace brompris {

class LinuxPlayerService : public PlayerService {
public:
    LinuxPlayerService(std::shared_ptr<brodbus::Bus> bus,
                       const PlayerServiceConfig& config,
                       PlayerServiceCallbacks callbacks);
    ~LinuxPlayerService() override;

    bool is_running() const noexcept override { return running_ && name_acquired_.load(); }
    bool is_connected() const noexcept override { return bus_ && bus_->is_valid() && name_acquired_.load(); }

    void set_identity(std::string identity) override;
    void set_desktop_entry(std::string desktop_entry) override;
    void set_playback_status(PlaybackStatus status) override;
    void set_loop_status(LoopStatus status) override;
    void set_shuffle_status(ShuffleStatus status) override;
    void set_volume(double volume) override;
    void set_rate(double rate) override;
    void set_rate_range(double min_rate, double max_rate) override;
    void set_fullscreen(bool fullscreen) override;
    void set_capabilities(const PlayerCapabilities& caps) override;
    void set_metadata(MediaMetadata metadata) override;
    void set_supported_uri_schemes(std::vector<std::string> schemes) override;
    void set_supported_mime_types(std::vector<std::string> mime_types) override;

    void emit_seeked(int64_t position_usec) override;

    bool start() override;
    void stop() override;
    int process() override;
    int wait(uint64_t timeout_usec = 100000) override;

private:
    static int on_bus_message(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    bool handle_method_call(brodbus::Message& msg);

    bool append_property_variant(brodbus::Message& msg,
                                 const std::string& iface,
                                 const std::string& prop) const;
    void append_all_properties(brodbus::Message& msg, const std::string& iface) const;
    void append_root_properties_entries(brodbus::Message& msg) const;
    void append_player_properties_entries(brodbus::Message& msg) const;

    void emit_property_changed(const std::string& iface,
                               const std::string& prop,
                               const brodbus::PropertyValue& val);
    void emit_metadata_changed(const MediaMetadata& meta);
    void emit_capabilities_changed(const PlayerCapabilities& old_caps,
                                   const PlayerCapabilities& new_caps);

    void worker_loop();
    void wake_worker();

    std::shared_ptr<brodbus::Bus> bus_;
    mutable std::recursive_mutex bus_mutex_;
    brodbus::Slot object_slot_;
    std::string bus_name_;
    std::atomic<bool> name_acquired_{false};

    std::atomic<bool> worker_running_{false};
    int wakeup_fd_ = -1;
    std::thread worker_thread_;
};

}  // namespace brompris

#endif  // BROMPRIS_HAS_BRODBUS

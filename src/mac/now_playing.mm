#include "brompris/mpris.h"

#include <memory>
#include <vector>

namespace brompris {

#if defined(__APPLE__)

class MacPlaybackController : public PlaybackController {
public:
    MacPlaybackController() = default;
    ~MacPlaybackController() override = default;

    bool play() override { return false; }
    bool pause() override { return false; }
    bool play_pause() override { return false; }
    bool stop() override { return false; }
    bool next() override { return false; }
    bool previous() override { return false; }
    bool seek(int64_t offset_usec) override { (void)offset_usec; return false; }
    bool set_position(const std::string& track_id, int64_t position_usec) override {
        (void)track_id; (void)position_usec; return false;
    }
    bool set_volume(double volume) override { (void)volume; return false; }
    bool set_loop_status(LoopStatus status) override { (void)status; return false; }
    bool set_shuffle_status(ShuffleStatus status) override { (void)status; return false; }
    bool set_rate(double rate) override { (void)rate; return false; }
    bool open_uri(const std::string& uri) override { (void)uri; return false; }
    bool raise() override { return false; }
    bool quit() override { return false; }
};

class MacMediaManager : public MediaManager {
public:
    MacMediaManager() = default;
    ~MacMediaManager() override = default;

    std::vector<std::shared_ptr<MediaPlayer>> active_players() const override {
        return players_;
    }

    std::shared_ptr<MediaPlayer> active_player() const override {
        return players_.empty() ? nullptr : players_.front();
    }

    std::shared_ptr<MediaPlayer> find_player(const std::string& id) const override {
        for (const auto& p : players_) {
            if (p->id() == id) return p;
        }
        return nullptr;
    }

    int process() override { return 0; }
    int wait(uint64_t timeout_usec = 100000) override { (void)timeout_usec; return 0; }
    void update() override {}

private:
    std::vector<std::shared_ptr<MediaPlayer>> players_;
};

std::unique_ptr<MediaManager> create_mac_media_manager() {
    return std::make_unique<MacMediaManager>();
}

#else

std::unique_ptr<MediaManager> create_mac_media_manager() {
    return nullptr;
}

#endif  // __APPLE__

}  // namespace brompris

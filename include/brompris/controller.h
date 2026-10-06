#pragma once

#include "brompris/metadata.h"

#include <cstdint>
#include <string>

namespace brompris {

class PlaybackController {
public:
    virtual ~PlaybackController() = default;

    virtual bool play() = 0;
    virtual bool pause() = 0;
    virtual bool play_pause() = 0;
    virtual bool stop() = 0;
    virtual bool next() = 0;
    virtual bool previous() = 0;
    virtual bool seek(int64_t offset_usec) = 0;
    virtual bool set_position(const std::string& track_id, int64_t position_usec) = 0;
    virtual bool set_volume(double volume) = 0;
    virtual bool set_loop_status(LoopStatus status) = 0;
    virtual bool set_shuffle_status(ShuffleStatus status) = 0;
    virtual bool set_rate(double rate) = 0;
    virtual bool open_uri(const std::string& uri) = 0;
    virtual bool raise() = 0;
    virtual bool quit() = 0;
};

}  // namespace brompris

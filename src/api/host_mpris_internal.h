#pragma once

#include "embed/embed.h"
#include "brompris/metadata.h"
#include "brompris/mpris.h"
#include "brompris/observer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace brompris::api {

namespace ev = bronze::embed;
using Value = bronze::Value;

// Internal event types queued by observers
struct PlayerAddedEvent {
    std::string id;
    std::string identity;
    PlaybackStatus playback_status = PlaybackStatus::Stopped;
    PlayerCapabilities capabilities;
    double volume = 1.0;
    int64_t position_usec = 0;
};

struct PlayerRemovedEvent {
    std::string id;
};

struct ActivePlayerChangedEvent {
    std::string id;
    std::string identity;
    PlaybackStatus playback_status = PlaybackStatus::Stopped;
    PlayerCapabilities capabilities;
    double volume = 1.0;
    int64_t position_usec = 0;
    bool has_player = false;
};

struct PlaybackStatusEvent {
    std::string player_id;
    PlaybackStatus status = PlaybackStatus::Stopped;
};

struct MetadataEvent {
    std::string player_id;
    MediaMetadata metadata;
};

struct SeekedEvent {
    std::string player_id;
    int64_t position_usec = 0;
};

using MprisEventVariant = std::variant<
    PlayerAddedEvent,
    PlayerRemovedEvent,
    ActivePlayerChangedEvent,
    PlaybackStatusEvent,
    MetadataEvent,
    SeekedEvent
>;

Value ensureBroMpris();
void installNativeMpris(Value mprisObj);

void queueMprisEvent(MprisEventVariant evItem);
void drainMprisEvents();
void clearSubscriptions();
void clearPendingEvents();

void addMprisEventListener(const std::string& event, Value callback);
void removeMprisEventListener(const std::string& event, Value callback);
void dispatchMprisEvent(std::string_view event, Value payload);

Value buildPlayerObject(const MediaPlayer& player);
Value buildPlayerObjectFromSnapshot(const std::string& id,
                                   const std::string& identity,
                                   PlaybackStatus status,
                                   const PlayerCapabilities& caps,
                                   double volume,
                                   int64_t position_usec);
Value buildMetadataObject(const MediaMetadata& meta);

std::shared_ptr<brompris::MediaManager> activeManager();

} // namespace brompris::api

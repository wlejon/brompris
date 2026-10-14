#pragma once

#include "brompris/mpris.h"
#include "brompris/player_service.h"

#include <functional>
#include <memory>
#include <string>

namespace brompris::api {

using PlayerServiceFactory = std::function<std::unique_ptr<brompris::PlayerService>(
    const brompris::PlayerServiceConfig&, brompris::PlayerServiceCallbacks)>;

/// Mounts `bro.mpris` in the current Bronze realm.
void installMpris();

/// Processes manager updates and dispatches queued MPRIS events to JS listeners.
void tickMprisAsync();

/// Cleans up observers and shuts down MPRIS event dispatching.
void shutdownMprisAsync();

/// Sets the media manager used by the API (defaults to MediaManager::create()).
void setMediaManager(std::shared_ptr<brompris::MediaManager> mgr);

/// Gets the media manager currently used by the API.
std::shared_ptr<brompris::MediaManager> getMediaManager();

/// Sets a custom factory for PlayerService instances created via JS.
void setPlayerServiceFactory(PlayerServiceFactory factory);

/// Sets a custom D-Bus address used when creating default MediaManager and PlayerService instances.
void setDbusAddress(const std::string& address);

/// Gets the configured D-Bus address (empty if using default session bus).
std::string getDbusAddress();

} // namespace brompris::api

using brompris::api::installMpris;
using brompris::api::tickMprisAsync;
using brompris::api::shutdownMprisAsync;
using brompris::api::setMediaManager;
using brompris::api::getMediaManager;
using brompris::api::setPlayerServiceFactory;
using brompris::api::setDbusAddress;
using brompris::api::getDbusAddress;

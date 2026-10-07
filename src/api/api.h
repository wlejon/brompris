#pragma once

#include "brompris/mpris.h"

#include <memory>

namespace brompris::api {

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

} // namespace brompris::api

using brompris::api::installMpris;
using brompris::api::tickMprisAsync;
using brompris::api::shutdownMprisAsync;
using brompris::api::setMediaManager;
using brompris::api::getMediaManager;

#include "brompris/metadata.h"

#include <algorithm>

namespace brompris {

std::string_view to_string(PlaybackStatus status) noexcept {
    switch (status) {
        case PlaybackStatus::Playing:
            return "Playing";
        case PlaybackStatus::Paused:
            return "Paused";
        case PlaybackStatus::Stopped:
        default:
            return "Stopped";
    }
}

PlaybackStatus playback_status_from_string(std::string_view str) noexcept {
    if (str == "Playing" || str == "playing") {
        return PlaybackStatus::Playing;
    }
    if (str == "Paused" || str == "paused") {
        return PlaybackStatus::Paused;
    }
    return PlaybackStatus::Stopped;
}

std::string_view to_string(LoopStatus status) noexcept {
    switch (status) {
        case LoopStatus::Track:
            return "Track";
        case LoopStatus::Playlist:
            return "Playlist";
        case LoopStatus::None:
        default:
            return "None";
    }
}

LoopStatus loop_status_from_string(std::string_view str) noexcept {
    if (str == "Track" || str == "track") {
        return LoopStatus::Track;
    }
    if (str == "Playlist" || str == "playlist") {
        return LoopStatus::Playlist;
    }
    return LoopStatus::None;
}

std::string_view to_string(ShuffleStatus status) noexcept {
    switch (status) {
        case ShuffleStatus::On:
            return "On";
        case ShuffleStatus::Off:
        default:
            return "Off";
    }
}

ShuffleStatus shuffle_status_from_string(std::string_view str) noexcept {
    if (str == "On" || str == "on" || str == "true" || str == "1") {
        return ShuffleStatus::On;
    }
    return ShuffleStatus::Off;
}

ShuffleStatus shuffle_status_from_bool(bool shuffle) noexcept {
    return shuffle ? ShuffleStatus::On : ShuffleStatus::Off;
}

bool shuffle_status_to_bool(ShuffleStatus status) noexcept {
    return status == ShuffleStatus::On;
}

#if defined(BROMPRIS_HAS_BRODBUS)

MediaMetadata parse_mpris_metadata(const std::map<std::string, brodbus::PropertyValue>& dict) {
    MediaMetadata meta;

    for (const auto& [key, val] : dict) {
        if (key == "xesam:title") {
            meta.title = val.as_string();
        } else if (key == "xesam:artist") {
            if (val.is<std::vector<std::string>>()) {
                meta.artists = val.as_string_list();
                if (!meta.artists.empty()) {
                    meta.artist = meta.artists.front();
                }
            } else if (val.is<std::string>()) {
                meta.artist = val.as_string();
                meta.artists = {meta.artist};
            }
        } else if (key == "xesam:album") {
            meta.album = val.as_string();
        } else if (key == "xesam:albumArtist") {
            if (val.is<std::vector<std::string>>()) {
                auto list = val.as_string_list();
                if (!list.empty()) {
                    meta.album_artist = list.front();
                }
            } else if (val.is<std::string>()) {
                meta.album_artist = val.as_string();
            }
        } else if (key == "mpris:artUrl") {
            meta.album_art_url = val.as_string();
        } else if (key == "mpris:length") {
            if (val.is<int64_t>()) {
                meta.duration_usec = val.as_int64();
            } else if (val.is<uint64_t>()) {
                meta.duration_usec = static_cast<int64_t>(val.as_uint64());
            } else if (val.is<int32_t>()) {
                meta.duration_usec = val.as_int32();
            } else if (val.is<uint32_t>()) {
                meta.duration_usec = val.as_uint32();
            }
        } else if (key == "mpris:trackid") {
            if (val.is<brodbus::ObjectPath>()) {
                meta.track_id = val.as_object_path().str();
            } else {
                meta.track_id = val.as_string();
            }
        } else if (key == "xesam:genre") {
            if (val.is<std::vector<std::string>>()) {
                meta.genres = val.as_string_list();
            } else if (val.is<std::string>()) {
                meta.genres = {val.as_string()};
            }
        } else if (key == "xesam:trackNumber") {
            meta.track_number = val.as_int32();
        } else if (key == "xesam:discNumber") {
            meta.disc_number = val.as_int32();
        } else if (key == "xesam:userRating") {
            meta.user_rating = val.as_double();
        } else if (key == "xesam:url") {
            meta.url = val.as_string();
        } else {
            meta.extra[key] = val.as_string();
        }
    }

    return meta;
}

std::map<std::string, brodbus::PropertyValue> to_mpris_properties(const MediaMetadata& meta) {
    std::map<std::string, brodbus::PropertyValue> dict;

    if (!meta.title.empty()) {
        dict["xesam:title"] = brodbus::PropertyValue(meta.title);
    }
    if (!meta.artists.empty()) {
        dict["xesam:artist"] = brodbus::PropertyValue(meta.artists);
    } else if (!meta.artist.empty()) {
        dict["xesam:artist"] = brodbus::PropertyValue(std::vector<std::string>{meta.artist});
    }
    if (!meta.album.empty()) {
        dict["xesam:album"] = brodbus::PropertyValue(meta.album);
    }
    if (!meta.album_artist.empty()) {
        dict["xesam:albumArtist"] = brodbus::PropertyValue(std::vector<std::string>{meta.album_artist});
    }
    if (!meta.album_art_url.empty()) {
        dict["mpris:artUrl"] = brodbus::PropertyValue(meta.album_art_url);
    }
    if (meta.duration_usec > 0) {
        dict["mpris:length"] = brodbus::PropertyValue(meta.duration_usec);
    }
    if (!meta.track_id.empty()) {
        if (meta.track_id.front() == '/') {
            dict["mpris:trackid"] = brodbus::PropertyValue(brodbus::ObjectPath(meta.track_id));
        } else {
            dict["mpris:trackid"] = brodbus::PropertyValue(meta.track_id);
        }
    }
    if (!meta.genres.empty()) {
        dict["xesam:genre"] = brodbus::PropertyValue(meta.genres);
    }
    if (meta.track_number > 0) {
        dict["xesam:trackNumber"] = brodbus::PropertyValue(static_cast<int32_t>(meta.track_number));
    }
    if (meta.disc_number > 0) {
        dict["xesam:discNumber"] = brodbus::PropertyValue(static_cast<int32_t>(meta.disc_number));
    }
    if (meta.user_rating > 0.0) {
        dict["xesam:userRating"] = brodbus::PropertyValue(meta.user_rating);
    }
    if (!meta.url.empty()) {
        dict["xesam:url"] = brodbus::PropertyValue(meta.url);
    }

    for (const auto& [k, v] : meta.extra) {
        dict[k] = brodbus::PropertyValue(v);
    }

    return dict;
}

#endif  // BROMPRIS_HAS_BRODBUS

}  // namespace brompris

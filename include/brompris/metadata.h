#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#if defined(__has_include)
#if __has_include(<brodbus/types.h>)
#include <brodbus/types.h>
#define BROMPRIS_HAS_BRODBUS 1
#endif
#endif

namespace brompris {

enum class PlaybackStatus {
    Stopped,
    Playing,
    Paused
};

enum class LoopStatus {
    None,
    Track,
    Playlist
};

enum class ShuffleStatus {
    Off,
    On
};

std::string_view to_string(PlaybackStatus status) noexcept;
PlaybackStatus playback_status_from_string(std::string_view str) noexcept;

std::string_view to_string(LoopStatus status) noexcept;
LoopStatus loop_status_from_string(std::string_view str) noexcept;

std::string_view to_string(ShuffleStatus status) noexcept;
ShuffleStatus shuffle_status_from_string(std::string_view str) noexcept;
ShuffleStatus shuffle_status_from_bool(bool shuffle) noexcept;
bool shuffle_status_to_bool(ShuffleStatus status) noexcept;

struct MediaMetadata {
    std::string title;
    std::string artist;               // Primary artist
    std::vector<std::string> artists; // All artists
    std::string album;
    std::string album_artist;
    std::string album_art_url;
    int64_t duration_usec = 0;        // Microseconds (MPRIS mpris:length)
    std::string track_id;             // Track ID (object path or string)

    std::vector<std::string> genres;
    int track_number = 0;
    int disc_number = 0;
    double user_rating = 0.0;
    std::string url;
    std::map<std::string, std::string> extra;

    bool empty() const noexcept {
        return title.empty() && artist.empty() && album.empty() &&
               track_id.empty() && duration_usec == 0;
    }

    std::chrono::microseconds duration() const noexcept {
        return std::chrono::microseconds(duration_usec);
    }

    double duration_seconds() const noexcept {
        return duration_usec > 0 ? static_cast<double>(duration_usec) / 1'000'000.0 : 0.0;
    }

    bool operator==(const MediaMetadata& o) const = default;
};

#if defined(BROMPRIS_HAS_BRODBUS)
MediaMetadata parse_mpris_metadata(const std::map<std::string, brodbus::PropertyValue>& dict);
std::map<std::string, brodbus::PropertyValue> to_mpris_properties(const MediaMetadata& meta);
#endif

}  // namespace brompris

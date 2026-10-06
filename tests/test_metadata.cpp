#include "check.h"
#include "brompris/metadata.h"

using namespace brompris;

void test_enums() {
    // PlaybackStatus
    CHECK_EQ(to_string(PlaybackStatus::Playing), "Playing");
    CHECK_EQ(to_string(PlaybackStatus::Paused), "Paused");
    CHECK_EQ(to_string(PlaybackStatus::Stopped), "Stopped");

    CHECK_EQ(playback_status_from_string("Playing"), PlaybackStatus::Playing);
    CHECK_EQ(playback_status_from_string("playing"), PlaybackStatus::Playing);
    CHECK_EQ(playback_status_from_string("Paused"), PlaybackStatus::Paused);
    CHECK_EQ(playback_status_from_string("paused"), PlaybackStatus::Paused);
    CHECK_EQ(playback_status_from_string("Stopped"), PlaybackStatus::Stopped);
    CHECK_EQ(playback_status_from_string("unknown"), PlaybackStatus::Stopped);

    // LoopStatus
    CHECK_EQ(to_string(LoopStatus::None), "None");
    CHECK_EQ(to_string(LoopStatus::Track), "Track");
    CHECK_EQ(to_string(LoopStatus::Playlist), "Playlist");

    CHECK_EQ(loop_status_from_string("None"), LoopStatus::None);
    CHECK_EQ(loop_status_from_string("Track"), LoopStatus::Track);
    CHECK_EQ(loop_status_from_string("Playlist"), LoopStatus::Playlist);
    CHECK_EQ(loop_status_from_string("other"), LoopStatus::None);

    // ShuffleStatus
    CHECK_EQ(to_string(ShuffleStatus::Off), "Off");
    CHECK_EQ(to_string(ShuffleStatus::On), "On");

    CHECK_EQ(shuffle_status_from_bool(true), ShuffleStatus::On);
    CHECK_EQ(shuffle_status_from_bool(false), ShuffleStatus::Off);
    CHECK_EQ(shuffle_status_to_bool(ShuffleStatus::On), true);
    CHECK_EQ(shuffle_status_to_bool(ShuffleStatus::Off), false);
    CHECK_EQ(shuffle_status_from_string("on"), ShuffleStatus::On);
    CHECK_EQ(shuffle_status_from_string("off"), ShuffleStatus::Off);
}

void test_metadata_defaults() {
    MediaMetadata meta;
    CHECK(meta.empty());
    CHECK_EQ(meta.duration_usec, 0);
    CHECK_DOUBLE_EQ(meta.duration_seconds(), 0.0, 0.0001);
    CHECK_EQ(meta.duration().count(), 0);

    meta.title = "Song Title";
    CHECK(!meta.empty());

    meta.duration_usec = 180000000;  // 180 seconds (3 mins)
    CHECK_DOUBLE_EQ(meta.duration_seconds(), 180.0, 0.0001);
    CHECK_EQ(meta.duration().count(), 180000000);
}

#if defined(BROMPRIS_HAS_BRODBUS)
void test_mpris_metadata_parsing() {
    std::map<std::string, brodbus::PropertyValue> dict;
    dict["xesam:title"] = brodbus::PropertyValue("Comfortably Numb");
    dict["xesam:artist"] = brodbus::PropertyValue(std::vector<std::string>{"Pink Floyd"});
    dict["xesam:album"] = brodbus::PropertyValue("The Wall");
    dict["xesam:albumArtist"] = brodbus::PropertyValue(std::vector<std::string>{"Pink Floyd"});
    dict["mpris:artUrl"] = brodbus::PropertyValue("file:///home/user/music/the_wall.jpg");
    dict["mpris:length"] = brodbus::PropertyValue(int64_t(382000000));  // 382 sec
    dict["mpris:trackid"] = brodbus::PropertyValue(brodbus::ObjectPath("/org/mpris/MediaPlayer2/track/42"));
    dict["xesam:genre"] = brodbus::PropertyValue(std::vector<std::string>{"Progressive Rock"});
    dict["xesam:trackNumber"] = brodbus::PropertyValue(int32_t(6));
    dict["xesam:discNumber"] = brodbus::PropertyValue(int32_t(2));
    dict["xesam:userRating"] = brodbus::PropertyValue(0.95);
    dict["xesam:url"] = brodbus::PropertyValue("file:///home/user/music/comfortably_numb.flac");
    dict["custom:source"] = brodbus::PropertyValue("local-library");

    MediaMetadata meta = parse_mpris_metadata(dict);
    CHECK_EQ(meta.title, "Comfortably Numb");
    CHECK_EQ(meta.artist, "Pink Floyd");
    REQUIRE_EQ(meta.artists.size(), 1u);
    CHECK_EQ(meta.artists[0], "Pink Floyd");
    CHECK_EQ(meta.album, "The Wall");
    CHECK_EQ(meta.album_artist, "Pink Floyd");
    CHECK_EQ(meta.album_art_url, "file:///home/user/music/the_wall.jpg");
    CHECK_EQ(meta.duration_usec, 382000000);
    CHECK_DOUBLE_EQ(meta.duration_seconds(), 382.0, 0.001);
    CHECK_EQ(meta.track_id, "/org/mpris/MediaPlayer2/track/42");
    REQUIRE_EQ(meta.genres.size(), 1u);
    CHECK_EQ(meta.genres[0], "Progressive Rock");
    CHECK_EQ(meta.track_number, 6);
    CHECK_EQ(meta.disc_number, 2);
    CHECK_DOUBLE_EQ(meta.user_rating, 0.95, 0.001);
    CHECK_EQ(meta.url, "file:///home/user/music/comfortably_numb.flac");
    CHECK_EQ(meta.extra["custom:source"], "local-library");

    // Test round trip conversion
    auto serialized = to_mpris_properties(meta);
    MediaMetadata round_trip = parse_mpris_metadata(serialized);
    CHECK_EQ(round_trip.title, meta.title);
    CHECK_EQ(round_trip.artist, meta.artist);
    CHECK_EQ(round_trip.album, meta.album);
    CHECK_EQ(round_trip.duration_usec, meta.duration_usec);
    CHECK_EQ(round_trip.track_id, meta.track_id);
    CHECK_EQ(round_trip.track_number, meta.track_number);
    CHECK_EQ(round_trip.disc_number, meta.disc_number);
    CHECK_DOUBLE_EQ(round_trip.user_rating, meta.user_rating, 0.001);
}

void test_metadata_edge_cases() {
    // Single string artist instead of array
    {
        std::map<std::string, brodbus::PropertyValue> dict;
        dict["xesam:title"] = brodbus::PropertyValue("Test Track");
        dict["xesam:artist"] = brodbus::PropertyValue("Solo Artist");
        dict["mpris:length"] = brodbus::PropertyValue(uint64_t(60000000));
        dict["mpris:trackid"] = brodbus::PropertyValue("track-string-id");

        MediaMetadata meta = parse_mpris_metadata(dict);
        CHECK_EQ(meta.title, "Test Track");
        CHECK_EQ(meta.artist, "Solo Artist");
        REQUIRE_EQ(meta.artists.size(), 1u);
        CHECK_EQ(meta.artists[0], "Solo Artist");
        CHECK_EQ(meta.duration_usec, 60000000);
        CHECK_EQ(meta.track_id, "track-string-id");
    }

    // Multiple artists
    {
        std::map<std::string, brodbus::PropertyValue> dict;
        dict["xesam:artist"] = brodbus::PropertyValue(std::vector<std::string>{"Artist A", "Artist B"});
        MediaMetadata meta = parse_mpris_metadata(dict);
        CHECK_EQ(meta.artist, "Artist A");
        REQUIRE_EQ(meta.artists.size(), 2u);
        CHECK_EQ(meta.artists[1], "Artist B");
    }
}
#endif

int main() {
    test_enums();
    test_metadata_defaults();
#if defined(BROMPRIS_HAS_BRODBUS)
    test_mpris_metadata_parsing();
    test_metadata_edge_cases();
#endif
    return bstest::finish("test_metadata");
}

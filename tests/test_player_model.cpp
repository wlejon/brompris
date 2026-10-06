#include "check.h"
#include "brompris/player_model.h"

#include <chrono>
#include <thread>

using namespace brompris;
using namespace std::chrono_literals;

void test_model_initialization() {
    PlayerModel model("org.mpris.MediaPlayer2.test");
    CHECK_EQ(model.id(), "org.mpris.MediaPlayer2.test");
    CHECK_EQ(model.playback_status(), PlaybackStatus::Stopped);
    CHECK(model.is_stopped());
    CHECK(!model.is_playing());
    CHECK(!model.is_paused());
    CHECK_EQ(model.loop_status(), LoopStatus::None);
    CHECK_EQ(model.shuffle_status(), ShuffleStatus::Off);
    CHECK_DOUBLE_EQ(model.volume(), 1.0, 0.0001);
    CHECK_DOUBLE_EQ(model.rate(), 1.0, 0.0001);
    CHECK_EQ(model.estimated_position_usec(), 0);
    CHECK(!model.has_position());
}

void test_model_position_ticker() {
    PlayerModel model("test_ticker");
    MediaMetadata meta;
    meta.track_id = "track_1";
    meta.duration_usec = 10'000'000;  // 10 seconds
    model.set_metadata(meta);

    // Initial position set while Stopped
    model.set_position(1'000'000);  // 1.0s
    CHECK(model.has_position());
    CHECK_EQ(model.estimated_position_usec(), 1'000'000);

    // Sleep a bit while Stopped; position MUST NOT change
    std::this_thread::sleep_for(30ms);
    CHECK_EQ(model.estimated_position_usec(), 1'000'000);

    // Switch to Playing; position should advance
    model.set_playback_status(PlaybackStatus::Playing);
    CHECK(model.is_playing());

    std::this_thread::sleep_for(50ms);
    int64_t pos_after_50ms = model.estimated_position_usec();
    // 50ms = 50,000 usec. Allow reasonable scheduling tolerance [30ms..150ms]
    CHECK(pos_after_50ms >= 1'030'000);
    CHECK(pos_after_50ms <= 1'250'000);

    // Switch to Paused; position must freeze
    model.set_playback_status(PlaybackStatus::Paused);
    CHECK(model.is_paused());
    int64_t paused_pos = model.estimated_position_usec();

    std::this_thread::sleep_for(40ms);
    CHECK_EQ(model.estimated_position_usec(), paused_pos);

    // Resume Playing; position should continue from paused_pos
    model.set_playback_status(PlaybackStatus::Playing);
    std::this_thread::sleep_for(50ms);
    int64_t resumed_pos = model.estimated_position_usec();
    CHECK(resumed_pos >= paused_pos + 30'000);
}

void test_model_rate_scaling() {
    PlayerModel model("test_rate");
    MediaMetadata meta;
    meta.track_id = "t1";
    meta.duration_usec = 100'000'000;
    model.set_metadata(meta);
    model.set_position(0);
    model.set_rate(2.0);  // 2x playback speed
    model.set_playback_status(PlaybackStatus::Playing);

    std::this_thread::sleep_for(50ms);
    int64_t pos = model.estimated_position_usec();
    // 50ms at 2x is 100ms = 100,000 usec
    CHECK(pos >= 60'000);
}

void test_model_duration_clamp() {
    PlayerModel model("test_clamp");
    MediaMetadata meta;
    meta.track_id = "short_track";
    meta.duration_usec = 2'000'000;  // 2.0s
    model.set_metadata(meta);

    model.set_position(1'990'000);
    model.set_playback_status(PlaybackStatus::Playing);

    std::this_thread::sleep_for(50ms);
    CHECK_EQ(model.estimated_position_usec(), 2'000'000);
}

void test_model_track_change() {
    PlayerModel model("test_track_change");
    MediaMetadata meta1;
    meta1.track_id = "song_1";
    meta1.duration_usec = 5'000'000;
    model.set_metadata(meta1);
    model.set_position(3'000'000);
    model.set_playback_status(PlaybackStatus::Playing);

    CHECK_EQ(model.metadata().track_id, "song_1");
    CHECK(model.estimated_position_usec() >= 3'000'000);

    // Change to song 2: position should reset to 0
    MediaMetadata meta2;
    meta2.track_id = "song_2";
    meta2.duration_usec = 6'000'000;
    model.set_metadata(meta2);

    CHECK_EQ(model.metadata().track_id, "song_2");
    CHECK_EQ(model.base_position_usec(), 0);
}

void test_model_mutations() {
    PlayerModel model("test_mut");
    model.set_identity("VLC");
    model.set_desktop_entry("vlc");
    model.set_volume(0.85);
    model.set_loop_status(LoopStatus::Track);
    model.set_shuffle_status(ShuffleStatus::On);

    PlayerCapabilities caps;
    caps.can_play = true;
    caps.can_pause = true;
    caps.can_seek = true;
    model.set_capabilities(caps);

    CHECK_EQ(model.identity(), "VLC");
    CHECK_EQ(model.desktop_entry(), "vlc");
    CHECK_DOUBLE_EQ(model.volume(), 0.85, 0.001);
    CHECK_EQ(model.loop_status(), LoopStatus::Track);
    CHECK_EQ(model.shuffle_status(), ShuffleStatus::On);
    CHECK_EQ(model.capabilities(), caps);

    model.reset();
    CHECK_EQ(model.playback_status(), PlaybackStatus::Stopped);
    CHECK_EQ(model.loop_status(), LoopStatus::None);
    CHECK_EQ(model.shuffle_status(), ShuffleStatus::Off);
    CHECK_DOUBLE_EQ(model.volume(), 1.0, 0.001);
}

int main() {
    test_model_initialization();
    test_model_position_ticker();
    test_model_rate_scaling();
    test_model_duration_clamp();
    test_model_track_change();
    test_model_mutations();
    return bstest::finish("test_player_model");
}

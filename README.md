# brompris

[![CI](https://github.com/wlejon/brompris/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/brompris/actions/workflows/ci.yml)

A modern, reusable C++20 cross-platform media player interfacing library for the [bro](https://github.com/wlejon/bro) ecosystem.

`brompris` discovers running media players, monitors their playback status and metadata, tracks smooth playback position in real time, and exposes standardized transport controls:
- **Linux**: Full MPRIS2 (`org.mpris.MediaPlayer2.*`) client backed by [brodbus](https://github.com/wlejon/brodbus).
- **Windows**: not implemented yet. The library builds, and the manager reports no players; a backend on the System Media Transport Controls (WinRT SMTC) is planned.
- **macOS**: not implemented yet. The library builds, and the manager reports no players; a backend on MediaRemote / Now Playing is planned.

---

## Where It Sits

Part of the **[bro](https://github.com/wlejon/bro)** desktop ecosystem (see [ecosystem architecture](https://github.com/wlejon/bro/blob/main/docs/ecosystem.md)).

Within the desktop stack, `brompris` sits alongside `brosys`, `broportal`, and `brodbus`:
- **[brodbus](https://github.com/wlejon/brodbus)**: Low-level D-Bus message serialization and bus lifecycle.
- **`brompris`**: High-level media player discovery, metadata parsing, position tracking, and remote transport control.

---

## Core Architecture

`brompris` is decomposed into focused modules:

```
include/brompris/
├── brompris.h         # Umbrella header
├── metadata.h         # MediaMetadata, PlaybackStatus, LoopStatus, ShuffleStatus
├── controller.h       # PlaybackController abstract transport interface
├── observer.h         # PlayerObserver & ManagerObserver interfaces
├── player_model.h     # Normalized player state & position ticker
└── mpris.h            # MediaPlayer & MediaManager discovery coordinators

src/
├── metadata.cpp       # MediaMetadata codecs & MPRIS dictionary parser
├── player_model.cpp   # State mutations & playback position interpolator
├── media_player.cpp   # MediaPlayer instance & observer dispatch
├── media_manager.cpp  # Manager lifecycle & active player heuristics
├── linux/
│   ├── mpris_client.h # Linux MPRIS client definitions
│   └── mpris_client.cpp # org.mpris.MediaPlayer2.* discovery & signals via brodbus
├── win/
│   └── smtc.cpp       # Windows WinRT SMTC backend
└── mac/
    └── now_playing.mm # macOS NowPlaying backend
```

### Key Components

1. **`MediaMetadata`**:
   Normalized container for track metadata (`title`, `artist`, `artists`, `album`, `album_artist`, `album_art_url`, `duration_usec`, `track_id`, `track_number`, `disc_number`, `genres`, `user_rating`, `url`). Includes bi-directional codecs for MPRIS2 `a{sv}` dictionaries.

2. **`PlayerModel` & Position Ticker**:
   Media IPC protocols (MPRIS2, SMTC) do not broadcast continuous 60Hz position ticks over IPC. Instead, `PlayerModel` calculates the exact playback position using a steady-clock linear interpolator:
   - When **Playing**: advances smoothly based on elapsed time scaled by playback `rate`.
   - When **Paused** or **Stopped**: freezes at the last recorded position.
   - When **Seeked**: updates reference position and resets the interpolator timestamp.
   - Clamped within `[0, duration_usec]`.

3. **`PlaybackController`**:
   Unified control API across backends:
   - `play()`, `pause()`, `play_pause()`, `stop()`
   - `next()`, `previous()`
   - `seek(offset_usec)`, `set_position(track_id, position_usec)`
   - `set_volume(double)`, `set_rate(double)`
   - `set_loop_status(LoopStatus)`, `set_shuffle_status(ShuffleStatus)`
   - `open_uri(uri)`, `raise()`, `quit()`

4. **`MediaManager`**:
   Discovers all active players on the system. Automatically selects the current `active_player()` based on playback status heuristics (preferring playing players, then paused players, ordered by most recent activity).

---

## Building

### Prerequisites

- **CMake 3.24+** and a **C++20** compiler (GCC 12+, Clang 15+, MSVC 2022+).
- **Linux**: `libsystemd` (sd-bus >= 246) and `dbus-daemon` (for running tests).
- **Dependencies**: `brodbus` (Linux) and [bronze](https://github.com/wlejon/bronze) for the
  JavaScript binding. A plain `git clone` is enough: each is a `bro_dependency()` pin in
  `CMakeLists.txt` (`cmake/bro_deps.cmake`), taken from a working tree at `../<name>` when there
  is one and otherwise fetched at configure (override with `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<path>`).

### Standalone Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ctest --test-dir build --output-on-failure
```

---

## Testing

Tests run with 100% pass rate without external synthetic mocks. On Linux, the test suite leverages `brodbus::PrivateBus` to spin up isolated, dedicated D-Bus test daemons:

```bash
ctest --test-dir build --output-on-failure
```

Test coverage:
- **`test_metadata`**: Validates metadata parsing, serialization, duration math, and MPRIS `a{sv}` round-trip conversion.
- **`test_player_model`**: Tests state mutations, status transitions, and position ticker interpolation/freezing/scaling/clamping.
- **`test_mpris_client`**: Integration test spinning up a private D-Bus daemon, exporting a real MPRIS2 player, connecting a `brompris` client, and verifying real signal subscriptions (`PropertiesChanged`, `Seeked`) and method calls (`Play`, `Pause`, `Seek`, etc.).
- **`test_media_manager`**: Tests multi-player discovery, dynamic player arrival/exit via `NameOwnerChanged`, and `active_player()` heuristic switching.

---

## Example Usage

```cpp
#include <brompris/brompris.h>
#include <iostream>

class PrintObserver : public brompris::PlayerObserver {
public:
    void on_metadata_changed(brompris::MediaPlayer& player,
                             const brompris::MediaMetadata& meta) override {
        std::cout << "[" << player.id() << "] Now Playing: "
                  << meta.artist << " - " << meta.title
                  << " (" << meta.duration_seconds() << "s)\n";
    }

    void on_playback_status_changed(brompris::MediaPlayer& player,
                                    brompris::PlaybackStatus status) override {
        std::cout << "[" << player.id() << "] Status: "
                  << brompris::to_string(status) << "\n";
    }
};

int main() {
    auto manager = brompris::MediaManager::create();
    if (!manager) {
        std::cerr << "MediaManager not available on this platform\n";
        return 1;
    }

    PrintObserver observer;

    for (const auto& player : manager->active_players()) {
        player->add_observer(&observer);
        std::cout << "Found player: " << player->id() << "\n";
    }

    auto active = manager->active_player();
    if (active) {
        std::cout << "Active: " << active->id() << ", status: "
                  << brompris::to_string(active->playback_status()) << "\n";
        // Toggle playback
        active->play_pause();
    }

    // Run event loop
    while (true) {
        manager->wait(100000);  // wait up to 100ms for updates
    }
    return 0;
}
```

---

## License

MIT License. Copyright (c) 2026 Jonny Brannum.

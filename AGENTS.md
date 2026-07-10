# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```bash
# Configure (one-time, after conan install)
cmake --preset conan-debug

# Build (debug only — conforming to the .clang-format)
cmake --build --preset conan-debug

# Run tests
ctest --preset conan-debug

# Run a single test
./build/Debug/TheSunsetStraits_tests --run_test=<test_suite_name>
# e.g. ./build/Debug/TheSunsetStraits_tests --run_test=test_navigation
```

Dependencies are managed via Conan 2. After cloning:
```bash
conan install . --output-folder=build/Debug --build=missing -s build_type=Debug
```

## Code Standards

- **C++26** — use Modern C++ features (ranges, format, string_view, auto, concepts)
- **Formatting** — follow `.clang-format` (LLVM-based). Address clangd/clang-tidy warnings.
- **Assert over if** — use `assert()` for invariant checks (non-null pointers, component presence). Reserve `if` guards for runtime data conditions (bounds checks, file I/O).
- **Conventional commits** — `feat:`, `fix:`, `refactor:`, `chore:`, etc.
- **Tests** — write Boost.Test unit tests for new features (`tests/test_*.cpp`). The test target links `TheSunsetStraits_lib` + `Boost::unit_test_framework`.
- **Regard warnings as errors** — treat compiler warnings as errors. Use `-Werror` in coding.

### TDD Workflow

1. **Write the test first** in `tests/test_<feature>.cpp` using `BOOST_AUTO_TEST_SUITE` + `BOOST_AUTO_TEST_CASE`
2. **Build**: `cmake --build --preset conan-debug`
3. **Run**: `ctest --preset conan-debug` or single suite: `./build/Debug/TheSunsetStraits_tests --run_test=<suite>`
4. **Test patterns** used in this project:
   - Stack-allocate a `flecs::world` if ECS is needed (see `test_combat.cpp`)
   - Use `BOOST_TEST()` for assertions; `BOOST_REQUIRE()` for mandatory checks
   - No mocking — use real production classes directly
   - Use `std::filesystem::temp_directory_path()` for temp file I/O tests
   - Don't use `||`/`&&` inside `BOOST_TEST()` — assign to a `bool` first

### Code Patterns

- **Prefer `enum class`** over magic numbers. If a switch matches values that correspond to an existing enum (e.g. `BuildingData::Type`, `Team`), use the enum directly with `static_cast`.
- **Use existing component structs** instead of declaring parallel fields. If data already exists in an ECS component (`CombatStats`, `Movement`, `Vision`, `Sprite`), embed it or reuse it rather than re-declaring its fields.

- Don't pre-optimize. Focus on correctness and clarity first. Use profiling to identify bottlenecks before optimizing, which is my work.
- Don't hardcode anything if there is a config for it, use it.

- 错误应该暴露而不是无视，找出并修复才是对的

## Architecture Overview

### Game Loop

Authoritative server + thin client, both in-process via `LocalSession` for single-player.

```
App::run()
 ├── process_events()    ← SDL/ImGui input
 ├── update(dt)          ← main thread: client_->update() + input handling + camera
 └── render()            ← RenderSystem + UIManager (ImGui overlay)
```

`GameMode` runs on its own thread (the "server" thread):
```
GameMode::update(dt)
 ├── server_->poll_messages()     ← consume client inputs
 ├── world_state_.update(dt)      ← day/night cycle
 ├── events_->update()            ← scripted faction events
 ├── projectiles + combat events
 ├── world_.progress(dt)          ← flecs ECS tick (PreUpdate → OnUpdate → PostUpdate)
 └── server_->broadcast_sync()    ← sync dirty entities to clients
```

### Key Layers

| Layer | Directory | Tech |
|-------|-----------|------|
| Game logic | `src/core/game-mode.*` | flecs ECS world, manual systems |
| ECS components | `src/entities/components/*.hpp` | Plain structs (Transform, CombatStats, NPCState, etc.) |
| Net | `src/net/` | Boost.Asio coroutines, binary packet protocol |
| Rendering | `src/systems/render-system.*` | SDL3_Renderer (Vulkan backend), sprite-based |
| UI | `src/ui/ui-manager.*` | Dear ImGui (SDL3 backend) + SDL_ttf font overlay |
| World data | `src/world/` | MapData (tile grid), WorldState (time/season), terrain+location loaders |
| Systems | `src/systems/` | flecs systems: AI, combat, collision, navigation (A*), camera |
| Dialogue | `src/dialogue/` | Template-based NPC dialogue, locale-driven |
| Factions/Events | `src/factions/` | FactionNetwork, EventSimulator |

### Data Flow

1. **Client** sends input (WASD direction, interact, etc.) via `ClientMsgType`
2. **Server** (`GameMode`) processes input, runs ECS simulation
3. **Sync**: dirty entities are serialized with component bitmask → binary payload → `ServerMsgType`
4. **Client** receives sync → updates `RemoteEntity` list → interpolates positions
5. **RenderSystem** reads `Client` state + `NavigationSystem` for tile auto-tiling
6. **UIManager** reads `Client` state + `WorldState` for HUD/panels

### Network Protocol

- Binary packets: `[type:u32][size:u32][payload...]`
- Entity sync uses component bitmask (`SyncComponent::Mask`) for delta compression
- Transport: in-process `LocalSession` (ring buffer) or `NetworkSession` (TCP)

### Key Dependencies

| Library | Use |
|---------|-----|
| SDL3 | Window, input, renderer |
| flecs 4.x | ECS world, systems, queries |
| Boost (Asio + JSON) | Networking, coroutines, JSON parsing |
| Dear ImGui | Debug UI, dialogue panels, menus |
| spdlog | Logging |
| SDL3_ttf | Font rendering (HUD text) |

### Memory

Persistent project facts are stored in `.claude/projects/*/memory/` as markdown files. Update them when you learn non-obvious project constraints.

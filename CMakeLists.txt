cmake_minimum_required(VERSION 3.22)
project(TheSunsetStraits VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED)
find_package(Boost REQUIRED)
find_package(imgui REQUIRED)

set(SOURCES
    src/main.cpp
    src/core/app.cpp
    src/core/game-mode.cpp
    src/core/resource-manager.cpp
    src/core/input-manager.cpp
    src/core/game-clock.cpp
    src/entities/entity-manager.cpp
    src/systems/render-system.cpp
    src/systems/camera-system.cpp
    src/systems/navigation-system.cpp
    src/knowledge/knowledge-graph.cpp
    src/world/world-state.cpp
    src/world/map-data.cpp
    src/dialogue/dialogue-engine.cpp
    src/dialogue/topic-registry.cpp
    src/dialogue/relationship-table.cpp
    src/factions/faction-network.cpp
    src/factions/event-simulator.cpp
    src/knowledge/rumor-propagator.cpp
    src/survival/condition-tracker.cpp
    src/save/save-manager.cpp
    src/systems/combat-system.cpp
    src/systems/quest-manager.cpp
    src/net/network-manager.cpp
    src/net/server.cpp
    src/net/client.cpp
    src/net/local-transport.cpp
    src/net/network-transport.cpp
    src/core/locale-manager.cpp
    src/ui/ui-manager.cpp
    src/ui/hud.cpp
    src/ui/imgui_impl_sdl3.cpp
)

set(HEADERS
    src/core/app.hpp
    src/core/resource-manager.hpp
    src/core/input-manager.hpp
    src/core/game-clock.hpp
    src/core/math.hpp
    src/entities/entity-manager.hpp
    src/entities/components/position.hpp
    src/entities/components/sprite.hpp
    src/entities/components/movement.hpp
    src/entities/components/npc-state.hpp
    src/entities/components/interactable.hpp
    src/systems/render-system.hpp
    src/systems/camera-system.hpp
    src/systems/navigation-system.hpp
    src/knowledge/knowledge-graph.hpp
    src/world/world-state.hpp
    src/world/map-data.hpp
    src/dialogue/dialogue-engine.hpp
    src/dialogue/topic-registry.hpp
    src/dialogue/relationship-table.hpp
    src/ui/ui-manager.hpp
    src/ui/hud.hpp
    src/ui/imgui_impl_sdl3.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    sdl::sdl
    Boost::boost
    Boost::json
    imgui::imgui
)

if(MINGW OR WIN32)
    target_link_libraries(TheSunsetStraits PRIVATE
        ws2_32
        mswsock
    )

    target_link_options(TheSunsetStraits PRIVATE
        -static
        -Wl,--allow-multiple-definition
    )
endif()


# compile_commands.json
if(WIN32)
    # Visual Studio doesn't support symlinks by default. Don't use it.
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

# ---- Tests ----
set(TEST_SOURCES
    tests/main.cpp
    tests/test_math.cpp
    tests/test_knowledge.cpp
    tests/test_entity.cpp
    tests/test_navigation.cpp
    tests/test_world.cpp
    tests/test_input.cpp
    tests/test_dialogue.cpp
    tests/test_factions.cpp
    tests/test_survival.cpp
    tests/test_combat.cpp
    src/core/input-manager.cpp
    src/core/game-clock.cpp
    src/entities/entity-manager.cpp
    src/systems/navigation-system.cpp
    src/knowledge/knowledge-graph.cpp
    src/world/world-state.cpp
    src/world/map-data.cpp
    src/dialogue/dialogue-engine.cpp
    src/dialogue/topic-registry.cpp
    src/dialogue/relationship-table.cpp
    src/factions/faction-network.cpp
    src/factions/event-simulator.cpp
    src/knowledge/rumor-propagator.cpp
    src/survival/condition-tracker.cpp
    src/save/save-manager.cpp
    src/systems/combat-system.cpp
)

add_executable(TheSunsetStraits_tests ${TEST_SOURCES})
target_include_directories(TheSunsetStraits_tests PRIVATE src)
target_link_libraries(TheSunsetStraits_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
    Boost::json
)

# Register with CTest
enable_testing()
add_test(NAME unit_tests COMMAND TheSunsetStraits_tests)

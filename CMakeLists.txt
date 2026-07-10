cmake_minimum_required(VERSION 3.22)
project(TheSunsetStraits VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_NO_SYSTEM_FROM_IMPORTED OFF) # Turn off diagnostic on imported headers

find_package(SDL3 REQUIRED)
find_package(Boost REQUIRED)
find_package(imgui REQUIRED)
find_package(spdlog REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(flecs REQUIRED)
find_package(reflectcpp REQUIRED)

# ---- Helper: link Dear ImGui backends from Conan package ----
function(add_imgui_backends)
    set(one_value_args TARGET)
    set(multi_value_args BACKENDS DEPS)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "${one_value_args}"
                          "${multi_value_args}")
    cmake_path(SET imgui_bindings_dir NORMALIZE
               "${imgui_INCLUDE_DIR}/../res/bindings")

    foreach (backend ${arg_BACKENDS})
        find_file(imgui_backend "imgui_impl_${backend}.h"
            NO_DEFAULT_PATH
            PATHS ${imgui_bindings_dir}
            REQUIRED
            )
        if (backend STREQUAL "metal")
            set(extension "mm")
        else()
            set(extension "cpp")
        endif()
        list(APPEND backend_sources
             "${imgui_bindings_dir}/imgui_impl_${backend}.${extension}")
    endforeach()
    MESSAGE(STATUS "Adding ImGui backends: ${arg_BACKENDS}")
    add_library(imgui_backend "${backend_sources}")
    target_include_directories(imgui_backend PUBLIC ${imgui_bindings_dir})
    target_link_libraries(imgui_backend PUBLIC imgui::imgui ${arg_DEPS})
    target_link_libraries(${arg_TARGET} PRIVATE imgui_backend)
endfunction()

# ---- Production library (all game code except main.cpp) ----
set(LIB_SOURCES
    src/core/app.cpp
    src/core/game-mode.cpp
    src/core/resource-manager.cpp
    src/core/input-manager.cpp
    src/core/game-clock.cpp
    src/core/locale-manager.cpp
    src/systems/render-system.cpp
    src/systems/camera-system.cpp
    src/systems/navigation-system.cpp
    src/systems/combat-system.cpp
    src/systems/combat-utils.cpp
    src/systems/collision-system.cpp
    src/systems/quest-manager.cpp
    src/systems/formation.cpp
    src/systems/animation-system.cpp
    src/systems/animation-controller-system.cpp
    src/knowledge/knowledge-graph.cpp
    src/knowledge/rumor-propagator.cpp
    src/world/world-state.cpp
    src/world/map-data.cpp
    src/dialogue/dialogue-engine.cpp
    src/dialogue/topic-registry.cpp
    src/dialogue/relationship-table.cpp
    src/entities/entity-factory.cpp
    src/factions/faction-network.cpp
    src/factions/event-simulator.cpp
    src/save/save-manager.cpp
    src/net/server.cpp
    src/net/client.cpp
    src/net/session.cpp
    src/net/local-session.cpp
    src/net/network-session.cpp
    src/net/sync-utils.cpp
    src/ui/ui-manager.cpp
    src/animation/animation-data.cpp
)

set(LIB_HEADERS
    src/core/app.hpp
    src/core/camera-device.hpp
    src/core/font.hpp
    src/core/game-clock.hpp
    src/core/game-mode.hpp
    src/core/game-types.hpp
    src/core/input-manager.hpp
    src/core/locale-manager.hpp
    src/core/math.hpp
    src/core/resource-manager.hpp
    src/dialogue/dialogue-engine.hpp
    src/dialogue/relationship-table.hpp
    src/dialogue/topic-registry.hpp
    src/entities/entity-factory.hpp
    src/components/building-data.hpp
    src/components/collider.hpp
    src/components/combat-stats.hpp
    src/components/defense-structure.hpp
    src/components/interactable.hpp
    src/components/movement.hpp
    src/components/npc-state.hpp
    src/components/player.hpp
    src/components/position.hpp
    src/components/soldier-ai.hpp
    src/components/survival-state.hpp
    src/components/vision.hpp
    src/components/visual-fx.hpp
    src/components/entity-kind.hpp
    src/components/interpolation-target.hpp
    src/components/visual/animation.hpp
    src/components/visual/sprite.hpp
    src/factions/event-simulator.hpp
    src/factions/faction-network.hpp
    src/knowledge/knowledge-graph.hpp
    src/knowledge/rumor-propagator.hpp
    src/net/client.hpp
    src/net/local-session.hpp
    src/net/net-packet.hpp
    src/net/network-session.hpp
    src/net/server.hpp
    src/net/session.hpp
    src/net/sync-utils.hpp
    src/net/sync-io.hpp
    src/net/sync-registry.hpp
    src/save/save-manager.hpp
    src/systems/camera-system.hpp
    src/systems/collision-system.hpp
    src/systems/combat-system.hpp
    src/systems/combat-utils.hpp
    src/systems/formation.hpp
    src/systems/animation-system.hpp
    src/systems/animation-controller-system.hpp
    src/systems/navigation-system.hpp
    src/systems/quest-manager.hpp
    src/systems/render-system.hpp
    src/ui/ui-manager.hpp
    src/world/location-store.hpp
    src/world/map-data.hpp
    src/world/terrain-generator.hpp
    src/world/world-state.hpp
    src/animation/animation-data.hpp
)

add_library(TheSunsetStraits_lib STATIC ${LIB_SOURCES} ${LIB_HEADERS})

target_include_directories(TheSunsetStraits_lib PUBLIC src)

target_compile_options(TheSunsetStraits_lib PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    # -Weverything              # too noisy; enable selectively during reviews
    -Wno-deprecated-declarations # flecs keeps reporting this warning
)

target_link_libraries(TheSunsetStraits_lib PUBLIC
    sdl::sdl                    # test code includes SDL headers transitively
    Boost::json                 # test code uses Boost.JSON
    spdlog::spdlog              # test code includes spdlog headers
    flecs::flecs_static         # test code creates flecs::world
    reflectcpp::reflectcpp      # used in public headers (entity-factory.hpp)
)
target_link_libraries(TheSunsetStraits_lib PRIVATE
    Boost::boost                # core Boost headers (non-JSON)
    imgui::imgui                # only used by render code, not by tests
)
# SDL3_image/ttf are PUBLIC because lib headers (font.hpp, render-system.hpp)
# include them; targets linking the lib need the include paths.
target_link_libraries(TheSunsetStraits_lib PUBLIC
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf-static
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Sanitizers
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES
        "GNU")
        target_compile_options(TheSunsetStraits_lib PRIVATE
            # -fsanitize=thread      # disabled: tsan + flecs 4.x produces false
            #                         # positives on world::progress()
            # -fsanitize=address     # disabled for now; enable in CI builds
            -O0
        )
        target_link_options(TheSunsetStraits_lib PRIVATE
            # -fsanitize=thread
            # -fsanitize=address
        )
        target_link_libraries(TheSunsetStraits_lib PRIVATE pthread)
    endif()
endif()


# ---- Main executable ----
add_executable(${PROJECT_NAME} src/main.cpp)

# Patch ImGui backends — use source files from Conan package
add_imgui_backends(TARGET TheSunsetStraits_lib
    BACKENDS #[[ sdl3 ]] sdlrenderer3
    DEPS sdl::sdl)

# The lib's ui-manager.cpp references imgui_backend symbols.
# PUBLIC propagates to the main target so linker can resolve them.
target_link_libraries(TheSunsetStraits_lib PUBLIC imgui_backend)

target_link_libraries(${PROJECT_NAME} PRIVATE
    TheSunsetStraits_lib
)


# ---- compile_commands.json ----
if(NOT WIN32)
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
    tests/test_combat.cpp
    tests/test_session.cpp
    tests/test_local_session.cpp
    tests/test_network_session.cpp
    tests/test_collision.cpp
    tests/test_quest.cpp
    tests/test_save.cpp
    tests/test_game_mode.cpp
)

add_executable(TheSunsetStraits_tests ${TEST_SOURCES})
target_include_directories(TheSunsetStraits_tests PRIVATE src)
target_link_libraries(TheSunsetStraits_tests PRIVATE
    TheSunsetStraits_lib
    Boost::unit_test_framework
)

if(MINGW OR WIN32)
    target_link_libraries(TheSunsetStraits_lib PUBLIC
        ws2_32
        mswsock
    )

    target_link_options(TheSunsetStraits_lib PUBLIC
        -static
        -Wl,--allow-multiple-definition
    )
endif()

# Register with CTest
enable_testing()
add_test(NAME unit_tests COMMAND TheSunsetStraits_tests)

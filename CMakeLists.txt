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
# find_package(SDL3_ttf REQUIRED)
find_package(flecs REQUIRED)

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

set(SOURCES
    src/main.cpp
    src/core/app.cpp
    src/core/game-mode.cpp
    src/core/resource-manager.cpp
    src/core/input-manager.cpp
    src/core/game-clock.cpp
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
    src/save/save-manager.cpp
    src/systems/combat-system.cpp
    src/systems/collision-system.cpp
    src/systems/quest-manager.cpp
    src/net/server.cpp
    src/net/client.cpp
    src/net/session.cpp
    src/net/local-session.cpp
    src/net/network-session.cpp
    src/core/locale-manager.cpp
    src/ui/ui-manager.cpp
)

set(HEADERS
    src/core/app.hpp
    src/core/resource-manager.hpp
    src/core/input-manager.hpp
    src/core/game-clock.hpp
    src/core/math.hpp
    src/entities/components/position.hpp
    src/entities/components/sprite.hpp
    src/entities/components/movement.hpp
    src/entities/components/npc-state.hpp
    src/entities/components/interactable.hpp
    src/entities/components/collider.hpp
    src/entities/components/player.hpp
    src/systems/render-system.hpp
    src/systems/camera-system.hpp
    src/systems/navigation-system.hpp
    src/systems/collision-system.hpp
    src/systems/combat-utils.hpp
    src/knowledge/knowledge-graph.hpp
    src/world/world-state.hpp
    src/world/map-data.hpp
    src/dialogue/dialogue-engine.hpp
    src/dialogue/topic-registry.hpp
    src/dialogue/relationship-table.hpp
    src/ui/ui-manager.hpp
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    # -Weverything
    -Wno-deprecated-declarations # flecs keeps reporting this warning
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

# Patch imgui backends — use source files from conan package
add_imgui_backends(TARGET ${PROJECT_NAME}
    BACKENDS #[[ sdl3 ]] sdlrenderer3
    DEPS sdl::sdl)

target_link_libraries(${PROJECT_NAME} PRIVATE
    sdl::sdl
    Boost::boost
    Boost::json
    imgui::imgui
    spdlog::spdlog
    SDL3_image::SDL3_image
    # SDL3_ttf::SDL3_ttf-static
    flecs::flecs_static
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Sanitizers
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES
        "GNU")
        target_compile_options(TheSunsetStraits PRIVATE
            # -fsanitize=thread
            # -fsanitize=address
            -O0
        )
        target_link_options(TheSunsetStraits PRIVATE
            # -fsanitize=thread
            # -fsanitize=address
        )
        target_link_libraries(TheSunsetStraits PRIVATE pthread)
    endif()
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
    tests/test_combat.cpp
    tests/test_session.cpp
    tests/test_local_session.cpp
    tests/test_network_session.cpp
    src/core/input-manager.cpp
    src/core/game-clock.cpp
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
    src/save/save-manager.cpp
    src/systems/combat-system.cpp
    src/net/session.cpp
    src/net/local-session.cpp
    src/net/network-session.cpp
)

add_executable(TheSunsetStraits_tests ${TEST_SOURCES})
target_include_directories(TheSunsetStraits_tests PRIVATE src)
target_link_libraries(TheSunsetStraits_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
    Boost::json
    spdlog::spdlog
    flecs::flecs_static
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


    target_link_libraries(TheSunsetStraits_tests PRIVATE
        ws2_32
        mswsock
    )

    target_link_options(TheSunsetStraits_tests PRIVATE
        -static
        -Wl,--allow-multiple-definition
    )
endif()

# Register with CTest
enable_testing()
add_test(NAME unit_tests COMMAND TheSunsetStraits_tests)

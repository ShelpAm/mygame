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
    src/core/App.cpp
    src/core/GameMode.cpp
    src/core/ResourceManager.cpp
    src/core/InputManager.cpp
    src/core/GameClock.cpp
    src/entities/EntityManager.cpp
    src/systems/RenderSystem.cpp
    src/systems/CameraSystem.cpp
    src/systems/NavigationSystem.cpp
    src/knowledge/KnowledgeGraph.cpp
    src/world/WorldState.cpp
    src/world/MapData.cpp
    src/dialogue/DialogueEngine.cpp
    src/dialogue/TopicRegistry.cpp
    src/dialogue/RelationshipTable.cpp
    src/factions/FactionNetwork.cpp
    src/factions/EventSimulator.cpp
    src/knowledge/RumorPropagator.cpp
    src/survival/ConditionTracker.cpp
    src/save/SaveManager.cpp
    src/systems/CombatSystem.cpp
    src/systems/QuestManager.cpp
    src/net/NetworkManager.cpp
    src/core/LocaleManager.cpp
    src/ui/UIManager.cpp
    src/ui/HUD.cpp
    src/ui/imgui_impl_sdl3.cpp
)

set(HEADERS
    src/core/App.hpp
    src/core/ResourceManager.hpp
    src/core/InputManager.hpp
    src/core/GameClock.hpp
    src/core/Math.hpp
    src/entities/EntityManager.hpp
    src/entities/components/Position.hpp
    src/entities/components/Sprite.hpp
    src/entities/components/Movement.hpp
    src/entities/components/NPCState.hpp
    src/entities/components/Interactable.hpp
    src/systems/RenderSystem.hpp
    src/systems/CameraSystem.hpp
    src/systems/NavigationSystem.hpp
    src/knowledge/KnowledgeGraph.hpp
    src/world/WorldState.hpp
    src/world/MapData.hpp
    src/dialogue/DialogueEngine.hpp
    src/dialogue/TopicRegistry.hpp
    src/dialogue/RelationshipTable.hpp
    src/ui/UIManager.hpp
    src/ui/HUD.hpp
    src/ui/imgui_impl_sdl3.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3
    Boost::boost
    Boost::json
    imgui::imgui
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

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
    src/core/InputManager.cpp
    src/core/GameClock.cpp
    src/entities/EntityManager.cpp
    src/systems/NavigationSystem.cpp
    src/knowledge/KnowledgeGraph.cpp
    src/world/WorldState.cpp
    src/world/MapData.cpp
    src/dialogue/DialogueEngine.cpp
    src/dialogue/TopicRegistry.cpp
    src/dialogue/RelationshipTable.cpp
    src/factions/FactionNetwork.cpp
    src/factions/EventSimulator.cpp
    src/knowledge/RumorPropagator.cpp
    src/survival/ConditionTracker.cpp
    src/save/SaveManager.cpp
    src/systems/CombatSystem.cpp
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

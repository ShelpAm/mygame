#pragma once

#include <SDL3/SDL.h>
#include "core/GameTypes.hpp"
#include "core/LocaleManager.hpp"
#include <memory>
#include <string>
#include <vector>

class ResourceManager;
class InputManager;
class GameClock;
class RenderSystem;
class CameraSystem;
class NavigationSystem;
class EntityManager;
class UIManager;
class WorldState;
class KnowledgeGraph;
class DialogueEngine;
class TopicRegistry;
class RelationshipTable;
class ConditionTracker;
class FactionNetwork;
class EventSimulator;
class RumorPropagator;
class CombatSystem;
class QuestManager;
class NetworkManager;
#include "core/GameMode.hpp"
struct CombatStats;

class App {
public:
    App();
    ~App();
    bool init();
    void run();
    void shutdown();

    SDL_Window* window() { return m_window; }
    SDL_Renderer* renderer() { return m_renderer; }
    InputManager& input() { return *m_input; }
    LocaleManager& locale() { return *m_locale; }
    const LocaleManager& locale() const { return *m_locale; }
    const ConditionTracker& survival() const { return *m_survival; }
    void doRest();
    void recruitSoldier();
    const CombatStats* playerCombatStats() const;
    bool isPlayerDead() const;
    bool showLoadMenu() const { return m_showLoadMenu; }
    void setShowLoadMenu(bool v) { m_showLoadMenu = v; }
    bool showHelp() const { return m_showHelp; }
    void setShowHelp(bool v) { m_showHelp = v; }
    bool showMultiplayer() const { return m_showMultiplayer; }
    void setShowMultiplayer(bool v) { m_showMultiplayer = v; }
    NetworkManager* networkMut() { return m_network.get(); }
    const NetworkManager* network() const { return m_network.get(); }
    QuestManager& questsMut() { return *m_quests; }
    const QuestManager& quests() const { return *m_quests; }
    GameMode& gameMode() { return *m_gameMode; }
    const GameMode& gameMode() const { return *m_gameMode; }

    void setUILanguage(int langIndex);
    void endDialogue() { m_gameMode->endDialogue(); }
    void askTopic(const std::string& t) { m_gameMode->doDialogueAction(t, *m_locale); }
    void doDialogueAction(const std::string& a) { m_gameMode->doDialogueAction(a, *m_locale); }

    void quickSave();
    void saveToSlot(int slot);
    void loadFromSlot(int slot);
    std::vector<int> availableSaveSlots() const;

private:
    void processEvents();
    void update(float dt);
    void render();

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    std::unique_ptr<ResourceManager> m_resources;
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<GameClock> m_gameClock;
    std::unique_ptr<RenderSystem> m_renderSystem;
    std::unique_ptr<CameraSystem> m_cameraSystem;
    std::unique_ptr<NavigationSystem> m_navigationSystem;
    std::unique_ptr<EntityManager> m_entityManager;
    std::unique_ptr<UIManager> m_uiManager;
    std::unique_ptr<WorldState> m_worldState;
    std::unique_ptr<KnowledgeGraph> m_knowledge;
    std::unique_ptr<DialogueEngine> m_dialogueEngine;
    std::unique_ptr<TopicRegistry> m_topicRegistry;
    std::unique_ptr<RelationshipTable> m_relationships;
    std::unique_ptr<LocaleManager> m_locale;
    std::unique_ptr<ConditionTracker> m_survival;
    std::unique_ptr<FactionNetwork> m_factions;
    std::unique_ptr<EventSimulator> m_events;
    std::unique_ptr<RumorPropagator> m_rumors;
    std::unique_ptr<CombatSystem> m_combat;
    std::unique_ptr<QuestManager> m_quests;
    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<GameMode> m_gameMode;

    bool m_showLoadMenu = false;
    bool m_showHelp = false;
    bool m_showMultiplayer = false;
    bool m_running = false;
    int m_nextSaveSlot = 1;

    static constexpr int WINDOW_WIDTH = 1280;
    static constexpr int WINDOW_HEIGHT = 720;
    static constexpr const char* WINDOW_TITLE = "The Sunset Straits";
};

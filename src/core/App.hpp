#pragma once

#include <SDL3/SDL.h>
#include "entities/components/Position.hpp"
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
struct CombatStats;

struct DialogueLine {
    enum Speaker { Player, NPC };
    Speaker speaker = NPC;
    std::string textKey;   // locale key (for translatable lines)
    std::string rawText;   // fallback direct text (for procedurally generated lines)
    bool useRaw = false;   // true = display rawText, false = display locale.get(textKey)
    std::string npcName;   // only used when speaker == NPC
};

struct DialogueState {
    bool active = false;
    EntityId npcEntity = INVALID_ENTITY;
    std::string npcId;
    std::string npcName;
    std::vector<DialogueLine> history;
    std::vector<std::string> availableTopics;  // Topics to ask about
    std::vector<std::string> availableActions;  // tell/gift/threaten/etc
    int npcTrust = 0;
    int npcFear = 0;
    bool canTrade = false;
    bool canGift = false;
    bool canThreaten = false;
};

class App {
public:
    App();
    ~App();

    bool init();
    void run();
    void shutdown();

    SDL_Window* window() { return m_window; }
    SDL_Renderer* renderer() { return m_renderer; }
    ResourceManager& resources() { return *m_resources; }
    InputManager& input() { return *m_input; }
    GameClock& clock() { return *m_gameClock; }

    // Dialogue state access
    const DialogueState& dialogueState() const { return m_dialogueState; }
    DialogueState& dialogueStateMut() { return m_dialogueState; }
    void startDialogue(EntityId npcEntity, const std::string& npcId,
                       const std::string& npcName);
    void endDialogue();
    void askTopic(const std::string& topicId);
    void doDialogueAction(const std::string& action);
    void setUILanguage(int langIndex);
    void quickSave();
    void quickLoad(const std::string& path);
    void saveToSlot(int slot);
    void loadFromSlot(int slot);
    std::vector<int> availableSaveSlots() const;
    void recruitSoldier();
    void spawnTestEnemies();
    void doRest();

    LocaleManager& locale() { return *m_locale; }
    const LocaleManager& locale() const { return *m_locale; }
    const ConditionTracker& survival() const { return *m_survival; }
    const CombatStats* playerCombatStats() const;
    bool isPlayerDead() const;
    bool showLoadMenu() const { return m_showLoadMenu; }
    void setShowLoadMenu(bool v) { m_showLoadMenu = v; }
    bool showHelp() const { return m_showHelp; }
    const QuestManager& quests() const { return *m_quests; }
    QuestManager& questsMut() { return *m_quests; }
    bool showMultiplayer() const { return m_showMultiplayer; }
    void setShowMultiplayer(bool v) { m_showMultiplayer = v; }
    const NetworkManager* network() const { return m_network.get(); }
    NetworkManager* networkMut() { return m_network.get(); }

private:
    void processEvents();
    void update(float dt);
    void render();

    struct NPCKnowledgeEntry {
        std::string factId;
        std::string version;
        int confidence = 70;
        bool witnessed = false;
        std::string source;
    };
    void spawnNPC(const std::string& id, const std::string& name,
                  float x, float y, const std::string& personality,
                  const std::vector<NPCKnowledgeEntry>& knownFacts);
    EntityId findNearestInteractable() const;
    void handleInteraction();

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
    bool m_showMultiplayer = false;
    float m_netSyncTimer = 0.f;

    EntityId m_playerEntity = INVALID_ENTITY;
    std::vector<EntityId> m_npcEntities;
    Vec2f m_playerFacing{0.f, -1.f};
    bool m_showLoadMenu = false;
    bool m_showHelp = false;
    int m_nextSaveSlot = 1;  // Last movement direction
    DialogueState m_dialogueState;

    bool m_running = false;
    static constexpr int WINDOW_WIDTH = 1280;
    static constexpr int WINDOW_HEIGHT = 720;
    static constexpr const char* WINDOW_TITLE = "The Sunset Straits";
};

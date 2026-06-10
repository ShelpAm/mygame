#include "core/App.hpp"
#include "core/GameMode.hpp"
#include "core/ResourceManager.hpp"
#include "core/InputManager.hpp"
#include "core/GameClock.hpp"
#include "systems/RenderSystem.hpp"
#include "systems/CameraSystem.hpp"
#include "systems/CombatSystem.hpp"
#include "systems/NavigationSystem.hpp"
#include "entities/EntityManager.hpp"
#include "entities/components/CombatStats.hpp"
#include "dialogue/DialogueEngine.hpp"
#include "dialogue/TopicRegistry.hpp"
#include "dialogue/RelationshipTable.hpp"
#include "factions/FactionNetwork.hpp"
#include "factions/EventSimulator.hpp"
#include "knowledge/RumorPropagator.hpp"
#include "survival/ConditionTracker.hpp"
#include "save/SaveManager.hpp"
#include "systems/QuestManager.hpp"
#include "net/NetworkManager.hpp"
#include "net/Server.hpp"
#include "net/Client.hpp"
#include "ui/UIManager.hpp"
#include "world/WorldState.hpp"
#include <imgui.h>
#include <fstream>

App::App() = default;
App::~App() { shutdown(); }

bool App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) return false;
    m_window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (!m_window) return false;
    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) return false;
    SDL_SetRenderVSync(m_renderer, 1);

    m_resources = std::make_unique<ResourceManager>();
    m_input = std::make_unique<InputManager>();
    m_gameClock = std::make_unique<GameClock>();
    m_entityManager = std::make_unique<EntityManager>();
    m_worldState = std::make_unique<WorldState>();
    m_knowledge = std::make_unique<KnowledgeGraph>();
    m_dialogueEngine = std::make_unique<DialogueEngine>();
    m_topicRegistry = std::make_unique<TopicRegistry>();
    m_relationships = std::make_unique<RelationshipTable>();
    m_locale = std::make_unique<LocaleManager>();
    m_survival = std::make_unique<ConditionTracker>();
    m_factions = std::make_unique<FactionNetwork>();
    m_cameraSystem = std::make_unique<CameraSystem>(WINDOW_WIDTH, WINDOW_HEIGHT);
    m_renderSystem = std::make_unique<RenderSystem>(m_renderer, *m_resources, *m_cameraSystem);
    m_navigationSystem = std::make_unique<NavigationSystem>();
    m_uiManager = std::make_unique<UIManager>(m_window, m_renderer);
    m_combat = std::make_unique<CombatSystem>();
    m_quests = std::make_unique<QuestManager>();
    m_network = std::make_unique<NetworkManager>();
    m_server = std::make_unique<Server>();
    m_client = std::make_unique<Client>();
    m_server->setManagers(m_combat.get(), m_worldState.get(), m_quests.get());
    m_client->setManagers(m_combat.get(), m_worldState.get(), m_quests.get());

    m_locale->discoverLanguages("assets/locale");
    m_locale->setLanguage(0);
    m_dialogueEngine->discoverLanguages("assets/dialogue");
    m_quests->loadFromJson("assets/data/quests.json");

    // Network callback: handle incoming entity updates and combat events
    m_network->setCallback([this](const NetMessage& msg) {
        if (msg.type == NetMessage::EntityUpdate && msg.data.size() >= 21) {
            // Remote player position update
            int id; float x, y; int hp, maxHp; uint8_t alive;
            memcpy(&id, msg.data.data(), 4);
            memcpy(&x, msg.data.data()+4, 4); memcpy(&y, msg.data.data()+8, 4);
            memcpy(&hp, msg.data.data()+12, 4); memcpy(&maxHp, msg.data.data()+16, 4);
            alive = msg.data[20];
            // Create/update remote player entity on server
            if (!m_server->remotePlayer(id)) {
                m_server->addRemotePlayer(id, {x, y});
            } else {
                m_server->updateRemotePlayer(id, {x, y}, hp, maxHp, (bool)alive);
            }
        } else if (msg.type == NetMessage::CombatEvent && msg.data.size() >= 13) {
            auto read = [&](int o) { int v; memcpy(&v, msg.data.data()+o, 4); return v; };
            int att = read(0), def = read(4), dmg = read(8); bool k = msg.data[12];
            if (m_server) m_server->handleCombatEvent(att, def, dmg, k);
            if (m_client) m_client->handleCombatEvent(att, def, dmg, k);
        }
    });

    m_events = std::make_unique<EventSimulator>(*m_factions, *m_knowledge, *m_worldState);
    m_rumors = std::make_unique<RumorPropagator>(*m_knowledge);
    m_gameMode = std::make_unique<GameMode>(m_server->entities());
    m_gameMode->initWorld(*m_worldState, *m_knowledge, *m_dialogueEngine,
                          *m_topicRegistry, *m_relationships, *m_factions,
                          *m_events, *m_rumors, *m_combat, *m_quests, *m_network);

    m_gameClock->restart();
    m_running = true;
    return true;
}

void App::run() {
    while (m_running) {
        float dt = m_gameClock->tick();
        processEvents();
        if (!m_running) break;
        update(dt);
        render();
    }
}

void App::shutdown() {
    m_running = false;
    // Stop networking first, then destroy game logic, then render/IO
    if (m_network) { m_network->disconnect(); m_network.reset(); }
    m_combat.reset();
    m_events.reset();
    m_gameMode.reset();
    m_quests.reset();
    m_gameMode.reset();
    m_combat.reset();
    m_quests.reset();
    m_events.reset();
    m_factions.reset();
    m_rumors.reset();
    m_knowledge.reset();
    m_dialogueEngine.reset();
    m_topicRegistry.reset();
    m_relationships.reset();
    m_locale.reset();
    m_survival.reset();
    m_uiManager.reset();
    m_renderSystem.reset();
    m_cameraSystem.reset();
    m_navigationSystem.reset();
    m_entityManager.reset();
    m_worldState.reset();
    m_input.reset();
    m_resources.reset();
    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    SDL_Quit();
}

void App::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        m_uiManager->processEvent(event);
        switch (event.type) {
            case SDL_EVENT_QUIT: m_running = false; break;
            case SDL_EVENT_WINDOW_RESIZED:
                m_cameraSystem->resize(event.window.data1, event.window.data2); break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat) break;
                switch (event.key.scancode) {
                    case SDL_SCANCODE_E: if (!isPlayerDead()) m_gameMode->handleInteraction(*m_locale); break;
                    case SDL_SCANCODE_R: doRest(); break;
                    case SDL_SCANCODE_F1: m_showHelp = !m_showHelp; break;
                    case SDL_SCANCODE_F2: recruitSoldier(); break;
                    case SDL_SCANCODE_F5: quickSave(); break;
                    case SDL_SCANCODE_F9: m_showLoadMenu = !m_showLoadMenu; break;
                    case SDL_SCANCODE_F10: m_showMultiplayer = !m_showMultiplayer; break;
                    default: break;
                }
                break;
        }
    }
    m_input->update();
}

void App::update(float dt) {
    m_survival->update(dt, false, false);
    m_worldState->update(dt);
    m_events->update(m_worldState->day());

    // Network handling
    if (m_network->isConnected()) {
        m_network->update();
        if (m_network->isHosting()) {
            // Server: authoritative combat + sync (Server owns the combat loop)
            m_server->update(dt, *m_network);
        } else {
            // Client: receive sync
            m_client->update(dt, *m_network);
        }
        m_network->interpolateEntities(dt);
    }

    // Single-player or host: spawn enemies from events
    if (!m_network->isConnected() || m_network->isHosting()) {
        static size_t s_lastCount = 0;
        auto cnt = m_events->triggeredEvents().size();
        if (cnt > s_lastCount) { s_lastCount = cnt;
            auto* pos = m_server->entities().getComponent<Position>(m_gameMode->playerEntity());
            Vec2f center = pos ? pos->worldPos : Vec2f{};
            auto& latest = m_events->triggeredEvents().back();
            if (latest.type == GameEvent::Type::Battle || latest.type == GameEvent::Type::RefugeeWave)
                m_combat->spawnEnemyWave(m_server->entities(), 2 + rand() % 4, center, 400.f, Team::Enemy);
        }
    }

    if (m_survival->state().food <= 0 || m_survival->state().water <= 0) {
        auto* pcs = m_server->entities().getComponent<CombatStats>(m_gameMode->playerEntity());
        if (pcs && pcs->alive) pcs->hp = std::max(0, pcs->hp - 1);
    }

    m_gameMode->isClient = (m_network->isConnected() && !m_network->isHosting());
    m_gameMode->update(dt, *m_input, *m_locale, *m_worldState);

    // Client: move local player and send to server
    if (m_network->isConnected() && !m_network->isHosting()) {
        Vec2f p = m_client->localPlayerPos();
        float mx = 0, my = 0;
        if (!ImGui::IsAnyItemActive()) {
            if (m_input->isPressed(InputManager::Action::MoveUp)) my -= 1;
            if (m_input->isPressed(InputManager::Action::MoveDown)) my += 1;
            if (m_input->isPressed(InputManager::Action::MoveLeft)) mx -= 1;
            if (m_input->isPressed(InputManager::Action::MoveRight)) mx += 1;
        }
        if (mx != 0 || my != 0) {
            float len = std::hypot(mx, my);
            m_client->setLocalPlayerPos({p.x + mx/len * 200.f * dt, p.y + my/len * 200.f * dt});
        }
        m_cameraSystem->setTarget(m_client->localPlayerPos());
        auto* cs = m_client->entities().getComponent<CombatStats>(m_client->localPlayer());
        m_network->sendEntityUpdate(0, m_client->localPlayerPos(), cs ? cs->hp : 20, cs ? cs->maxHp : 20, cs ? cs->alive : true);
    } else {
        auto* ppos = m_server->entities().getComponent<Position>(m_gameMode->playerEntity());
        if (ppos) m_cameraSystem->setTarget(ppos->worldPos);
    }
    m_cameraSystem->update(dt);

    m_uiManager->update(dt);
}

void App::render() {
    SDL_SetRenderDrawColor(m_renderer, 10, 10, 15, 255);
    SDL_RenderClear(m_renderer);
    // Render from server (single/host) or client (join)
    auto& renderEm = (m_network->isConnected() && !m_network->isHosting())
        ? m_client->entities() : m_server->entities();
    m_renderSystem->render(renderEm, *m_worldState, *m_navigationSystem,
                            m_combat->events(), m_network->remoteEntities());
    m_combat->clearEvents();
    m_uiManager->render(*m_worldState, *this);
    SDL_RenderPresent(m_renderer);
}

void App::setUILanguage(int langIndex) {
    m_locale->setLanguage(langIndex);
    m_dialogueEngine->setLanguage(langIndex);
}

bool App::isPlayerDead() const {
    auto* cs = m_server->entities().getComponent<CombatStats>(m_gameMode->playerEntity());
    return cs && !cs->alive;
}

const CombatStats* App::playerCombatStats() const {
    return m_server->entities().getComponent<CombatStats>(m_gameMode->playerEntity());
}

void App::doRest() {
    auto* pcs = m_server->entities().getComponent<CombatStats>(m_gameMode->playerEntity());
    if (pcs && pcs->alive) {
        pcs->hp = std::min(pcs->maxHp, pcs->hp + 5);
        m_survival->heal(5.f);
    }
}

void App::quickSave() { saveToSlot(m_nextSaveSlot++); }

void App::saveToSlot(int slot) {
    auto* pos = m_server->entities().getComponent<Position>(m_gameMode->playerEntity());
    auto* cs = m_server->entities().getComponent<CombatStats>(m_gameMode->playerEntity());
    Vec2f pp = pos ? pos->worldPos : Vec2f{};

    std::vector<SaveManager::NPCData> npcData;
    for (auto eid : m_gameMode->npcEntities()) {
        auto* np = m_server->entities().getComponent<Position>(eid);
        auto* ns = m_server->entities().getComponent<NPCState>(eid);
        auto* ncs = m_server->entities().getComponent<CombatStats>(eid);
        if (!np || !ns) continue;
        SaveManager::NPCData nd;
        nd.id = ns->npcId; nd.name = ns->displayName; nd.personality = ns->personality;
        nd.position = np->worldPos; nd.hp = ncs ? ncs->hp : 10; nd.maxHp = ncs ? ncs->maxHp : 10;
        nd.alive = ncs ? ncs->alive : true;
        for (const auto& [fid, kf] : ns->knowledge) nd.knowledge[fid] = kf.npcVersion;
        npcData.push_back(std::move(nd));
    }
    SaveManager::save("saves/save_" + std::to_string(slot) + ".json", *m_worldState,
                      *m_knowledge, *m_relationships, pp, cs ? cs->hp : 20, cs ? cs->maxHp : 20, npcData);
}

std::vector<int> App::availableSaveSlots() const {
    std::vector<int> slots;
    for (int i = 1; i <= 10; ++i) {
        std::ifstream f("saves/save_" + std::to_string(i) + ".json");
        if (f.good()) slots.push_back(i);
    }
    return slots;
}

void App::loadFromSlot(int slot) {
    std::string path = "saves/save_" + std::to_string(slot) + ".json";
    SaveManager::SaveData data;
    if (!SaveManager::load(path, data)) return;

    // Reset
    while (!m_server->entities().allEntities().empty())
        m_server->entities().destroyEntity(m_server->entities().allEntities().back());
    m_gameMode = std::make_unique<GameMode>(*m_entityManager);

    auto pid = m_gameMode->spawnPlayer(data.playerPos.x, data.playerPos.y);
    auto* cs = m_server->entities().getComponent<CombatStats>(pid);
    if (cs) { cs->hp = data.playerHp; cs->maxHp = data.playerMaxHp; }

    m_worldState->setDay(data.day); m_worldState->setSeason(data.season);
    for (const auto& t : data.seenTiles) m_worldState->revealTile(t);
    m_knowledge->markTopicKnown("ugarit_sack");
    m_knowledge->markTopicKnown("sea_peoples");
    m_knowledge->markTopicKnown("byblos_king");
    for (const auto& t : data.knownTopics) m_knowledge->markTopicKnown(t);
    m_relationships = std::make_unique<RelationshipTable>();
    for (const auto& [nid, rel] : data.relations)
        m_relationships->setRelation(nid, {rel[0], rel[1], rel[2]});

    for (const auto& nd : data.npcs) {
        std::vector<NPCKnowledgeEntry> facts;
        for (const auto& [fid, ver] : nd.knowledge) facts.push_back({fid, ver, 70, false, ""});
        m_gameMode->spawnNPC(nd.id, nd.name, nd.position.x, nd.position.y, nd.personality, facts);
        auto& eid = m_gameMode->npcEntities().back();
        auto* ncs = m_server->entities().getComponent<CombatStats>(eid);
        if (ncs) { ncs->hp = nd.hp; ncs->maxHp = nd.maxHp; ncs->alive = nd.alive; }
    }

    auto* pos = m_server->entities().getComponent<Position>(pid);
    if (pos) m_cameraSystem->centerOn(pos->worldPos);
}

void App::recruitSoldier() {
    static int idx = 0;
    Vec2f facing{0, -1};
    m_gameMode->spawnSoldier(m_gameMode->playerEntity(), idx++, facing);
}

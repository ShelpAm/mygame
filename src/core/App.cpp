#include "core/App.hpp"
#include "core/ResourceManager.hpp"
#include "core/InputManager.hpp"
#include "core/GameClock.hpp"
#include "core/LocaleManager.hpp"
#include "systems/RenderSystem.hpp"
#include "systems/CameraSystem.hpp"
#include "systems/NavigationSystem.hpp"
#include "systems/CombatSystem.hpp"
#include "entities/EntityManager.hpp"
#include "entities/components/Position.hpp"
#include "entities/components/Sprite.hpp"
#include "entities/components/NPCState.hpp"
#include "entities/components/Interactable.hpp"
#include "entities/components/CombatStats.hpp"
#include "entities/components/SoldierAI.hpp"
#include "knowledge/KnowledgeGraph.hpp"
#include "knowledge/RumorPropagator.hpp"
#include "dialogue/DialogueEngine.hpp"
#include "dialogue/TopicRegistry.hpp"
#include "dialogue/RelationshipTable.hpp"
#include "factions/FactionNetwork.hpp"
#include "factions/EventSimulator.hpp"
#include "survival/ConditionTracker.hpp"
#include "save/SaveManager.hpp"
#include "systems/QuestManager.hpp"
#include "net/NetworkManager.hpp"
#include "ui/UIManager.hpp"
#include "world/WorldState.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <filesystem>
#include <cmath>

App::App() = default;
App::~App() { shutdown(); }

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

bool App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    m_window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (!m_window) return false;
    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) return false;
    SDL_SetRenderVSync(m_renderer, 1);

    // Core systems
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
    m_combat = std::make_unique<CombatSystem>();
    m_quests = std::make_unique<QuestManager>();
    m_quests->loadFromJson("assets/data/quests.json");
    m_network = std::make_unique<NetworkManager>();
    m_cameraSystem = std::make_unique<CameraSystem>(WINDOW_WIDTH, WINDOW_HEIGHT);
    m_renderSystem = std::make_unique<RenderSystem>(m_renderer, *m_resources, *m_cameraSystem);
    m_navigationSystem = std::make_unique<NavigationSystem>();
    m_uiManager = std::make_unique<UIManager>(m_window, m_renderer);

    // Load data files
    m_locale->discoverLanguages("assets/locale");
    m_dialogueEngine->loadTemplates(DialogueEngine::Language::English, "assets/data/dialogue_templates.json");
    m_dialogueEngine->loadTemplates(DialogueEngine::Language::Chinese, "assets/data/dialogue_templates_zh.json");

    // Load factions
    try {
        auto json = boost::json::parse(readFile("assets/data/factions.json"));
        for (const auto& item : json.as_object().at("factions").as_array()) {
            auto& obj = item.as_object();
            Faction f;
            f.id = std::string(obj.at("id").as_string());
            f.name = std::string(obj.at("name").as_string());
            f.power = static_cast<int>(obj.at("power").as_int64());
            f.cohesion = static_cast<int>(obj.at("cohesion").as_int64());
            f.wealth = static_cast<int>(obj.at("wealth").as_int64());
            if (obj.contains("relations")) {
                for (const auto& [otherId, val] : obj.at("relations").as_object()) {
                    f.relations[std::string(otherId)] = static_cast<int>(val.as_int64());
                }
            }
            m_factions->addFaction(std::move(f));
        }
    } catch (const std::exception& e) {
        SDL_Log("Failed to load factions: %s", e.what());
    }

    // Load events
    m_events = std::make_unique<EventSimulator>(*m_factions, *m_knowledge, *m_worldState);
    try {
        auto json = boost::json::parse(readFile("assets/data/events.json"));
        for (const auto& item : json.as_object().at("events").as_array()) {
            auto& obj = item.as_object();
            GameEvent ev;
            ev.id = std::string(obj.at("id").as_string());
            ev.description = std::string(obj.at("description").as_string());
            ev.triggerDay = static_cast<int>(obj.at("triggerDay").as_int64());
            if (obj.contains("locationId")) ev.locationId = std::string(obj.at("locationId").as_string());
            if (obj.contains("sourceFactionId")) ev.sourceFactionId = std::string(obj.at("sourceFactionId").as_string());
            if (obj.contains("targetFactionId")) ev.targetFactionId = std::string(obj.at("targetFactionId").as_string());
            if (obj.contains("powerShift")) ev.powerShift = static_cast<int>(obj.at("powerShift").as_int64());
            if (obj.contains("wealthShift")) ev.wealthShift = static_cast<int>(obj.at("wealthShift").as_int64());
            if (obj.contains("cohesionShift")) ev.cohesionShift = static_cast<int>(obj.at("cohesionShift").as_int64());

            auto typeStr = std::string(obj.at("type").as_string());
            if (typeStr == "Battle") ev.type = GameEvent::Type::Battle;
            else if (typeStr == "TradeDeal") ev.type = GameEvent::Type::TradeDeal;
            else if (typeStr == "Betrayal") ev.type = GameEvent::Type::Betrayal;
            else if (typeStr == "NaturalDisaster") ev.type = GameEvent::Type::NaturalDisaster;
            else if (typeStr == "DiplomaticShift") ev.type = GameEvent::Type::DiplomaticShift;
            else if (typeStr == "RefugeeWave") ev.type = GameEvent::Type::RefugeeWave;
            else if (typeStr == "Plague") ev.type = GameEvent::Type::Plague;
            else if (typeStr == "Discovery") ev.type = GameEvent::Type::Discovery;
            else if (typeStr == "Assassination") ev.type = GameEvent::Type::Assassination;

            m_events->addEvent(std::move(ev));
        }
    } catch (const std::exception& e) {
        SDL_Log("Failed to load events: %s", e.what());
    }

    m_rumors = std::make_unique<RumorPropagator>(*m_knowledge);

    // Register topics
    m_topicRegistry->registerTopic("ugarit_sack", "the Sack of Ugarit", "events");
    m_topicRegistry->registerTopic("sea_peoples", "the Sea Peoples", "factions");
    m_topicRegistry->registerTopic("byblos_king", "the King of Byblos", "people");
    m_topicRegistry->registerTopic("copper_trade", "the Copper Trade", "resources");
    m_knowledge->markTopicKnown("ugarit_sack");
    m_knowledge->markTopicKnown("sea_peoples");
    m_knowledge->markTopicKnown("byblos_king");

    // Player entity
    m_playerEntity = m_entityManager->createEntity();
    m_entityManager->addComponent<Position>(m_playerEntity, Position{
        .worldPos = {0.f, 0.f}, .tilePos = {0, 0}, .zOrder = 1.f
    });
    m_entityManager->addComponent<Sprite>(m_playerEntity, Sprite{
        .textureName = "player", .origin = {16.f, 16.f},
        .color = {0.3f, 0.8f, 0.3f, 1.f}, .scale = 1.f, .visible = true
    });
    m_entityManager->addComponent<CombatStats>(m_playerEntity, CombatStats{
        .team = Team::Player, .maxHp = 20, .hp = 20,
        .attack = 4, .defense = 3, .attackRange = 80.f
    });

    // Load NPCs from JSON
    try {
        auto json = boost::json::parse(readFile("assets/data/npcs.json"));
        for (const auto& item : json.as_object().at("npcs").as_array()) {
            auto& obj = item.as_object();
            std::string id = std::string(obj.at("id").as_string());
            std::string name = std::string(obj.at("name").as_string());
            std::string personality = std::string(obj.at("personality").as_string());
            float x = static_cast<float>(obj.at("x").as_int64());
            float y = static_cast<float>(obj.at("y").as_int64());

            std::vector<NPCKnowledgeEntry> facts;
            if (obj.contains("knowledge")) {
                for (const auto& k : obj.at("knowledge").as_array()) {
                    auto& ko = k.as_object();
                    NPCKnowledgeEntry e;
                    e.factId = std::string(ko.at("factId").as_string());
                    e.version = std::string(ko.at("version").as_string());
                    if (ko.contains("confidence")) e.confidence = static_cast<int>(ko.at("confidence").as_int64());
                    if (ko.contains("witnessed")) e.witnessed = ko.at("witnessed").as_bool();
                    if (ko.contains("source")) e.source = std::string(ko.at("source").as_string());
                    facts.push_back(std::move(e));
                }
            }
            spawnNPC(id, name, x, y, personality, facts);

            // Spawn guards for captains
            if (obj.contains("captain") && obj.at("captain").as_bool()) {
                int guardCount = obj.contains("guards")
                    ? static_cast<int>(obj.at("guards").as_int64()) : 3;
                Team team = personality == "hostile" ? Team::Enemy : Team::Player;
                auto* npcEnt = m_entityManager->getComponent<Position>(m_npcEntities.back());
                Vec2f center = npcEnt ? npcEnt->worldPos : Vec2f{x, y};
                for (int g = 0; g < guardCount; ++g) {
                    auto gid = m_entityManager->createEntity();
                    float angle = (float)g / (float)guardCount * 6.28318f;
                    Vec2f gpos{center.x + std::cos(angle) * 50.f,
                               center.y + std::sin(angle) * 50.f};
                    m_entityManager->addComponent<Position>(gid, Position{
                        .worldPos = gpos, .tilePos = {0,0}, .zOrder = 0.5f
                    });
                    m_entityManager->addComponent<Sprite>(gid, Sprite{
                        .origin = {10.f, 10.f},
                        .color = team == Team::Enemy
                            ? SDL_FColor{0.8f, 0.3f, 0.1f, 1.f}
                            : SDL_FColor{0.3f, 0.6f, 0.9f, 1.f},
                        .scale = 0.7f, .visible = true
                    });
                    m_entityManager->addComponent<CombatStats>(gid, CombatStats{
                        .team = team, .maxHp = 10, .hp = 10,
                        .attack = 3, .defense = 2, .attackRange = 70.f
                    });
                    m_entityManager->addComponent<SoldierAI>(gid, SoldierAI{
                        .followTarget = m_npcEntities.back(),
                        .formationOffset = {gpos.x - center.x, gpos.y - center.y},
                        .followDistance = 32.f,
                        .engageRange = 180.f
                    });
                }
            }
        }
    } catch (const std::exception& e) {
        SDL_Log("Failed to load NPCs: %s", e.what());
        return false;
    }

    // Block NPC tiles for pathfinding
    for (auto eid : m_npcEntities) {
        auto* p = m_entityManager->getComponent<Position>(eid);
        if (p) m_navigationSystem->setWalkable(p->tilePos, false);
    }

    // Place wall obstacles
    for (int x = -3; x <= 3; ++x) {
        m_navigationSystem->setWalkable({x, -5}, false);
        m_navigationSystem->setWalkable({x, 5}, false);
    }
    m_navigationSystem->setWalkable({0, -3}, false);
    m_navigationSystem->setWalkable({0, 3}, false);

    m_worldState->revealRadius({0, 0}, 8);
    m_gameClock->restart();
    m_running = true;
    return true;
}

void App::spawnNPC(const std::string& id, const std::string& name,
                    float x, float y, const std::string& personality,
                    const std::vector<NPCKnowledgeEntry>& knownFacts) {
    auto eid = m_entityManager->createEntity();
    int tx = static_cast<int>(x / 64.f);
    int ty = static_cast<int>(y / 64.f);
    m_entityManager->addComponent<Position>(eid, Position{
        .worldPos = {x, y}, .tilePos = {tx, ty}, .zOrder = 1.f
    });
    SDL_FColor col;
    if (personality == "hostile") col = {0.8f, 0.2f, 0.2f, 1.f};
    else if (personality == "guarded") col = {0.6f, 0.6f, 0.8f, 1.f};
    else if (personality == "fearful") col = {0.8f, 0.5f, 0.8f, 1.f};
    else col = {0.8f, 0.6f, 0.2f, 1.f};

    m_entityManager->addComponent<Sprite>(eid, Sprite{
        .textureName = "", .origin = {16.f, 16.f},
        .color = col, .scale = 1.f, .visible = true
    });
    m_entityManager->addComponent<Interactable>(eid, {64.f, true});

    NPCState npc;
    npc.npcId = id;
    npc.displayName = name;
    npc.personality = personality;
    for (const auto& kf : knownFacts) {
        npc.knowledge[kf.factId] = {
            kf.factId, kf.version,
            kf.confidence, kf.witnessed, kf.source
        };
    }
    m_entityManager->addComponent<NPCState>(eid, npc);
    m_entityManager->addComponent<CombatStats>(eid, CombatStats{
        .team = Team::Neutral, .maxHp = 15, .hp = 15,
        .attack = 2, .defense = 1, .attackRange = 60.f
    });
    m_relationships->setRelation(id, {});
    m_npcEntities.push_back(eid);
}

EntityId App::findNearestInteractable() const {
    auto* playerPos = m_entityManager->getComponent<Position>(m_playerEntity);
    if (!playerPos) return INVALID_ENTITY;

    EntityId nearest = INVALID_ENTITY;
    float nearestDist = 1e9f;

    for (auto eid : m_entityManager->allEntities()) {
        if (eid == m_playerEntity) continue;
        if (!m_entityManager->getComponent<Interactable>(eid)) continue;
        auto* npcCs = m_entityManager->getComponent<CombatStats>(eid);
        if (npcCs && !npcCs->alive) continue;
        auto* pos = m_entityManager->getComponent<Position>(eid);
        if (!pos) continue;

        Vec2f diff = pos->worldPos - playerPos->worldPos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y);
        if (dist < nearestDist) { nearestDist = dist; nearest = eid; }
    }
    return (nearest != INVALID_ENTITY && nearestDist < 80.f) ? nearest : INVALID_ENTITY;
}

void App::handleInteraction() {
    if (m_dialogueState.active) { endDialogue(); return; }

    auto eid = findNearestInteractable();
    if (eid == INVALID_ENTITY) return;

    auto* npc = m_entityManager->getComponent<NPCState>(eid);
    if (!npc) return;

    auto* rel = m_relationships->getRelation(npc->npcId);
    int trust = rel ? rel->trust : 0;
    auto resp = m_dialogueEngine->generateGreeting(*npc, trust);
    startDialogue(eid, npc->npcId, npc->displayName);
    m_dialogueState.history.push_back({DialogueLine::NPC, "", resp.text, true, npc->displayName});
    m_dialogueState.npcTrust = trust;

    // NPC's knowledge as topics
    for (const auto& [topicId, known] : npc->knowledge) {
        m_dialogueState.availableTopics.push_back(topicId);
    }
    // Player's knowledge to tell NPC
    for (const auto& t : m_knowledge->knownTopics()) {
        if (!npc->knowledge.contains(t)) {
            m_dialogueState.availableActions.push_back("tell:" + t);
        }
    }
    if (npc->personality == "hostile") {
        m_dialogueState.availableTopics.push_back("__attack__");
        m_dialogueState.canThreaten = true;
    }
    auto* npcCs = m_entityManager->getComponent<CombatStats>(eid);
    if (npcCs) {
        m_dialogueState.canTrade = true;
        m_dialogueState.canGift = true;
    }
    if (npc->personality == "friendly") {
        m_dialogueState.canGift = true;
    }

    // Check if NPC has a quest
    for (const auto* q : m_quests->availableQuests()) {
        if (q->giver == npc->npcId) {
            m_dialogueState.availableTopics.push_back("__quest__");
            break;
        }
    }
    // Check for turn-in
    for (const auto* q : m_quests->activeQuests()) {
        if (q->giver == npc->npcId) {
            m_dialogueState.availableTopics.push_back("__quest_turnin__");
            break;
        }
    }
}

void App::startDialogue(EntityId npcEntity, const std::string& npcId,
                         const std::string& npcName) {
    m_dialogueState = {};
    m_dialogueState.active = true;
    m_dialogueState.npcEntity = npcEntity;
    m_dialogueState.npcId = npcId;
    m_dialogueState.npcName = npcName;
}

void App::endDialogue() { m_dialogueState = {}; }

void App::setUILanguage(int langIndex) {
    m_locale->setLanguage(langIndex);
    m_dialogueEngine->setLanguage(
        langIndex == 1 ? DialogueEngine::Language::Chinese : DialogueEngine::Language::English);

    if (m_dialogueState.active) {
        auto eid = m_dialogueState.npcEntity;
        auto* npc = m_entityManager->getComponent<NPCState>(eid);
        m_dialogueState.history.clear();
        m_dialogueState.availableTopics.clear();
        m_dialogueState.availableActions.clear();
        if (npc) {
            auto* rel = m_relationships->getRelation(npc->npcId);
            auto resp = m_dialogueEngine->generateGreeting(*npc, rel ? rel->trust : 0);
            m_dialogueState.history.push_back(
                {DialogueLine::NPC, "", resp.text, true, npc->displayName});
            for (const auto& [topicId, known] : npc->knowledge)
                m_dialogueState.availableTopics.push_back(topicId);
            if (npc->personality == "hostile") {
                m_dialogueState.availableTopics.push_back("__attack__");
                m_dialogueState.canThreaten = true;
            }
            m_dialogueState.canGift = (npc->personality == "friendly");
        }
    }
}

void App::doDialogueAction(const std::string& action) {
    if (!m_dialogueState.active) return;
    auto eid = m_dialogueState.npcEntity;
    auto* npc = m_entityManager->getComponent<NPCState>(eid);
    if (!npc) return;

    auto L = [this](const std::string& k) { return m_locale->get(k); };
    auto playerName = L("dialogue.player");
    if (playerName.empty()) playerName = "You";  // absolute fallback

    if (action == "__quest__") {
        for (auto* q : m_quests->availableQuests()) {
            if (q->giver == npc->npcId) {
                m_dialogueState.history.push_back({DialogueLine::Player, "quest.ask_help"});
                m_dialogueState.history.push_back({DialogueLine::NPC, q->titleKey});
                m_dialogueState.history.push_back({DialogueLine::NPC, q->descKey});
                m_quests->acceptQuest(q->id);
                m_dialogueState.history.push_back({DialogueLine::NPC, "quest.accepted"});
                return;
            }
        }
        return;
    }
    if (action == "__quest_turnin__") {
        for (auto* q : m_quests->activeQuests()) {
            if (q->giver != npc->npcId) continue;
            bool allDone = true;
            for (const auto& obj : q->objectives) {
                if (obj.progress < obj.count) { allDone = false; break; }
            }
            if (allDone) {
                m_dialogueState.history.push_back({DialogueLine::Player, "quest.turnin"});
                m_quests->completeQuest(q->id);
                m_relationships->modifyTrust(npc->npcId, q->rewardTrust);
                m_dialogueState.npcTrust += q->rewardTrust;
                m_dialogueState.history.push_back({DialogueLine::NPC, "quest.reward"});
            } else {
                m_dialogueState.history.push_back({DialogueLine::Player, "quest.turnin"});
                m_dialogueState.history.push_back({DialogueLine::NPC, "quest.not_done"});
            }
            return;
        }
        return;
    }
    // Track talk objectives
    m_quests->reportTalk(npc->npcId);
    if (action == "__attack__") {
        auto* npcCs = m_entityManager->getComponent<CombatStats>(eid);
        if (npcCs) { npcCs->team = Team::Enemy; npcCs->alive = true; }
        m_dialogueState.history.push_back({DialogueLine::Player, "resp.attack_you"});
        m_dialogueState.history.push_back({DialogueLine::NPC,
            npc->personality == "hostile" ? "resp.attack_hostile" : "resp.attack_neutral"});
        endDialogue();
        return;
    }
    if (action == "__gift__") {
        m_relationships->modifyTrust(npc->npcId, 8);
        m_dialogueState.npcTrust += 8;
        m_dialogueState.history.push_back({DialogueLine::Player, "resp.gift_you"});
        m_dialogueState.history.push_back({DialogueLine::NPC,
            npc->personality == "friendly" ? "resp.gift_friendly" : "resp.gift_neutral"});
        return;
    }
    if (action == "__threaten__") {
        m_relationships->modifyFear(npc->npcId, 15);
        m_dialogueState.npcFear += 15;
        m_dialogueState.history.push_back({DialogueLine::Player, "resp.threaten_you"});
        m_dialogueState.history.push_back({DialogueLine::NPC,
            npc->personality == "hostile" ? "resp.threaten_hostile" : "resp.threaten_neutral"});
        return;
    }
    if (action.starts_with("tell:")) {
        std::string topicId = action.substr(5);
        auto& dn = m_topicRegistry->displayName(topicId);
        auto tellText = m_locale->fmt("dialogue.tell_prefix", dn.empty() ? topicId : dn);
        m_dialogueState.history.push_back({DialogueLine::Player, "", tellText, true});
        std::string respKey = npc->knowledge.contains(topicId)
            ? "resp.already_known" : "resp.learned";
        if (!npc->knowledge.contains(topicId)) {
            m_relationships->modifyTrust(npc->npcId, 5);
            m_dialogueState.npcTrust += 5;
        }
        m_dialogueState.history.push_back({DialogueLine::NPC, respKey});
        return;
    }

    // Regular topic ask
    auto* rel = m_relationships->getRelation(npc->npcId);
    int trust = rel ? rel->trust : 0;
    auto& dn = m_topicRegistry->displayName(action);
    auto resp = m_dialogueEngine->generateAskResponse(*npc, action,
        dn.empty() ? action : dn, trust);

    auto askText = m_locale->fmt("dialogue.ask_prefix", dn.empty() ? action : dn);
    m_dialogueState.history.push_back({DialogueLine::Player, "", askText, true});
    m_dialogueState.history.push_back({DialogueLine::NPC, "", resp.text, true, npc->displayName});

    if (resp.trustDelta != 0) {
        m_relationships->modifyTrust(npc->npcId, resp.trustDelta);
        m_dialogueState.npcTrust += resp.trustDelta;
    }

    if (resp.isTruthful && !resp.factId.empty() && npc->knowledge.contains(action)) {
        Fact f;
        f.id = action + "_from_" + npc->npcId;
        f.description = npc->knowledge[action].npcVersion;
        f.origins.push_back({Fact::Origin::Source::NPCTestimony,
            npc->npcId, "", m_worldState->day(), npc->knowledge[action].confidence});
        m_knowledge->addOrUpdateFact(f);
    }
}

void App::askTopic(const std::string& topicId) {
    doDialogueAction(topicId);
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
    m_playerEntity = INVALID_ENTITY;
    m_npcEntities.clear();
    m_rumors.reset();
    m_events.reset();
    m_factions.reset();
    m_knowledge.reset();
    m_dialogueEngine.reset();
    m_topicRegistry.reset();
    m_relationships.reset();
    m_locale.reset();
    m_survival.reset();
    m_network->disconnect();
    m_network.reset();
    m_quests.reset();
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
                bool dead = isPlayerDead();
                switch (event.key.scancode) {
                    case SDL_SCANCODE_E: if (!dead) handleInteraction(); break;
                    case SDL_SCANCODE_R: if (!dead) doRest(); break;
                    case SDL_SCANCODE_F2: if (!dead) recruitSoldier(); break;
                    case SDL_SCANCODE_F1: m_showHelp = !m_showHelp; break;
                    case SDL_SCANCODE_F10: m_showMultiplayer = !m_showMultiplayer; break;
                    case SDL_SCANCODE_F5: quickSave(); break;
                    case SDL_SCANCODE_F9: m_showLoadMenu = !m_showLoadMenu; break;
                    default: break;
                }
                break;
        }
    }
    m_input->update();
}

void App::update(float dt) {
    m_worldState->update(dt);
    auto prevCount = m_events->triggeredEvents().size();
    m_events->update(m_worldState->day());
    // Auto-spawn enemies from new events
    if (m_events->triggeredEvents().size() > prevCount) {
        auto* pPos = m_entityManager->getComponent<Position>(m_playerEntity);
        Vec2f center = pPos ? pPos->worldPos : Vec2f{0.f, 0.f};
        auto& latest = m_events->triggeredEvents().back();
        if (latest.type == GameEvent::Type::Battle ||
            latest.type == GameEvent::Type::RefugeeWave) {
            int count = 2 + std::rand() % 4;
            m_combat->spawnEnemyWave(*m_entityManager, count, center, 400.f, Team::Enemy);
            SDL_Log("%s", latest.description.c_str());
        }
    }
    m_survival->update(dt, false, false);

    // Network sync (every 100ms)
    if (m_network->isConnected()) {
        m_netSyncTimer += dt;
        if (m_netSyncTimer >= 0.1f) {
            m_netSyncTimer = 0.f;
            auto* ps = m_entityManager->getComponent<CombatStats>(m_playerEntity);
            auto* pp = m_entityManager->getComponent<Position>(m_playerEntity);
            if (ps && pp)
                m_network->sendEntityUpdate(0, pp->worldPos, ps->hp, ps->maxHp, ps->alive);
        }
        m_network->update();
    }

    // Starvation damages combat HP
    if (m_survival->state().food <= 0.f || m_survival->state().water <= 0.f) {
        auto* pcs = m_entityManager->getComponent<CombatStats>(m_playerEntity);
        if (pcs && pcs->alive) {
            pcs->hp = std::max(0, pcs->hp - 1);
        }
    }
    m_combat->update(*m_entityManager, dt);
    // Report enemy kills for quest tracking
    for (const auto& ev : m_combat->events()) {
        if (ev.killed)
            m_quests->reportKill("enemy");
    }
    if (isPlayerDead() && m_dialogueState.active) endDialogue();
    m_uiManager->update(dt);

    if (m_dialogueState.active) {
        m_cameraSystem->update(dt);
        m_navigationSystem->update(dt);
        return;
    }

    // Check if player is dead
    auto* pcs = m_entityManager->getComponent<CombatStats>(m_playerEntity);
    if (pcs && !pcs->alive) return;

    // Don't move when menus are open (they capture keyboard)
    if (m_showMultiplayer || m_showLoadMenu || m_showHelp) return;

    float moveX = 0.f, moveY = 0.f;
    if (m_input->isPressed(InputManager::Action::MoveUp))    moveY -= 1.f;
    if (m_input->isPressed(InputManager::Action::MoveDown))  moveY += 1.f;
    if (m_input->isPressed(InputManager::Action::MoveLeft))  moveX -= 1.f;
    if (m_input->isPressed(InputManager::Action::MoveRight)) moveX += 1.f;

    auto* pos = m_entityManager->getComponent<Position>(m_playerEntity);
    if (pos && pcs && pcs->alive && (moveX != 0.f || moveY != 0.f)) {
        float len = std::sqrt(moveX * moveX + moveY * moveY);
        m_playerFacing = {moveX / len, moveY / len};
        pos->worldPos.x += (moveX / len) * 200.f * dt;
        pos->worldPos.y += (moveY / len) * 200.f * dt;
        pos->tilePos = {(int)(pos->worldPos.x / 64.f), (int)(pos->worldPos.y / 64.f)};
        m_worldState->revealRadius(pos->tilePos, 8);
        m_survival->update(0.f, true, false);

        // Update soldier formation based on facing
        int soldierIdx = 0;
        const int ROWS = 3;
        const int PER_ROW[] = {3, 4, 5};
        for (auto eid : m_entityManager->allEntities()) {
            auto* ai = m_entityManager->getComponent<SoldierAI>(eid);
            if (!ai || ai->followTarget != m_playerEntity) continue;

            int remaining = ++soldierIdx;
            int row = 0, col = 0;
            for (row = 0; row < ROWS; ++row) {
                if (remaining <= PER_ROW[row]) { col = remaining - 1; break; }
                remaining -= PER_ROW[row];
            }
            if (row >= ROWS) { row = ROWS - 1; col = soldierIdx - PER_ROW[0] - PER_ROW[1] - 1; }

            float spacing = 36.f;
            float ox = (col - (PER_ROW[row] - 1) / 2.f) * spacing;
            float oy = -(50.f + row * 45.f);

            // Rotate by facing
            ai->formationOffset = {
                ox * m_playerFacing.y + oy * m_playerFacing.x,
                ox * -m_playerFacing.x + oy * m_playerFacing.y
            };
        }
    }

    m_cameraSystem->setTarget(pos ? pos->worldPos : Vec2f{});
    m_cameraSystem->update(dt);
    m_navigationSystem->update(dt);
}

void App::render() {
    SDL_SetRenderDrawColor(m_renderer, 10, 10, 15, 255);
    SDL_RenderClear(m_renderer);
    m_renderSystem->render(*m_entityManager, *m_worldState, *m_navigationSystem,
                            m_combat->events(), m_network->remotePlayers());
    m_combat->clearEvents();
    m_uiManager->render(*m_worldState, *this);
    SDL_RenderPresent(m_renderer);
}

const CombatStats* App::playerCombatStats() const {
    return m_entityManager->getComponent<CombatStats>(m_playerEntity);
}

bool App::isPlayerDead() const {
    auto* cs = m_entityManager->getComponent<CombatStats>(m_playerEntity);
    return cs && !cs->alive;
}

void App::quickSave() {
    saveToSlot(m_nextSaveSlot++);
}

void App::saveToSlot(int slot) {
    auto* pos = m_entityManager->getComponent<Position>(m_playerEntity);
    auto* cs = m_entityManager->getComponent<CombatStats>(m_playerEntity);
    Vec2f ppos = pos ? pos->worldPos : Vec2f{};

    std::vector<SaveManager::NPCData> npcData;
    for (auto eid : m_npcEntities) {
        auto* np = m_entityManager->getComponent<Position>(eid);
        auto* ns = m_entityManager->getComponent<NPCState>(eid);
        auto* ncs = m_entityManager->getComponent<CombatStats>(eid);
        if (!np || !ns) continue;
        SaveManager::NPCData nd;
        nd.id = ns->npcId;
        nd.name = ns->displayName;
        nd.personality = ns->personality;
        nd.position = np->worldPos;
        nd.hp = ncs ? ncs->hp : 10;
        nd.maxHp = ncs ? ncs->maxHp : 10;
        nd.alive = ncs ? ncs->alive : true;
        for (const auto& [fid, kf] : ns->knowledge)
            nd.knowledge[fid] = kf.npcVersion;
        npcData.push_back(std::move(nd));
    }

    std::string path = "saves/save_" + std::to_string(slot) + ".json";
    SaveManager::save(path, *m_worldState, *m_knowledge, *m_relationships,
                      ppos, cs ? cs->hp : 20, cs ? cs->maxHp : 20, npcData);
    SDL_Log("Saved to slot %d", slot);
}

void App::loadFromSlot(int slot) {
    std::string path = "saves/save_" + std::to_string(slot) + ".json";
    quickLoad(path);
    m_showLoadMenu = false;
}

std::vector<int> App::availableSaveSlots() const {
    std::vector<int> slots;
    for (int i = 1; i <= 10; ++i) {
        std::ifstream f("saves/save_" + std::to_string(i) + ".json");
        if (f.good()) slots.push_back(i);
    }
    return slots;
}

void App::quickLoad(const std::string& path) {
    SaveManager::SaveData data;
    if (!SaveManager::load(path, data)) {
        SDL_Log("%s", m_locale->get("resp.no_save").c_str());
        return;
    }

    // Full reset: clear entities and rebuild
    m_npcEntities.clear();
    while (!m_entityManager->allEntities().empty()) {
        m_entityManager->destroyEntity(m_entityManager->allEntities().back());
    }

    m_playerEntity = m_entityManager->createEntity();
    m_entityManager->addComponent<Position>(m_playerEntity, Position{
        .worldPos = data.playerPos,
        .tilePos = {(int)(data.playerPos.x / 64.f), (int)(data.playerPos.y / 64.f)},
        .zOrder = 1.f
    });
    m_entityManager->addComponent<Sprite>(m_playerEntity, Sprite{
        .textureName = "player", .origin = {16.f, 16.f},
        .color = {0.3f, 0.8f, 0.3f, 1.f}, .scale = 1.f, .visible = true
    });
    m_entityManager->addComponent<CombatStats>(m_playerEntity, CombatStats{
        .team = Team::Player, .maxHp = 20, .hp = 20,
        .attack = 4, .defense = 3, .attackRange = 80.f
    });

    // Restore world
    m_worldState->setDay(data.day);
    m_worldState->setSeason(data.season);
    for (const auto& t : data.seenTiles)
        m_worldState->revealTile(t);

    // Restore player HP
    auto* cs = m_entityManager->getComponent<CombatStats>(m_playerEntity);
    if (cs) { cs->hp = data.playerHp; cs->maxHp = data.playerMaxHp; }

    // Restore topics
    m_knowledge->markTopicKnown("ugarit_sack");
    m_knowledge->markTopicKnown("sea_peoples");
    m_knowledge->markTopicKnown("byblos_king");
    for (const auto& t : data.knownTopics) {
        m_knowledge->markTopicKnown(t);
    }

    // Restore relationships
    m_relationships = std::make_unique<RelationshipTable>();
    for (const auto& [npcId, rel] : data.relations) {
        m_relationships->setRelation(npcId, {rel[0], rel[1], rel[2]});
    }

    // Restore NPCs from save data
    for (const auto& nd : data.npcs) {
        std::vector<NPCKnowledgeEntry> facts;
        for (const auto& [fid, ver] : nd.knowledge)
            facts.push_back({fid, ver, 70, false, ""});
        spawnNPC(nd.id, nd.name, nd.position.x, nd.position.y, nd.personality, facts);

        // Restore NPC combat stats
        auto& eid = m_npcEntities.back();
        auto* ncs = m_entityManager->getComponent<CombatStats>(eid);
        if (ncs) {
            ncs->hp = nd.hp;
            ncs->maxHp = nd.maxHp;
            ncs->alive = nd.alive;
        }
    }

    // Restore NPC tile blocking
    for (auto eid : m_npcEntities) {
        auto* p = m_entityManager->getComponent<Position>(eid);
        if (p) m_navigationSystem->setWalkable(p->tilePos, false);
    }

    m_cameraSystem->centerOn(data.playerPos);
    SDL_Log("%s", m_locale->get("resp.loaded").c_str());
}

void App::recruitSoldier() {
    auto* pPos = m_entityManager->getComponent<Position>(m_playerEntity);
    if (!pPos) return;

    static int soldierNum = 0;
    soldierNum++;

    auto eid = m_entityManager->createEntity();

    // Half-circle formation behind the player
    const int ROWS = 3;
    const int PER_ROW[] = {3, 4, 5};
    int remaining = soldierNum;
    int row = 0;
    int colInRow = 0;
    for (row = 0; row < ROWS; ++row) {
        if (remaining <= PER_ROW[row]) {
            colInRow = remaining - 1;
            break;
        }
        remaining -= PER_ROW[row];
    }
    if (row >= ROWS) {
        row = ROWS - 1;
        colInRow = soldierNum - PER_ROW[0] - PER_ROW[1] - 1;
    }
    float spacing = 40.f;
    float offsetX = (colInRow - (PER_ROW[row] - 1) / 2.f) * spacing;
    float offsetY = -(50.f + row * 50.f);
    Vec2f offset{offsetX, offsetY};

    m_entityManager->addComponent<Position>(eid, Position{
        .worldPos = {pPos->worldPos.x + offset.x, pPos->worldPos.y + offset.y},
        .tilePos = {0, 0}, .zOrder = 0.8f
    });
    m_entityManager->addComponent<Sprite>(eid, Sprite{
        .origin = {12.f, 12.f},
        .color = {0.3f, 0.5f, 0.9f, 1.f},
        .scale = 0.8f, .visible = true
    });
    m_entityManager->addComponent<CombatStats>(eid, CombatStats{
        .team = Team::Player, .maxHp = 12, .hp = 12,
        .attack = 3, .defense = 2, .attackRange = 80.f
    });
    m_entityManager->addComponent<SoldierAI>(eid, SoldierAI{
        .followTarget = m_playerEntity,
        .formationOffset = offset,
        .followDistance = 32.f,
        .engageRange = 200.f
    });
    SDL_Log("%s", m_locale->get("resp.recruited").c_str());
}

void App::doRest() {
    // Rest consumes time and restores health
    auto* pcs = m_entityManager->getComponent<CombatStats>(m_playerEntity);
    if (pcs && pcs->alive) {
        pcs->hp = std::min(pcs->maxHp, pcs->hp + 5);
        m_survival->heal(5.f);
        m_worldState->update(m_worldState->day() == 0 ? 0.f : 2.f);  // Pass 2 game hours
        SDL_Log("%s HP: %d/%d", m_locale->get("resp.rested").c_str(), pcs->hp, pcs->maxHp);
    }
}

void App::spawnTestEnemies() {
    auto* pPos = m_entityManager->getComponent<Position>(m_playerEntity);
    Vec2f center = pPos ? pPos->worldPos : Vec2f{200.f, 0.f};
    m_combat->spawnEnemyWave(*m_entityManager, 3, center, 200.f, Team::Enemy);
    SDL_Log("Enemies spawned!");
}

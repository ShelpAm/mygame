#include "core/GameMode.hpp"
#include "core/InputManager.hpp"
#include "core/LocaleManager.hpp"
#include "entities/EntityManager.hpp"
#include <imgui.h>
#include "entities/components/Sprite.hpp"
#include "entities/components/CombatStats.hpp"
#include "entities/components/SoldierAI.hpp"
#include "entities/components/Interactable.hpp"
#include "knowledge/KnowledgeGraph.hpp"
#include "dialogue/DialogueEngine.hpp"
#include "dialogue/TopicRegistry.hpp"
#include "dialogue/RelationshipTable.hpp"
#include "factions/FactionNetwork.hpp"
#include "factions/EventSimulator.hpp"
#include "knowledge/RumorPropagator.hpp"
#include "systems/CombatSystem.hpp"
#include "systems/QuestManager.hpp"
#include "net/NetworkManager.hpp"
#include "world/WorldState.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

GameMode::GameMode(EntityManager& em) : m_em(em) {
    m_dialogue = std::make_unique<DialogueState>();
}

void GameMode::initWorld(WorldState& ws, KnowledgeGraph& kg, DialogueEngine& de,
                          TopicRegistry& tr, RelationshipTable& rt, FactionNetwork& fn,
                          EventSimulator& es, RumorPropagator& rp, CombatSystem& cs,
                          QuestManager& qm, NetworkManager& net) {
    m_ws = &ws; m_kg = &kg; m_de = &de; m_tr = &tr; m_rt = &rt;
    m_cs = &cs; m_qm = &qm;

    // Player
    m_playerEntity = spawnPlayer(0, 0);

    // Topics
    tr.registerTopic("ugarit_sack", "the Sack of Ugarit", "events");
    tr.registerTopic("sea_peoples", "the Sea Peoples", "factions");
    tr.registerTopic("byblos_king", "the King of Byblos", "people");
    tr.registerTopic("copper_trade", "the Copper Trade", "resources");
    kg.markTopicKnown("ugarit_sack");
    kg.markTopicKnown("sea_peoples");
    kg.markTopicKnown("byblos_king");

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
            if (obj.contains("relations"))
                for (const auto& [k, v] : obj.at("relations").as_object())
                    f.relations[std::string(k)] = static_cast<int>(v.as_int64());
            fn.addFaction(std::move(f));
        }
    } catch (...) {}

    // Load events
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
            auto ts = std::string(obj.at("type").as_string());
            if (ts == "Battle") ev.type = GameEvent::Type::Battle;
            else if (ts == "TradeDeal") ev.type = GameEvent::Type::TradeDeal;
            else if (ts == "Betrayal") ev.type = GameEvent::Type::Betrayal;
            else if (ts == "NaturalDisaster") ev.type = GameEvent::Type::NaturalDisaster;
            else if (ts == "DiplomaticShift") ev.type = GameEvent::Type::DiplomaticShift;
            else if (ts == "RefugeeWave") ev.type = GameEvent::Type::RefugeeWave;
            else if (ts == "Plague") ev.type = GameEvent::Type::Plague;
            else if (ts == "Discovery") ev.type = GameEvent::Type::Discovery;
            else if (ts == "Assassination") ev.type = GameEvent::Type::Assassination;
            es.addEvent(std::move(ev));
        }
    } catch (...) {}

    // Load NPCs
    try {
        auto json = boost::json::parse(readFile("assets/data/npcs.json"));
        for (const auto& item : json.as_object().at("npcs").as_array()) {
            auto& obj = item.as_object();
            std::string id = std::string(obj.at("id").as_string());
            std::string name = std::string(obj.at("name").as_string());
            std::string pers = std::string(obj.at("personality").as_string());
            float x = static_cast<float>(obj.at("x").as_int64());
            float y = static_cast<float>(obj.at("y").as_int64());
            std::vector<NPCKnowledgeEntry> facts;
            if (obj.contains("knowledge"))
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
            spawnNPC(id, name, x, y, pers, facts);
            if (obj.contains("captain") && obj.at("captain").as_bool()) {
                int gc = obj.contains("guards") ? static_cast<int>(obj.at("guards").as_int64()) : 3;
                Team t = pers == "hostile" ? Team::Enemy : Team::Player;
                spawnGuards(m_npcEntities.back(), gc, t);
            }
        }
    } catch (...) {}

    ws.revealRadius({0, 0}, 8);
}

EntityId GameMode::spawnPlayer(float x, float y) {
    auto eid = m_em.createEntity();
    m_em.addComponent<Position>(eid, Position{{x, y}, {0, 0}, 1.f});
    m_em.addComponent<Sprite>(eid, Sprite{"player", {}, {16, 16}, {0.3f, 0.8f, 0.3f, 1.f}, 1.f, true});
    m_em.addComponent<CombatStats>(eid, CombatStats{Team::Player, 20, 20, 4, 3, 80.f});
    return eid;
}

void GameMode::spawnNPC(const std::string& id, const std::string& name,
                         float x, float y, const std::string& personality,
                         const std::vector<NPCKnowledgeEntry>& knownFacts) {
    auto eid = m_em.createEntity();
    m_em.addComponent<Position>(eid, Position{{x, y}, {static_cast<int>(x/64.f), static_cast<int>(y/64.f)}, 1.f});
    SDL_FColor col{0.8f, 0.6f, 0.2f, 1.f};
    if (personality == "hostile") col = {0.8f, 0.2f, 0.2f, 1.f};
    else if (personality == "guarded") col = {0.6f, 0.6f, 0.8f, 1.f};
    else if (personality == "fearful") col = {0.8f, 0.5f, 0.8f, 1.f};
    m_em.addComponent<Sprite>(eid, Sprite{"", {}, {16, 16}, col, 1.f, true});
    m_em.addComponent<Interactable>(eid, {64.f, true});
    NPCState npc;
    npc.npcId = id; npc.displayName = name; npc.personality = personality;
    for (const auto& kf : knownFacts)
        npc.knowledge[kf.factId] = {kf.factId, kf.version, kf.confidence, kf.witnessed, kf.source};
    m_em.addComponent<NPCState>(eid, npc);
    m_em.addComponent<CombatStats>(eid, CombatStats{Team::Neutral, 15, 15, 2, 1, 60.f});
    m_rt->setRelation(id, {});
    m_npcEntities.push_back(eid);
}

EntityId GameMode::spawnSoldier(EntityId leader, int index, const Vec2f& facing) {
    const int PER_ROW[] = {3, 4, 5};
    int rem = index + 1, row = 0, col = 0;
    for (row = 0; row < 3; ++row) { if (rem <= PER_ROW[row]) { col = rem - 1; break; } rem -= PER_ROW[row]; }
    if (row >= 3) { row = 2; col = index - PER_ROW[0] - PER_ROW[1]; }
    float s = 36.f;
    float ox = (col - (PER_ROW[row] - 1) / 2.f) * s;
    float oy = -(50.f + row * 45.f);
    Vec2f off{ox * facing.y + oy * facing.x, ox * -facing.x + oy * facing.y};

    auto* lp = m_em.getComponent<Position>(leader);
    Vec2f start = lp ? lp->worldPos + off : Vec2f{off.x, off.y};

    auto eid = m_em.createEntity();
    m_em.addComponent<Position>(eid, Position{start, {0,0}, 0.8f});
    m_em.addComponent<Sprite>(eid, Sprite{"", {}, {12,12}, {0.3f, 0.5f, 0.9f, 1.f}, 0.8f, true});
    m_em.addComponent<CombatStats>(eid, CombatStats{Team::Player, 12, 12, 3, 2, 80.f});
    m_em.addComponent<SoldierAI>(eid, SoldierAI{leader, off, 32.f, 200.f});
    return eid;
}

void GameMode::spawnGuards(EntityId captainEid, int count, Team team) {
    auto* cp = m_em.getComponent<Position>(captainEid);
    Vec2f center = cp ? cp->worldPos : Vec2f{};
    SDL_FColor col = team == Team::Enemy ? SDL_FColor{0.8f, 0.3f, 0.1f, 1.f} : SDL_FColor{0.3f, 0.6f, 0.9f, 1.f};
    for (int g = 0; g < count; ++g) {
        float a = (float)g / count * 6.28318f;
        Vec2f gp{center.x + std::cos(a)*50.f, center.y + std::sin(a)*50.f};
        auto eid = m_em.createEntity();
        m_em.addComponent<Position>(eid, Position{gp, {0,0}, 0.5f});
        m_em.addComponent<Sprite>(eid, Sprite{"", {}, {10,10}, col, 0.7f, true});
        m_em.addComponent<CombatStats>(eid, CombatStats{team, 10, 10, 3, 2, 70.f});
        m_em.addComponent<SoldierAI>(eid, SoldierAI{captainEid, {gp.x-center.x, gp.y-center.y}, 32.f, 180.f});
    }
}
void GameMode::handleInteraction(LocaleManager& loc) {
    if (m_dialogue->active) { endDialogue(); return; }
    auto* pp = m_em.getComponent<Position>(m_playerEntity);
    if (!pp) return;
    auto eid = findNearestInteractable(pp->worldPos);
    if (eid == INVALID_ENTITY) return;
    auto* npc = m_em.getComponent<NPCState>(eid);
    if (!npc) return;
    auto* rel = m_rt->getRelation(npc->npcId);
    int trust = rel ? rel->trust : 0;
    auto resp = m_de->generateGreeting(*npc, trust);
    m_dialogue->active = true;
    m_dialogue->npcEntity = eid; m_dialogue->npcId = npc->npcId; m_dialogue->npcName = npc->displayName;
    m_dialogue->history.clear(); m_dialogue->availableTopics.clear(); m_dialogue->availableActions.clear();
    m_dialogue->canGift = false; m_dialogue->canThreaten = false;
    m_dialogue->history.push_back({DialogueLine::NPC, "", resp.text, true, npc->displayName});
    m_dialogue->npcTrust = trust;
    for (const auto& [tid, _] : npc->knowledge) m_dialogue->availableTopics.push_back(tid);
    for (const auto& t : m_kg->knownTopics()) if (!npc->knowledge.contains(t)) m_dialogue->availableActions.push_back("tell:" + t);
    if (npc->personality == "hostile") { m_dialogue->availableTopics.push_back("__attack__"); m_dialogue->canThreaten = true; }
    m_dialogue->canGift = true;
    for (const auto* q : m_qm->availableQuests()) if (q->giver == npc->npcId) { m_dialogue->availableTopics.push_back("__quest__"); break; }
    for (const auto* q : m_qm->activeQuests()) if (q->giver == npc->npcId) { m_dialogue->availableTopics.push_back("__quest_turnin__"); break; }
}

EntityId GameMode::findNearestInteractable(Vec2f playerPos) const {
    EntityId nearest = INVALID_ENTITY; float nearestDist = 80.f;
    for (auto eid : m_em.allEntities()) {
        if (eid == m_playerEntity) continue;
        if (!m_em.getComponent<Interactable>(eid)) continue;
        auto* cs = m_em.getComponent<CombatStats>(eid);
        if (cs && !cs->alive) continue;
        auto* pos = m_em.getComponent<Position>(eid);
        if (!pos) continue;
        float d = std::hypot(pos->worldPos.x - playerPos.x, pos->worldPos.y - playerPos.y);
        if (d < nearestDist) { nearestDist = d; nearest = eid; }
    }
    return nearest;
}

void GameMode::doDialogueAction(const std::string& action, LocaleManager& loc) {
    if (!m_dialogue->active) return;
    auto eid = m_dialogue->npcEntity;
    auto* npc = m_em.getComponent<NPCState>(eid);
    if (!npc) return;

    auto addHistory = [&](DialogueLine::Speaker s, const std::string& key, const std::string& raw = "", bool useRaw = false) {
        m_dialogue->history.push_back({s, key, raw, useRaw, npc->displayName});
    };

    if (action == "__attack__") {
        auto* cs = m_em.getComponent<CombatStats>(eid);
        if (cs) { cs->team = Team::Enemy; cs->alive = true; }
        addHistory(DialogueLine::Player, "resp.attack_you");
        addHistory(DialogueLine::NPC, npc->personality == "hostile" ? "resp.attack_hostile" : "resp.attack_neutral");
        endDialogue(); return;
    }
    if (action == "__gift__") {
        m_rt->modifyTrust(npc->npcId, 8); m_dialogue->npcTrust += 8;
        addHistory(DialogueLine::Player, "resp.gift_you");
        addHistory(DialogueLine::NPC, npc->personality == "friendly" ? "resp.gift_friendly" : "resp.gift_neutral");
        return;
    }
    if (action == "__threaten__") {
        m_rt->modifyFear(npc->npcId, 15); m_dialogue->npcFear += 15;
        addHistory(DialogueLine::Player, "resp.threaten_you");
        addHistory(DialogueLine::NPC, npc->personality == "hostile" ? "resp.threaten_hostile" : "resp.threaten_neutral");
        return;
    }
    if (action == "__quest__") {
        for (auto* q : m_qm->availableQuests()) {
            if (q->giver != npc->npcId) continue;
            addHistory(DialogueLine::Player, "quest.ask_help");
            addHistory(DialogueLine::NPC, q->titleKey);
            addHistory(DialogueLine::NPC, q->descKey);
            m_qm->acceptQuest(q->id);
            addHistory(DialogueLine::NPC, "quest.accepted");
            return;
        }
        return;
    }
    if (action == "__quest_turnin__") {
        for (auto* q : m_qm->activeQuests()) {
            if (q->giver != npc->npcId) continue;
            bool done = true;
            for (const auto& obj : q->objectives) if (obj.progress < obj.count) { done = false; break; }
            addHistory(DialogueLine::Player, "quest.turnin");
            if (done) {
                m_qm->completeQuest(q->id);
                m_rt->modifyTrust(npc->npcId, q->rewardTrust);
                m_dialogue->npcTrust += q->rewardTrust;
                addHistory(DialogueLine::NPC, "quest.reward");
            } else {
                addHistory(DialogueLine::NPC, "quest.not_done");
            }
            return;
        }
        return;
    }
    if (action.starts_with("tell:")) {
        std::string tid = action.substr(5);
        auto& dn = m_tr->displayName(tid);
        addHistory(DialogueLine::Player, "", loc.fmt("dialogue.tell_prefix", dn.empty() ? tid : dn), true);
        bool known = npc->knowledge.contains(tid);
        addHistory(DialogueLine::NPC, known ? "resp.already_known" : "resp.learned");
        if (!known) { m_rt->modifyTrust(npc->npcId, 5); m_dialogue->npcTrust += 5; }
        return;
    }

    // Ask about topic
    auto* rel = m_rt->getRelation(npc->npcId);
    int trust = rel ? rel->trust : 0;
    auto& dn = m_tr->displayName(action);
    auto resp = m_de->generateAskResponse(*npc, action, dn.empty() ? action : dn, trust);
    addHistory(DialogueLine::Player, "", loc.fmt("dialogue.ask_prefix", dn.empty() ? action : dn), true);
    addHistory(DialogueLine::NPC, "", resp.text, true);
    m_qm->reportTalk(npc->npcId);
    if (resp.trustDelta != 0) { m_rt->modifyTrust(npc->npcId, resp.trustDelta); m_dialogue->npcTrust += resp.trustDelta; }
    if (resp.isTruthful && !resp.factId.empty() && npc->knowledge.contains(action)) {
        Fact f;
        f.id = action + "_from_" + npc->npcId;
        f.description = npc->knowledge[action].npcVersion;
        f.origins.push_back({Fact::Origin::Source::NPCTestimony, npc->npcId, "", m_ws->day(), npc->knowledge[action].confidence});
        m_kg->addOrUpdateFact(f);
    }
}

void GameMode::endDialogue() { *m_dialogue = {}; }

void GameMode::update(float dt, InputManager& input, LocaleManager& loc, WorldState& ws) {
    (void)loc;
    auto* cs = m_em.getComponent<CombatStats>(m_playerEntity);
    bool dead = cs && !cs->alive;

    // Movement
    float mx = 0, my = 0;
    // Only block movement when actively typing in an ImGui text input
    bool typing = ImGui::IsAnyItemActive();
    if (!dead && !m_dialogue->active && !typing) {
        if (input.isPressed(InputManager::Action::MoveUp)) my -= 1;
        if (input.isPressed(InputManager::Action::MoveDown)) my += 1;
        if (input.isPressed(InputManager::Action::MoveLeft)) mx -= 1;
        if (input.isPressed(InputManager::Action::MoveRight)) mx += 1;
    }

    auto* pos = m_em.getComponent<Position>(m_playerEntity);
    if (pos && !dead && (mx != 0 || my != 0)) {
        float len = std::hypot(mx, my);
        Vec2f dir{mx / len, my / len};
        pos->worldPos.x += dir.x * 200.f * dt;
        pos->worldPos.y += dir.y * 200.f * dt;
        pos->tilePos = {static_cast<int>(pos->worldPos.x / 64.f), static_cast<int>(pos->worldPos.y / 64.f)};
        m_ws->revealRadius(pos->tilePos, 8);

        // Update soldier formation
        int si = 0;
        for (auto eid : m_em.allEntities()) {
            auto* ai = m_em.getComponent<SoldierAI>(eid);
            if (!ai || ai->followTarget != m_playerEntity) continue;
            const int PER_ROW[] = {3, 4, 5};
            int rem = ++si, row = 0, col = 0;
            for (row = 0; row < 3; ++row) { if (rem <= PER_ROW[row]) { col = rem - 1; break; } rem -= PER_ROW[row]; }
            if (row >= 3) { row = 2; col = si - PER_ROW[0] - PER_ROW[1] - 1; }
            float s = 36.f;
            float ox = (col - (PER_ROW[row] - 1) / 2.f) * s;
            float oy = -(50.f + row * 45.f);
            ai->formationOffset = {ox * dir.y + oy * dir.x, ox * -dir.x + oy * dir.y};
        }
    }

    // Update systems — both sides run combat for responsive damage
    m_cs->update(m_em, dt);
    for (const auto& ev : m_cs->events())
        if (ev.killed) m_qm->reportKill("enemy");

    if (dead && m_dialogue->active) endDialogue();
}

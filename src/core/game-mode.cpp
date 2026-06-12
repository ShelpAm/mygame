#include "core/game-mode.hpp"

#include "dialogue/dialogue-engine.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/interactable.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/sprite.hpp"
#include "entities/entity-manager.hpp"
#include "factions/event-simulator.hpp"
#include "factions/faction-network.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "knowledge/rumor-propagator.hpp"
#include "systems/combat-system.hpp"
#include "systems/quest-manager.hpp"
#include "world/world-state.hpp"
#include <boost/json.hpp>
#include <cmath>
#include <fstream>
#include <imgui.h>
#include <spdlog/spdlog.h>

static std::string readFile(std::string const &path)
{
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
}

GameMode::GameMode() = default;

void GameMode::init_world(WorldState &ws, KnowledgeGraph &kg,
                          DialogueEngine &de, FactionNetwork &fn,
                          EventSimulator &es, RumorPropagator &rp,
                          CombatSystem &cs, QuestManager &qm)
{
    ws_ = &ws;
    kg_ = &kg;
    de_ = &de;
    cs_ = &cs;
    qm_ = &qm;

    // Topics
    topic_registry_.register_topic("ugarit_sack", "the Sack of Ugarit", "events");
    topic_registry_.register_topic("sea_peoples", "the Sea Peoples", "factions");
    topic_registry_.register_topic("byblos_king", "the King of Byblos", "people");
    topic_registry_.register_topic("copper_trade", "the Copper Trade", "resources");
    kg.mark_topic_known("ugarit_sack");
    kg.mark_topic_known("sea_peoples");
    kg.mark_topic_known("byblos_king");

    // Load factions
    try {
        auto json = boost::json::parse(readFile("assets/data/factions.json"));
        for (auto const &item : json.as_object().at("factions").as_array()) {
            auto &obj = item.as_object();
            Faction f;
            f.id = std::string(obj.at("id").as_string());
            f.name = std::string(obj.at("name").as_string());
            f.power = static_cast<int>(obj.at("power").as_int64());
            f.cohesion = static_cast<int>(obj.at("cohesion").as_int64());
            f.wealth = static_cast<int>(obj.at("wealth").as_int64());
            if (obj.contains("relations"))
                for (auto const &[k, v] : obj.at("relations").as_object())
                    f.relations[std::string(k)] =
                        static_cast<int>(v.as_int64());
            fn.add_faction(std::move(f));
        }
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to load factions.json: {}", e.what());
        throw;
    }

    // Load events
    try {
        auto json = boost::json::parse(readFile("assets/data/events.json"));
        for (auto const &item : json.as_object().at("events").as_array()) {
            auto &obj = item.as_object();
            GameEvent ev;
            ev.id = std::string(obj.at("id").as_string());
            ev.description = std::string(obj.at("description").as_string());
            ev.trigger_day = static_cast<int>(obj.at("trigger_day").as_int64());
            if (obj.contains("location_id"))
                ev.location_id = std::string(obj.at("location_id").as_string());
            if (obj.contains("source_faction_id"))
                ev.source_faction_id =
                    std::string(obj.at("source_faction_id").as_string());
            if (obj.contains("target_faction_id"))
                ev.target_faction_id =
                    std::string(obj.at("target_faction_id").as_string());
            if (obj.contains("power_shift"))
                ev.power_shift =
                    static_cast<int>(obj.at("power_shift").as_int64());
            if (obj.contains("wealth_shift"))
                ev.wealth_shift =
                    static_cast<int>(obj.at("wealth_shift").as_int64());
            if (obj.contains("cohesion_shift"))
                ev.cohesion_shift =
                    static_cast<int>(obj.at("cohesion_shift").as_int64());
            auto ts = std::string(obj.at("type").as_string());
            if (ts == "Battle")
                ev.type = GameEvent::Type::battle;
            else if (ts == "TradeDeal")
                ev.type = GameEvent::Type::trade_deal;
            else if (ts == "Betrayal")
                ev.type = GameEvent::Type::betrayal;
            else if (ts == "NaturalDisaster")
                ev.type = GameEvent::Type::natural_disaster;
            else if (ts == "DiplomaticShift")
                ev.type = GameEvent::Type::diplomatic_shift;
            else if (ts == "RefugeeWave")
                ev.type = GameEvent::Type::refugee_wave;
            else if (ts == "Plague")
                ev.type = GameEvent::Type::plague;
            else if (ts == "Discovery")
                ev.type = GameEvent::Type::discovery;
            else if (ts == "Assassination")
                ev.type = GameEvent::Type::assassination;
            es.add_event(std::move(ev));
        }
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to load events.json: {}", e.what());
        throw;
    }

    // Load NPCs
    try {
        auto json = boost::json::parse(readFile("assets/data/npcs.json"));
        for (auto const &item : json.as_object().at("npcs").as_array()) {
            auto &obj = item.as_object();
            std::string id = std::string(obj.at("id").as_string());
            std::string name = std::string(obj.at("name").as_string());
            std::string pers = std::string(obj.at("personality").as_string());
            float x = static_cast<float>(obj.at("x").as_int64());
            float y = static_cast<float>(obj.at("y").as_int64());
            std::vector<NPCKnowledgeEntry> facts;
            if (obj.contains("knowledge"))
                for (auto const &k : obj.at("knowledge").as_array()) {
                    auto &ko = k.as_object();
                    NPCKnowledgeEntry e;
                    e.fact_id = std::string(ko.at("fact_id").as_string());
                    e.version = std::string(ko.at("version").as_string());
                    if (ko.contains("confidence"))
                        e.confidence =
                            static_cast<int>(ko.at("confidence").as_int64());
                    if (ko.contains("witnessed"))
                        e.witnessed = ko.at("witnessed").as_bool();
                    if (ko.contains("source"))
                        e.source = std::string(ko.at("source").as_string());
                    facts.push_back(std::move(e));
                }
            spawn_npc(id, name, x, y, pers, facts);
            if (obj.contains("captain") && obj.at("captain").as_bool()) {
                int gc = obj.contains("guards")
                             ? static_cast<int>(obj.at("guards").as_int64())
                             : 3;
                Team t = pers == "hostile" ? Team::enemy : Team::player;
                spawn_guards(npc_entities_.back(), gc, t);
            }
        }
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to load npcs.json: {}", e.what());
        throw;
    }

    ws.reveal_radius({0, 0}, 8);
}

EntityId GameMode::spawn_player(float x, float y)
{
    auto eid = em_.create_entity();
    em_.add_component<Position>(eid, Position{{x, y}, {0, 0}, 1.f});
    em_.add_component<Sprite>(
        eid,
        Sprite{"player", {}, {16, 16}, {0.3f, 0.8f, 0.3f, 1.f}, 1.f, true});
    em_.add_component<CombatStats>(
        eid, CombatStats{Team::player, 20, 20, 4, 3, 80.f});
    return eid;
}

void GameMode::spawn_npc(std::string const &id, std::string const &name,
                         float x, float y, std::string const &personality,
                         std::vector<NPCKnowledgeEntry> const &known_facts)
{
    auto eid = em_.create_entity();
    em_.add_component<Position>(
        eid, Position{{x, y},
                      {static_cast<int>(x / 64.f), static_cast<int>(y / 64.f)},
                      1.f});
    SDL_FColor col{0.8f, 0.6f, 0.2f, 1.f};
    if (personality == "hostile")
        col = {0.8f, 0.2f, 0.2f, 1.f};
    else if (personality == "guarded")
        col = {0.6f, 0.6f, 0.8f, 1.f};
    else if (personality == "fearful")
        col = {0.8f, 0.5f, 0.8f, 1.f};
    em_.add_component<Sprite>(eid, Sprite{"", {}, {16, 16}, col, 1.f, true});
    em_.add_component<Interactable>(eid, {64.f, true});
    NPCState npc;
    npc.npc_id = id;
    npc.display_name = name;
    npc.personality = personality;
    for (auto const &kf : known_facts)
        npc.knowledge[kf.fact_id] = {kf.fact_id, kf.version, kf.confidence,
                                     kf.witnessed, kf.source};
    em_.add_component<NPCState>(eid, npc);
    em_.add_component<CombatStats>(
        eid, CombatStats{Team::neutral, 15, 15, 2, 1, 60.f});
    relationships_.set_relation(id, {});
    npc_entities_.push_back(eid);
}

EntityId GameMode::spawn_soldier(EntityId leader, int index,
                                 Vec2f const &facing)
{
    int const PER_ROW[] = {3, 4, 5};
    int rem = index + 1, row = 0, col = 0;
    for (row = 0; row < 3; ++row) {
        if (rem <= PER_ROW[row]) {
            col = rem - 1;
            break;
        }
        rem -= PER_ROW[row];
    }
    if (row >= 3) {
        row = 2;
        col = index - PER_ROW[0] - PER_ROW[1];
    }
    float s = 36.f;
    float ox = (col - (PER_ROW[row] - 1) / 2.f) * s;
    float oy = -(50.f + row * 45.f);
    Vec2f off{ox * facing.y + oy * facing.x, ox * -facing.x + oy * facing.y};

    auto *lp = em_.get_component<Position>(leader);
    Vec2f start = lp ? lp->world_pos + off : Vec2f{off.x, off.y};

    auto eid = em_.create_entity();
    em_.add_component<Position>(eid, Position{start, {0, 0}, 0.8f});
    em_.add_component<Sprite>(
        eid, Sprite{"", {}, {12, 12}, {0.3f, 0.5f, 0.9f, 1.f}, 0.8f, true});
    em_.add_component<CombatStats>(
        eid, CombatStats{Team::player, 12, 12, 3, 2, 80.f});
    em_.add_component<SoldierAI>(eid, SoldierAI{leader, off, 32.f, 200.f});
    return eid;
}

void GameMode::spawn_guards(EntityId captain_eid, int count, Team team)
{
    auto *cp = em_.get_component<Position>(captain_eid);
    Vec2f center = cp ? cp->world_pos : Vec2f{};
    SDL_FColor col = team == Team::enemy ? SDL_FColor{0.8f, 0.3f, 0.1f, 1.f}
                                         : SDL_FColor{0.3f, 0.6f, 0.9f, 1.f};
    for (int g = 0; g < count; ++g) {
        float a = (float)g / count * 6.28318f;
        Vec2f gp{center.x + std::cos(a) * 50.f, center.y + std::sin(a) * 50.f};
        auto eid = em_.create_entity();
        em_.add_component<Position>(eid, Position{gp, {0, 0}, 0.5f});
        em_.add_component<Sprite>(eid,
                                  Sprite{"", {}, {10, 10}, col, 0.7f, true});
        em_.add_component<CombatStats>(eid,
                                       CombatStats{team, 10, 10, 3, 2, 70.f});
        em_.add_component<SoldierAI>(
            eid,
            SoldierAI{
                captain_eid, {gp.x - center.x, gp.y - center.y}, 32.f, 180.f});
    }
}
void GameMode::handle_interaction(EntityId player)
{
    if (dialogue_->active) {
        end_dialogue();
        return;
    }
    auto *pp = em_.get_component<Position>(player);
    if (!pp)
        return;
    auto eid = find_nearest_interactable(player, pp->world_pos);
    if (eid == invalid_entity) {
        spdlog::debug("No interactable NPC near player {} at ({}, {})", player,
                      pp->world_pos.x, pp->world_pos.y);
        return;
    }
    auto *npc = em_.get_component<NPCState>(eid);
    if (!npc)
        return;
    auto *rel = relationships_.get_relation(npc->npc_id);
    int trust = rel ? rel->trust : 0;
    auto resp = de_->generate_greeting(*npc, trust);
    dialogue_->active = true;
    dialogue_->npc_entity = eid;
    dialogue_->npc_id = npc->npc_id;
    dialogue_->npc_name = npc->display_name;
    dialogue_->history.clear();
    dialogue_->available_topics.clear();
    dialogue_->available_actions.clear();
    dialogue_->can_gift = false;
    dialogue_->can_threaten = false;
    dialogue_->history.push_back(
        {DialogueLine::npc, "", resp.text, true, npc->display_name});
    dialogue_->npc_trust = trust;
    for (auto const &[tid, _] : npc->knowledge)
        dialogue_->available_topics.push_back(tid);
    for (auto const &t : kg_->known_topics())
        if (!npc->knowledge.contains(t))
            dialogue_->available_actions.push_back("tell:" + t);
    if (npc->personality == "hostile") {
        dialogue_->available_topics.push_back("__attack__");
        dialogue_->can_threaten = true;
    }
    dialogue_->can_gift = true;
    for (auto const *q : qm_->available_quests())
        if (q->giver == npc->npc_id) {
            dialogue_->available_topics.push_back("__quest__");
            break;
        }
    for (auto const *q : qm_->active_quests())
        if (q->giver == npc->npc_id) {
            dialogue_->available_topics.push_back("__quest_turnin__");
            break;
        }
}

EntityId GameMode::find_nearest_interactable(EntityId player,
                                             Vec2f player_pos)
{
    EntityId nearest = invalid_entity;
    float nearestDist = 80.f;
    for (auto eid : em_.all_entities()) {
        if (eid == player)
            continue;
        if (!em_.get_component<Interactable>(eid))
            continue;
        auto *cs = em_.get_component<CombatStats>(eid);
        if (cs && !cs->alive)
            continue;
        auto *pos = em_.get_component<Position>(eid);
        if (!pos)
            continue;
        float d = std::hypot(pos->world_pos.x - player_pos.x,
                             pos->world_pos.y - player_pos.y);
        if (d < nearestDist) {
            nearestDist = d;
            nearest = eid;
        }
    }
    return nearest;
}

void GameMode::do_dialogue_action(std::string const &action)
{
    if (!dialogue_->active)
        return;
    auto eid = dialogue_->npc_entity;
    auto *npc = em_.get_component<NPCState>(eid);
    if (!npc)
        return;

    auto addHistory = [&](DialogueLine::Speaker s, std::string const &key,
                          std::string const &raw = "", bool use_raw = false) {
        dialogue_->history.push_back({s, key, raw, use_raw, npc->display_name});
    };

    if (action == "__attack__") {
        auto *cs = em_.get_component<CombatStats>(eid);
        if (cs) {
            cs->team = Team::enemy;
            cs->alive = true;
        }
        addHistory(DialogueLine::player, "resp.attack_you");
        addHistory(DialogueLine::npc, npc->personality == "hostile"
                                          ? "resp.attack_hostile"
                                          : "resp.attack_neutral");
        end_dialogue();
        return;
    }
    if (action == "__gift__") {
        relationships_.modify_trust(npc->npc_id, 8);
        dialogue_->npc_trust += 8;
        addHistory(DialogueLine::player, "resp.gift_you");
        addHistory(DialogueLine::npc, npc->personality == "friendly"
                                          ? "resp.gift_friendly"
                                          : "resp.gift_neutral");
        return;
    }
    if (action == "__threaten__") {
        relationships_.modify_fear(npc->npc_id, 15);
        dialogue_->npc_fear += 15;
        addHistory(DialogueLine::player, "resp.threaten_you");
        addHistory(DialogueLine::npc, npc->personality == "hostile"
                                          ? "resp.threaten_hostile"
                                          : "resp.threaten_neutral");
        return;
    }
    if (action == "__quest__") {
        for (auto *q : qm_->available_quests()) {
            if (q->giver != npc->npc_id)
                continue;
            addHistory(DialogueLine::player, "quest.ask_help");
            addHistory(DialogueLine::npc, q->title_key);
            addHistory(DialogueLine::npc, q->desc_key);
            qm_->accept_quest(q->id);
            addHistory(DialogueLine::npc, "quest.accepted");
            return;
        }
        return;
    }
    if (action == "__quest_turnin__") {
        for (auto *q : qm_->active_quests()) {
            if (q->giver != npc->npc_id)
                continue;
            bool done = true;
            for (auto const &obj : q->objectives)
                if (obj.progress < obj.count) {
                    done = false;
                    break;
                }
            addHistory(DialogueLine::player, "quest.turnin");
            if (done) {
                qm_->complete_quest(q->id);
                relationships_.modify_trust(npc->npc_id, q->reward_trust);
                dialogue_->npc_trust += q->reward_trust;
                addHistory(DialogueLine::npc, "quest.reward");
            }
            else {
                addHistory(DialogueLine::npc, "quest.not_done");
            }
            return;
        }
        return;
    }
    if (action.starts_with("tell:")) {
        std::string tid = action.substr(5);
        auto &dn = topic_registry_.display_name(tid);
        addHistory(DialogueLine::player, "dialogue.tell_prefix");
        bool known = npc->knowledge.contains(tid);
        addHistory(DialogueLine::npc,
                   known ? "resp.already_known" : "resp.learned");
        if (!known) {
            relationships_.modify_trust(npc->npc_id, 5);
            dialogue_->npc_trust += 5;
        }
        return;
    }

    // Ask about topic
    auto *rel = relationships_.get_relation(npc->npc_id);
    int trust = rel ? rel->trust : 0;
    auto &dn = topic_registry_.display_name(action);
    auto resp = de_->generate_ask_response(*npc, action,
                                           dn.empty() ? action : dn, trust);
    addHistory(DialogueLine::player, "dialogue.ask_prefix");
    addHistory(DialogueLine::npc, "", resp.text, true);
    qm_->report_talk(npc->npc_id);
    if (resp.trust_delta != 0) {
        relationships_.modify_trust(npc->npc_id, resp.trust_delta);
        dialogue_->npc_trust += resp.trust_delta;
    }
    if (resp.is_truthful && !resp.fact_id.empty() &&
        npc->knowledge.contains(action)) {
        Fact f;
        f.id = action + "_from_" + npc->npc_id;
        f.description = npc->knowledge[action].npc_version;
        f.origins.push_back({Fact::Origin::Source::npc_testimony, npc->npc_id,
                             "", ws_->day(),
                             npc->knowledge[action].confidence});
        kg_->add_or_update_fact(f);
    }
}

void GameMode::end_dialogue()
{
    *dialogue_ = {};
}

EntityId GameMode::load_world(SaveManager::SaveData const &data)
{
    while (!em_.all_entities().empty())
        em_.destroy_entity(em_.all_entities().back());
    npc_entities_.clear();

    auto pid = spawn_player(data.player_pos.x, data.player_pos.y);
    auto *pcs = em_.get_component<CombatStats>(pid);
    if (pcs) {
        pcs->hp = data.player_hp;
        pcs->max_hp = data.player_max_hp;
    }

    ws_->set_day(data.day);
    ws_->set_season(data.season);
    for (auto const &t : data.seen_tiles)
        ws_->reveal_tile(t);

    kg_->mark_topic_known("ugarit_sack");
    kg_->mark_topic_known("sea_peoples");
    kg_->mark_topic_known("byblos_king");
    for (auto const &t : data.known_topics)
        kg_->mark_topic_known(t);

    relationships_ = {};
    for (auto const &[nid, rel] : data.relations)
        relationships_.set_relation(nid, {rel[0], rel[1], rel[2]});

    for (auto const &nd : data.npcs) {
        std::vector<NPCKnowledgeEntry> facts;
        for (auto const &[fid, ver] : nd.knowledge)
            facts.push_back({fid, ver, 70, false, ""});
        spawn_npc(nd.id, nd.name, nd.position.x, nd.position.y,
                  nd.personality, facts);
        EntityId eid = npc_entities_.back();
        auto *ncs = em_.get_component<CombatStats>(eid);
        if (ncs) {
            ncs->hp = nd.hp;
            ncs->max_hp = nd.max_hp;
            ncs->alive = nd.alive;
        }
    }

    return pid;
}

std::vector<SaveManager::NPCData> GameMode::collect_npc_save_data()
{
    std::vector<SaveManager::NPCData> npcData;
    for (auto eid : npc_entities_) {
        auto *np = em_.get_component<Position>(eid);
        auto *ns = em_.get_component<NPCState>(eid);
        auto *ncs = em_.get_component<CombatStats>(eid);
        if (!np || !ns)
            continue;
        SaveManager::NPCData nd;
        nd.id = ns->npc_id;
        nd.name = ns->display_name;
        nd.personality = ns->personality;
        nd.position = np->world_pos;
        nd.hp = ncs ? ncs->hp : 10;
        nd.max_hp = ncs ? ncs->max_hp : 10;
        nd.alive = ncs ? ncs->alive : true;
        for (auto const &[fid, kf] : ns->knowledge)
            nd.knowledge[fid] = kf.npc_version;
        npcData.push_back(std::move(nd));
    }
    return npcData;
}

void GameMode::update(EntityId player, float dt)
{
    (void)dt;
    auto *cs = em_.get_component<CombatStats>(player);
    bool dead = cs && !cs->alive;
    if (dead && dialogue_->active)
        end_dialogue();
}

void GameMode::apply_player_movement(EntityId player, Vec2f new_pos)
{
    auto *pos = em_.get_component<Position>(player);
    if (!pos)
        return;
    Vec2f old = pos->world_pos;
    pos->world_pos = new_pos;
    pos->tile_pos = {static_cast<int>(new_pos.x / 64.f),
                     static_cast<int>(new_pos.y / 64.f)};
    ws_->reveal_radius(pos->tile_pos, 8);

    Vec2f dir = new_pos - old;
    float len = std::hypot(dir.x, dir.y);
    if (len < 0.001f)
        return;
    dir.x /= len;
    dir.y /= len;

    int si = 0;
    for (auto eid : em_.all_entities()) {
        auto *ai = em_.get_component<SoldierAI>(eid);
        if (!ai || ai->follow_target != player)
            continue;
        int const PER_ROW[] = {3, 4, 5};
        int rem = ++si, row = 0, col = 0;
        for (row = 0; row < 3; ++row) {
            if (rem <= PER_ROW[row]) {
                col = rem - 1;
                break;
            }
            rem -= PER_ROW[row];
        }
        if (row >= 3) {
            row = 2;
            col = si - PER_ROW[0] - PER_ROW[1] - 1;
        }
        float s = 36.f;
        ai->formation_offset = {((col - (PER_ROW[row] - 1) / 2.f) * s) * dir.y +
                                    (-(50.f + row * 45.f)) * dir.x,
                                ((col - (PER_ROW[row] - 1) / 2.f) * s) *
                                        -dir.x +
                                    (-(50.f + row * 45.f)) * dir.y};
    }
}

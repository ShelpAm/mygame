#include "core/game-mode.hpp"

#include "dialogue/dialogue-engine.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/defense-structure.hpp"
#include "entities/components/interactable.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/sprite.hpp"
#include "factions/event-simulator.hpp"
#include "factions/faction-network.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "knowledge/rumor-propagator.hpp"
#include "net/server.hpp"
#include "survival/condition-tracker.hpp"
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
GameMode::~GameMode()
{
    spdlog::info("GameMode: destructor called, shutting down server");
    server_.reset();
}

void GameMode::init_world()
{
    events_ =
        std::make_unique<EventSimulator>(factions_, knowledge_, world_state_);
    rumors_ = std::make_unique<RumorPropagator>(knowledge_);
    dialogue_engine_.discover_languages("assets/dialogue");
    quests_.load_from_json("assets/data/quests.json");

    // Topics
    topic_registry_.register_topic("ugarit_sack", "the Sack of Ugarit",
                                   "events");
    topic_registry_.register_topic("sea_peoples", "the Sea Peoples",
                                   "factions");
    topic_registry_.register_topic("byblos_king", "the King of Byblos",
                                   "people");
    topic_registry_.register_topic("copper_trade", "the Copper Trade",
                                   "resources");
    knowledge_.mark_topic_known("ugarit_sack");
    knowledge_.mark_topic_known("sea_peoples");
    knowledge_.mark_topic_known("byblos_king");

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
            factions_.add_faction(std::move(f));
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
            events_->add_event(std::move(ev));
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

    world_state_.reveal_radius({0, 0}, 8);

    // Register flecs systems once (not per-frame!)
    // Movement: apply velocity to Position
    movement_sys_ =
        world_.system<Position, Movement>()
            .kind(flecs::OnUpdate)
            .each([this](flecs::entity e, Position &p, Movement &m) {
                auto dest = p.world_pos + m.velocity * dt_.count();
                if (player_entities_.contains(e.id()))
                    apply_player_movement(e.id(), dest);
                else
                    p.world_pos = dest;
                p.tile_pos = {static_cast<int>(p.world_pos.x / 64.f),
                              static_cast<int>(p.world_pos.y / 64.f)};
                if (m.moving)
                    mark_dirty(e.id());
            });

    // Death marker: entities with hp <= 0 become dead
    death_sys_ = world_.system<CombatStats>()
                     .kind(flecs::OnUpdate)
                     .each([this](flecs::entity e, CombatStats &cs) {
                         if (cs.alive && cs.hp <= 0) {
                             cs.alive = false;
                             mark_dirty(e.id());
                             if (dialogue_->active)
                                 end_dialogue();
                         }
                     });

    server_ = std::make_unique<Server>();
    server_->set_combat_system(&combat_);
    server_->set_game_mode(this);

    combat_.set_dirty_callback([this](EntityId eid) { mark_dirty(eid); });
}

awaitable<void> GameMode::start_host(int port)
{
    co_await server_->listen(port);
}

void GameMode::spawn_npc(std::string const &id, std::string const &name,
                         float x, float y, std::string const &personality,
                         std::vector<NPCKnowledgeEntry> const &known_facts)
{
    auto e = world_.entity();
    auto eid = e.id();

    SDL_FColor col{0.8f, 0.6f, 0.2f, 1.f};
    if (personality == "hostile")
        col = {0.8f, 0.2f, 0.2f, 1.f};
    else if (personality == "guarded")
        col = {0.6f, 0.6f, 0.8f, 1.f};
    else if (personality == "fearful")
        col = {0.8f, 0.5f, 0.8f, 1.f};

    NPCState npc;
    npc.npc_id = id;
    npc.display_name = name;
    npc.personality = personality;
    for (auto const &kf : known_facts)
        npc.knowledge[kf.fact_id] = {kf.fact_id, kf.version, kf.confidence,
                                     kf.witnessed, kf.source};

    e.set<Position>(Position{
        {x, y}, {static_cast<int>(x / 64.f), static_cast<int>(y / 64.f)}, 1.f});
    e.set<Sprite>(Sprite{"", {}, {16, 16}, col, 1.f, true});
    e.set<Interactable>(Interactable{64.f, true});
    e.set<NPCState>(npc);
    e.set<CombatStats>(CombatStats{Team::neutral, 15, 15, 2, 1, 60.f});

    relationships_.set_relation(id, {});
    mark_dirty(eid);
    npc_entities_.push_back(eid);
}

EntityId GameMode::spawn_soldier(EntityId leader, int index,
                                 Vec2f const &facing, Vec2f extra_offset)
{
    float angle_rad = ANGLE_DEG * 3.1415926535f / 180.0f;
    float tan_half = tanf(angle_rad);

    // 1. 根据 index 确定行号 row 及该行内的列号 col
    int row = 0;
    int cum = 0;
    while (true) {
        int row_size = row + 1; // 第 row 行有 row+1 个士兵
        if (index < cum + row_size) {
            int col = index - cum; // 0 ~ row
            // 2. 该行到玩家的距离
            float dist_to_player = BASE_OFFSET + row * ROW_SPACING;
            // 3. 该行的半宽（由角度和距离决定）
            float half_width = dist_to_player * tan_half;

            // 4. 本地坐标（玩家坐标系：+X 右，+Y 前）
            float local_x, local_y;
            if (row_size == 1) {
                local_x = 0.0f;
            }
            else {
                // 将 col 均匀映射到 [-half_width, half_width]
                local_x =
                    -half_width + (col + 0.5f) * (2.0f * half_width / row_size);
            }
            local_y = -dist_to_player; // 玩家后方为负

            // 5. 旋转到世界偏移（基于 facing 方向）
            // facing 是玩家前向单位向量 (dx, dy)
            float ox = local_x * (-facing.y) + local_y * facing.x;
            float oy =
                local_x * facing.x +
                local_y * facing.y; // 注意：原代码 ox/oy 公式略有不同，这里修正
            // 原公式：ox = local_x * facing.y + local_y * facing.x; oy =
            // local_x * -facing.x + local_y * facing.y;
            // 我根据标准旋转验证：向右向量 = (-facing.y, facing.x)，前向向量 =
            // facing 世界偏移 = local_x * right + local_y * forward 因此 ox =
            // local_x * (-facing.y) + local_y * facing.x
            //     oy = local_x * facing.x + local_y * facing.y
            // 如果你希望完全保持原公式的符号习惯，可以取消注释下面的原公式
            // float ox = local_x * facing.y + local_y * facing.x;
            // float oy = local_x * -facing.x + local_y * facing.y;

            // 6. 加上额外偏移
            Vec2f off{ox + extra_offset.x, oy + extra_offset.y};

            auto const *lp = world_.entity(leader).try_get<Position>();
            Vec2f start = lp ? lp->world_pos + off : off;

            auto e = world_.entity();
            auto eid = e.id();

            e.set<Position>(Position{start, {0, 0}, 0.8f});
            e.set<Sprite>(
                Sprite{"", {}, {12, 12}, {0.3f, 0.5f, 0.9f, 1.f}, 0.8f, true});
            e.set<CombatStats>(CombatStats{Team::player, 12, 12, 3, 2, 80.f});
            e.set<SoldierAI>(SoldierAI{leader, off, 32.f, 200.f});
            mark_dirty(eid);
            return eid;
        }
        cum += row_size;
        ++row;
    }
}

EntityId GameMode::spawn_recruit(EntityId leader)
{
    return spawn_soldier(leader, soldier_idx_++, {0, -1}, {32.f, 32.f});
}

void GameMode::spawn_guards(EntityId captain_eid, int count, Team team)
{
    auto const *cp = world_.entity(captain_eid).try_get<Position>();
    Vec2f center = cp ? cp->world_pos : Vec2f{};
    SDL_FColor col = team == Team::enemy ? SDL_FColor{0.8f, 0.3f, 0.1f, 1.f}
                                         : SDL_FColor{0.3f, 0.6f, 0.9f, 1.f};
    for (int g = 0; g < count; ++g) {
        float a = (float)g / count * 6.28318f;
        Vec2f gp{center.x + std::cos(a) * 50.f, center.y + std::sin(a) * 50.f};

        auto e = world_.entity();
        e.set<Position>(Position{gp, {0, 0}, 0.5f});
        e.set<Sprite>(Sprite{"", {}, {10, 10}, col, 0.7f, true});
        e.set<CombatStats>(CombatStats{team, 10, 10, 3, 2, 70.f});
        e.set<SoldierAI>(SoldierAI{
            captain_eid, {gp.x - center.x, gp.y - center.y}, 32.f, 180.f});
        mark_dirty(e.id());
    }
}
void GameMode::handle_interaction(EntityId player)
{
    if (dialogue_->active) {
        end_dialogue();
        return;
    }
    auto const *pp = world_.entity(player).try_get<Position>();
    if (!pp)
        return;
    auto eid = find_nearest_interactable(player, pp->world_pos);
    if (eid == invalid_entity) {
        spdlog::debug("No interactable NPC near player {} at ({}, {})", player,
                      pp->world_pos.x, pp->world_pos.y);
        return;
    }
    auto const *npc = world_.entity(eid).try_get<NPCState>();
    if (!npc)
        return;
    auto *rel = relationships_.get_relation(npc->npc_id);
    int trust = rel ? rel->trust : 0;
    auto resp = dialogue_engine_.generate_greeting(*npc, trust);
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
    for (auto const &t : knowledge_.known_topics())
        if (!npc->knowledge.contains(t))
            dialogue_->available_actions.push_back("tell:" + t);
    if (npc->personality == "hostile") {
        dialogue_->available_topics.push_back("__attack__");
        dialogue_->can_threaten = true;
    }
    dialogue_->can_gift = true;
    for (auto const *q : quests_.available_quests())
        if (q->giver == npc->npc_id) {
            dialogue_->available_topics.push_back("__quest__");
            break;
        }
    for (auto const *q : quests_.active_quests())
        if (q->giver == npc->npc_id) {
            dialogue_->available_topics.push_back("__quest_turnin__");
            break;
        }
}

EntityId GameMode::find_nearest_interactable(EntityId player, Vec2f player_pos)
{
    EntityId nearest = invalid_entity;
    float nearestDist = 80.f;
    world_.query<Position, Interactable>().each(
        [&](flecs::entity e, Position &pos, Interactable &) {
            if (e.id() == player)
                return;
            auto const *cs = e.try_get<CombatStats>();
            if (cs && !cs->alive)
                return;
            float d = std::hypot(pos.world_pos.x - player_pos.x,
                                 pos.world_pos.y - player_pos.y);
            if (d < nearestDist) {
                nearestDist = d;
                nearest = e.id();
            }
        });
    return nearest;
}

void GameMode::do_dialogue_action(std::string const &action)
{
    if (!dialogue_->active)
        return;
    auto eid = dialogue_->npc_entity;
    auto npc_entity = world_.entity(eid);
    auto *npc = npc_entity.try_get_mut<NPCState>();
    if (!npc)
        return;

    auto addHistory = [&](DialogueLine::Speaker s, std::string const &key,
                          std::string const &raw = "", bool use_raw = false) {
        dialogue_->history.push_back({s, key, raw, use_raw, npc->display_name});
    };

    if (action == "__attack__") {
        auto *cs = npc_entity.try_get_mut<CombatStats>();
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
        for (auto *q : quests_.available_quests()) {
            if (q->giver != npc->npc_id)
                continue;
            addHistory(DialogueLine::player, "quest.ask_help");
            addHistory(DialogueLine::npc, q->title_key);
            addHistory(DialogueLine::npc, q->desc_key);
            quests_.accept_quest(q->id);
            addHistory(DialogueLine::npc, "quest.accepted");
            return;
        }
        return;
    }
    if (action == "__quest_turnin__") {
        for (auto *q : quests_.active_quests()) {
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
                quests_.complete_quest(q->id);
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
    auto resp = dialogue_engine_.generate_ask_response(
        *npc, action, dn.empty() ? action : dn, trust);
    addHistory(DialogueLine::player, "dialogue.ask_prefix");
    addHistory(DialogueLine::npc, "", resp.text, true);
    quests_.report_talk(npc->npc_id);
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
                             "", world_state_.day(),
                             npc->knowledge[action].confidence});
        knowledge_.add_or_update_fact(f);
    }
}

void GameMode::end_dialogue()
{
    *dialogue_ = {};
}

EntityId GameMode::load_world(SaveManager::SaveData const &data)
{
    world_.each([](flecs::entity e) { e.destruct(); });
    npc_entities_.clear();

    auto pid = spawn_player({data.player_pos.x, data.player_pos.y});
    auto *pcs = world_.entity(pid).try_get_mut<CombatStats>();
    if (pcs) {
        pcs->hp = data.player_hp;
        pcs->max_hp = data.player_max_hp;
    }

    world_state_.set_day(data.day);
    world_state_.set_season(data.season);
    for (auto const &t : data.seen_tiles)
        world_state_.reveal_tile(t);

    knowledge_.mark_topic_known("ugarit_sack");
    knowledge_.mark_topic_known("sea_peoples");
    knowledge_.mark_topic_known("byblos_king");
    for (auto const &t : data.known_topics)
        knowledge_.mark_topic_known(t);

    relationships_ = {};
    for (auto const &[nid, rel] : data.relations)
        relationships_.set_relation(nid, {rel[0], rel[1], rel[2]});

    for (auto const &nd : data.npcs) {
        std::vector<NPCKnowledgeEntry> facts;
        for (auto const &[fid, ver] : nd.knowledge)
            facts.push_back({fid, ver, 70, false, ""});
        spawn_npc(nd.id, nd.name, nd.position.x, nd.position.y, nd.personality,
                  facts);
        EntityId eid = npc_entities_.back();
        auto *ncs = world_.entity(eid).try_get_mut<CombatStats>();
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
        auto const *np = world_.entity(eid).try_get<Position>();
        auto const *ns = world_.entity(eid).try_get<NPCState>();
        auto const *ncs = world_.entity(eid).try_get<CombatStats>();
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

EntityId GameMode::spawn_player(Vec2f pos)
{
    auto e = world_.entity();
    auto eid = e.id();

    e.set<Position>(Position{pos, {0, 0}, 1.f});
    e.set<CombatStats>(CombatStats{Team::player, 20, 20, 4, 3, 80.f});
    e.set<Movement>(Movement{});
    e.set<SurvivalState>(SurvivalState{});

    e.set<Sprite>(
        Sprite{"player", {}, {16, 16}, {0.3f, 0.8f, 0.3f, 1.f}, 1.f, true});

    mark_dirty(eid);
    player_entities_.insert(eid);
    return eid;
}

void GameMode::update(float dt)
{
    while (server_->messages().try_receive(
        [this](boost::system::error_code, ITransport *t, TransportMessage msg) {
            server_->handle_message(*t, std::move(msg));
        })) {
    }

    dt_ = std::chrono::duration<double>(dt);
    world_state_.update(dt);
    events_->update(world_state_.day());

    // Run survival decay directly on player entities' SurvivalState components
    spdlog::trace("GameMode: survival decay");
    float days_passed = dt / 24.f;
    for (auto pid : player_entities_) {
        auto *surv = world_.entity(pid).try_get_mut<SurvivalState>();
        if (!surv)
            continue;
        surv->food -= SurvivalState::food_decay_per_day * days_passed;
        surv->water -= SurvivalState::water_decay_per_day * days_passed;
        if (surv->food <= 0.f)
            surv->health -= SurvivalState::health_decay_starving * days_passed;
        if (surv->water <= 0.f)
            surv->health -= SurvivalState::health_decay_starving * days_passed;
        surv->energy -= 0.5f * dt;
        surv->food = std::clamp(surv->food, 0.f, 100.f);
        surv->water = std::clamp(surv->water, 0.f, 100.f);
        surv->health = std::clamp(surv->health, 0.f, 100.f);
        surv->energy = std::clamp(surv->energy, 0.f, 100.f);
        mark_dirty(pid);
    }

    // 2. Combat system
    spdlog::trace("GameMode: combat update");
    combat_.update(world_, dt);

    // Broadcast events
    spdlog::trace("GameMode: broadcast combat events");
    for (auto const &ev : combat_.consume_events()) {
        std::vector<uint8_t> p;
        auto push = [&](auto v) {
            auto *pb = (uint8_t *)&v;
            p.insert(p.end(), pb, pb + sizeof(v));
        };
        push(ev.attacker_id);
        push(ev.defender_id);
        push(ev.damage);
        p.push_back(ev.killed ? 1 : 0);
        for (auto &t : server_->transports_)
            ITransport::spawn(t->write({NetPacket::combat_event, p}));
    }

    // 3. Check event spawns
    spdlog::trace("GameMode: event spawns check");
    assert(events_);
    auto cnt = events_->triggered_events().size();
    if (cnt > last_event_count_) {
        last_event_count_ = cnt;
        for (auto pid : player_entities_) {
            auto const *pp = world_.entity(pid).try_get<Position>();
            Vec2f center = pp ? pp->world_pos : Vec2f{};
            auto &latest = events_->triggered_events().back();
            if (latest.type == GameEvent::Type::battle ||
                latest.type == GameEvent::Type::refugee_wave)
                spawn_enemy_wave(2 + rand() % 4, center, 400.f, Team::enemy);
        }
    }

    spdlog::trace("GameMode: world.progress");
    world_.progress();
    spdlog::trace("GameMode: broadcast sync");
    server_->broadcast_sync();
    spdlog::trace("GameMode: update done");
}

void GameMode::apply_player_movement(EntityId player, Vec2f new_pos)
{
    auto *pos = world_.entity(player).try_get_mut<Position>();
    if (!pos)
        return;
    Vec2f old = pos->world_pos;
    pos->world_pos = new_pos;
    pos->tile_pos = {static_cast<int>(new_pos.x / 64.f),
                     static_cast<int>(new_pos.y / 64.f)};
    world_state_.reveal_radius(pos->tile_pos, 8);

    Vec2f dir = new_pos - old;
    float len = std::hypot(dir.x, dir.y);
    if (len < 0.001f)
        return;
    dir.x /= len;
    dir.y /= len;

    int si = 0;

    world_.query<SoldierAI>().each([&](flecs::entity e, SoldierAI &ai) {
        // 楔形阵
        float angle_rad = ANGLE_DEG * 3.1415926535f / 180.0f;
        float tan_half = tanf(angle_rad); // 用于计算每行半宽

        if (ai.follow_target != player)
            return;

        int idx = si++; // si 需要在外部定义并重置

        // 1. 根据序号 idx 确定行号 row 及该行内的列号 col
        int row = 0;
        int cum = 0;
        while (true) {
            int row_size = row + 1; // 第 row 行有 row+1 个士兵
            if (idx < cum + row_size) {
                int col = idx - cum; // 0 ~ row
                // 2. 该行的半宽（由角度和行距决定）
                float dist_to_player =
                    BASE_OFFSET + row * ROW_SPACING; // 该行到玩家的距离
                float half_width = dist_to_player * tan_half; // 楔形半宽

                // 3. 本地坐标（玩家坐标系：+X 右，+Y 前）
                float local_x, local_y;
                if (row_size == 1) {
                    local_x = 0.0f;
                }
                else {
                    // 将 col 均匀映射到 [-half_width, half_width]
                    local_x = -half_width +
                              (col + 0.5f) * (2.0f * half_width / row_size);
                }
                local_y = -dist_to_player; // 玩家后方为负

                // 4. 根据玩家朝向旋转到世界偏移
                // dir 是玩家前向单位向量 (dx, dy)
                float right_x = -dir.y;
                float right_y = dir.x;
                ai.formation_offset.x = local_x * right_x + local_y * dir.x;
                ai.formation_offset.y = local_x * right_y + local_y * dir.y;
                mark_dirty(e.id());
                return;
            }
            cum += row_size;
            ++row;
        }
    });
}

void GameMode::apply_player_input(EntityId entity, Vec2f dir)
{
    spdlog::debug("Applying input for player {}: dir=({:.2f}, {:.2f})", entity,
                  dir.x, dir.y);
    auto &mov = world_.entity(entity).get_mut<Movement>();
    mov.velocity = {dir.x * mov.speed, dir.y * mov.speed};
    mov.moving = (dir.x != 0.f || dir.y != 0.f);
    mark_dirty(entity);
}

void GameMode::spawn_enemy_wave(int count, Vec2f center, float spread,
                                Team team)
{
    std::vector<EntityId> new_ids;
    combat_.spawn_enemy_wave(world_, count, center, spread, team, &new_ids);
    for (auto eid : new_ids)
        mark_dirty(eid);
}

void GameMode::apply_damage(EntityId target, int damage, bool killed)
{
    auto *cs = world_.entity(target).try_get_mut<CombatStats>();
    if (cs) {
        cs->hp -= damage;
        if (killed) {
            cs->alive = false;
            quests_.report_kill("enemy");
            quests_.report_kill("enemy");
        }
        mark_dirty(target);
    }
}

void GameMode::heal_entity(EntityId entity, int amount)
{
    auto e = world_.entity(entity);
    auto *cs = e.try_get_mut<CombatStats>();
    if (cs && cs->alive) {
        cs->hp = std::min(cs->max_hp, cs->hp + amount);
        mark_dirty(entity);
    }
    if (is_player(entity)) {
        auto *surv = e.try_get_mut<SurvivalState>();
        if (surv) {
            surv->health = std::clamp(surv->health + amount, 0.f, 100.f);
            mark_dirty(entity);
        }
    }
}

void GameMode::sync_entity_state(EntityId entity, Vec2f pos, int hp, int max_hp,
                                 bool alive)
{
    auto *p = world_.entity(entity).try_get_mut<Position>();
    auto *c = world_.entity(entity).try_get_mut<CombatStats>();
    if (p)
        p->world_pos = pos;
    if (c) {
        c->hp = hp;
        c->max_hp = max_hp;
        c->alive = alive;
    }
    mark_dirty(entity);
}

bool GameMode::has_dirty_entities() const
{
    return !dirty_entities_.empty();
}

uint8_t GameMode::entity_kind(flecs::entity e) const
{
    if (player_entities_.contains(e.id()))
        return EntityKind::player;
    if (e.has<NPCState>())
        return EntityKind::npc;
    if (e.has<SoldierAI>())
        return EntityKind::soldier;
    if (e.has<DefenseStructure>())
        return EntityKind::structure;
    return EntityKind::enemy;
}

void GameMode::serialize_entity(flecs::entity e,
                                std::vector<uint8_t> &out) const
{
    auto const *ep = e.try_get<Position>();
    auto const *ec = e.try_get<CombatStats>();
    if (!ep || !ec)
        return;

    uint16_t mask = SyncComponent::entity_kind | SyncComponent::position |
                    SyncComponent::combat;
    if (e.has<Movement>())
        mask |= SyncComponent::movement;
    if (e.has<SoldierAI>())
        mask |= SyncComponent::soldier_ai;
    if (e.has<Interactable>())
        mask |= SyncComponent::interact;
    if (e.has<SurvivalState>())
        mask |= SyncComponent::survival;

    write_bytes(out, e.id());
    write_bytes(out, mask);

    // LSB-first: entity_kind (bit 0)
    out.push_back(entity_kind(e));

    // position (bit 1)
    write_float(out, ep->world_pos.x);
    write_float(out, ep->world_pos.y);

    // combat (bit 2) — hp, max_hp, alive, team, attack, defense, attack_range
    write_bytes(out, ec->hp);
    write_bytes(out, ec->max_hp);
    out.push_back(ec->alive ? 1 : 0);
    out.push_back(static_cast<uint8_t>(ec->team));
    write_bytes(out, ec->attack);
    write_bytes(out, ec->defense);
    write_float(out, ec->attack_range);

    // movement (bit 3)
    if (mask & SyncComponent::movement) {
        auto const *m = e.try_get<Movement>();
        write_float(out, m->velocity.x);
        write_float(out, m->velocity.y);
        out.push_back(m->moving ? 1 : 0);
    }

    // soldier_ai (bit 4)
    if (mask & SyncComponent::soldier_ai) {
        auto const *ai = e.try_get<SoldierAI>();
        write_bytes(out, ai->follow_target);
        write_float(out, ai->formation_offset.x);
        write_float(out, ai->formation_offset.y);
        out.push_back(ai->in_combat ? 1 : 0);
    }

    // interact (bit 5)
    if (mask & SyncComponent::interact)
        out.push_back(1);

    // survival (bit 6)
    if (mask & SyncComponent::survival) {
        auto const *s = e.try_get<SurvivalState>();
        write_float(out, s->food);
        write_float(out, s->water);
        write_float(out, s->health);
        write_float(out, s->energy);
    }
}

static void write_world_header(std::vector<uint8_t> &out, WorldState const &ws)
{
    write_bytes(out, ws.day());
    out.push_back(static_cast<uint8_t>(ws.season()));
    write_float(out, ws.time_of_day());
}

std::vector<uint8_t> GameMode::build_dirty_payload()
{
    std::vector<uint8_t> out;
    write_world_header(out, world_state_);
    for (auto eid : dirty_entities_)
        serialize_entity(world_.entity(eid), out);
    return out;
}

std::vector<uint8_t> GameMode::build_full_payload() const
{
    std::vector<uint8_t> out;
    write_world_header(out, world_state_);
    world_.query<Position, CombatStats>().each(
        [&](flecs::entity e, Position &, CombatStats &) {
            serialize_entity(e, out);
        });
    return out;
}

void GameMode::mark_frame_clean()
{
    dirty_entities_.clear();
}

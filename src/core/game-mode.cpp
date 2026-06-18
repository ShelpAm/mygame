#include "core/game-mode.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "entities/components/collider.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/defense-structure.hpp"
#include "entities/components/interactable.hpp"
#include "entities/components/movement.hpp"
#include "entities/components/player.hpp"
#include "entities/components/position.hpp"
#include "entities/components/soldier-ai.hpp"
#include "entities/components/vision.hpp"
#include "factions/event-simulator.hpp"
#include "factions/faction-network.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "knowledge/rumor-propagator.hpp"
#include "net/local-session.hpp"
#include "net/server.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/collision-system.hpp"
#include "systems/combat-system.hpp"
#include "systems/combat-utils.hpp"
#include "systems/formation.hpp"
#include "systems/quest-manager.hpp"
#include "world/map-data.hpp"
#include "world/world-state.hpp"
#include <boost/json.hpp>
#include <cmath>
#include <cstring>
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
    // 开启 REST 服务（默认监听 27750 端口）
    // ecs_measure_system_time(world_.c_ptr(), true);
    // world_.app().enable_rest().enable_stats().run();
    world_.import<flecs::stats>();
    world_.set<flecs::Rest>({});

    rumors_ = std::make_unique<RumorPropagator>(knowledge_);
    dialogue_engine_.discover_languages("assets/dialogue");
    quests_.load_from_json("assets/data/quests.json");

    load_topics();
    load_factions();
    load_events();
    load_npcs();

    init_systems();

    // Cross-phase: PreUpdate finishes before OnUpdate, which finishes before
    // PostUpdate. Within PreUpdate, depends_on ensures:
    //   SurvivalDecay → SoldierAI → CombatResolution

    server_ = std::make_unique<Server>();
    server_->set_game_mode(this);

    combat_.set_dirty_callback([this](EntityId eid) { mark_dirty(eid); });
}

void GameMode::load_topics()
{
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
}

void GameMode::load_factions()
{ // Load factions
    try {
        auto json = boost::json::parse(readFile("assets/data/factions.json"));
        for (auto const &item : json.as_object().at("factions").as_array()) {
            auto const &obj = item.as_object();
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
}

void GameMode::load_events()
{
    events_ =
        std::make_unique<EventSimulator>(factions_, knowledge_, world_state_);
    // Load events
    try {
        auto json = boost::json::parse(readFile("assets/data/events.json"));
        for (auto const &item : json.as_object().at("events").as_array()) {
            auto const &obj = item.as_object();
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
}

void GameMode::load_npcs()
{
    // Load NPCs
    try {
        auto json = boost::json::parse(readFile("assets/data/npcs.json"));
        for (auto const &item : json.as_object().at("npcs").as_array()) {
            auto const &obj = item.as_object();
            std::string id = std::string(obj.at("id").as_string());
            std::string name = std::string(obj.at("name").as_string());
            std::string pers = std::string(obj.at("personality").as_string());
            float x = static_cast<float>(obj.at("x").as_int64());
            float y = static_cast<float>(obj.at("y").as_int64());
            std::vector<NPCKnowledgeEntry> facts;
            if (obj.contains("knowledge"))
                for (auto const &k : obj.at("knowledge").as_array()) {
                    auto const &ko = k.as_object();
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
            auto npc_eid = spawn_npc(id, name, x, y, pers, facts);
            if (obj.contains("captain") && obj.at("captain").as_bool()) {
                int gc = obj.contains("guards")
                             ? static_cast<int>(obj.at("guards").as_int64())
                             : 3;
                Team t = pers == "hostile" ? Team::enemy : Team::neutral;
                spawn_guards(npc_eid, gc, t);
            }
        }
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to load npcs.json: {}", e.what());
        throw;
    }
}

void GameMode::init_systems()
{
    // Register flecs systems once (not per-frame!)

    // -- PreUpdate phase: simulation decisions --

    survival_sys_ = world_.system<SurvivalState>("SurvivalDecay")
                        .kind(flecs::PreUpdate)
                        .each([this](flecs::entity e, SurvivalState &s) {
                            decay_survival(s, dt_.count());
                            mark_dirty(e.id());
                        });

    soldier_ai_sys_ =
        world_.system<SoldierAI, Position, Movement, CombatStats>("SoldierAI")
            .kind(flecs::PreUpdate)
            .each([this](flecs::entity e, SoldierAI &ai, Position &pos,
                         Movement &mov, CombatStats &cs) {
                run_soldier_ai(world_, e, ai, pos, mov, cs,
                               [this](EntityId eid) { mark_dirty(eid); });
            });
    soldier_ai_sys_.depends_on(survival_sys_);

    combat_resolution_sys_ =
        world_.system<>("CombatResolution")
            .kind(flecs::PreUpdate)
            .run([this](flecs::iter &) {
                run_combat_batch(world_, dt_.count(), pending_combat_events_,
                                 projectiles_,
                                 [this](EntityId eid) { mark_dirty(eid); });
            });
    combat_resolution_sys_.depends_on(soldier_ai_sys_);

    // -- OnUpdate phase: physics --

    movement_sys_ =
        world_.system<Position, Movement>("Movement")
            .kind(flecs::OnUpdate)
            .each([this](flecs::entity e, Position &p, Movement &m) {
                constexpr auto eps = 1e-5F; // Epsilon
                if (m.velocity.length() > eps) {
                    p.world_pos += m.velocity * dt_.count();
                    m.facing = m.velocity * (1.F / m.velocity.length());
                    mark_dirty(e.id());
                }

                if (e.has<Player>())
                    formation_update(e.id(), p, m);
            });

    collision_system_ = std::make_unique<CollisionSystem>();
    collision_sys_ =
        world_.system<Position, Collider>("Collision")
            .kind(flecs::OnUpdate)
            .each([this](flecs::entity e, Position &p, Collider &c) {
                Vec2f const original = p.world_pos;
                Vec2f pos = original;

                pos = collision_system_->resolve_tile_collisions(pos, c.radius);

                world_.query<Position, Collider>().each(
                    [&](flecs::entity other, Position const &op,
                        Collider const &oc) {
                        if (other == e)
                            return;
                        float dx = pos.x - op.world_pos.x;
                        float dy = pos.y - op.world_pos.y;
                        float dist = (pos - op.world_pos).length();
                        float min_dist = c.radius + oc.radius;
                        if (dist < min_dist) {
                            constexpr auto eps = 1e-5F; // Epsilon
                            if (dist > eps) {
                                float overlap = min_dist - dist;
                                pos.x += (dx / dist) * overlap;
                                pos.y += (dy / dist) * overlap;
                            }
                            else {
                                pos.x += min_dist;
                            }
                        }
                    });

                if (pos.x != original.x || pos.y != original.y) {
                    p.world_pos = pos;
                    mark_dirty(e.id());
                }
            });
    collision_sys_.depends_on(movement_sys_);

    // -- PostUpdate phase: cleanup --

    death_marker_sys_ =
        world_.system<CombatStats>("Combat")
            .kind(flecs::PostUpdate)
            .each([this](flecs::entity e, CombatStats &cs) {
                if (cs.alive && cs.hp <= 0) {
                    cs.alive = false;
                    mark_dirty(e.id());
                    for (auto &[pid, ds] : player_dialogues_)
                        if (ds.active && ds.npc_entity == e.id())
                            ds = {};
                }
            });
}

void GameMode::start_host(int port)
{
    Session::spawn([](GameMode *self, auto port) -> awaitable<void> {
        co_await self->server_->listen(port);
    }(this, port));
}

void GameMode::stop_host()
{
    server_->stop_listen();
    // Keeps current local client
    // Don't server_->clear_transports();
    for (auto const &s : server_->sessions()) {
        if (typeid(s.get()) == typeid(LocalSession *)) {
            server_->kick(s, "Host stopped the session");
        }
    }
}

EntityId GameMode::spawn_npc(std::string const &id, std::string const &name,
                             float x, float y, std::string const &personality,
                             std::vector<NPCKnowledgeEntry> const &known_facts)
{
    auto e = world_.entity();
    auto eid = e.id();

    NPCState npc;
    npc.npc_id = id;
    npc.display_name = name;
    npc.personality = personality;
    for (auto const &kf : known_facts)
        npc.knowledge[kf.fact_id] = {kf.fact_id, kf.version, kf.confidence,
                                     kf.witnessed, kf.source};

    e.set<Position>(Position{{x, y}});
    e.set<Interactable>(Interactable{64.f, true});
    e.set<NPCState>(npc);
    e.set<CombatStats>(CombatStats{Team::neutral, 15, 15, 2, 1, 60.f});
    e.set<Collider>(Collider{14.f});

    relationships_.set_relation(id, {});
    mark_dirty(eid);
    return eid;
}

EntityId GameMode::spawn_soldier(EntityId leader, int index,
                                 Vec2f const &facing, Vec2f extra_offset,
                                 SoldierRole role)
{
    auto soldier_prefab = world_.prefab()
                              .set<Position>(Position{})

                              .set<CombatStats>(CombatStats{Team::invalid_team,
                                                            12, 12, 3, 2, 80.f})
                              .set<Collider>(Collider{11.f})
                              .set<Movement>(Movement{.speed = 200})
                              .set<SoldierAI>(SoldierAI{.follow_target = leader,
                                                        .formation_offset = {},
                                                        .follow_distance = 32.f,
                                                        .engage_range = 200.f});

    // Count how many soldiers (of the same role) follow this leader
    int role_count = 0;
    world_.query<SoldierAI, CombatStats>().each(
        [&](flecs::entity, SoldierAI &ai, CombatStats &cs) {
            if (cs.alive && ai.follow_target == leader && ai.role == role)
                ++role_count;
        });
    Vec2f off = player_formation(leader, (uint8_t)role)
                    .compute_offset(index, role_count + 1, facing) +
                extra_offset;

    Vec2f start = world_.entity(leader).get<Position>().world_pos;
    auto leader_team = world_.entity(leader).get<CombatStats>().team;

    auto e = world_.entity()
                 .is_a(soldier_prefab)
                 .set<Position>(Position{start})

                 .set<CombatStats>(CombatStats{leader_team, 12, 12, 3, 2, 80.f})
                 .set<SoldierAI>(SoldierAI{leader, off, 32.f, 200.f});
    e.set<CombatStats>(CombatStats{leader_team, 12, 12, 3, 2, 80.f});
    e.set<Collider>(Collider{11.f});
    e.set<Movement>(Movement{.speed = 200});
    e.set<SoldierAI>(SoldierAI{leader, off, 32.f, 200.f});
    mark_dirty(e.id());
    return e.id();
}

EntityId GameMode::spawn_recruit(EntityId leader)
{
    auto facing = world_.entity(leader).get<Movement>().facing;
    return spawn_soldier(leader, soldier_idx_++, facing, {32.f, 32.f});
}

EntityId GameMode::spawn_recruit_ranged(EntityId leader)
{
    auto facing = world_.entity(leader).get<Movement>().facing;
    auto eid = spawn_soldier(leader, soldier_idx_++, facing, {32.f, 32.f},
                             SoldierRole::ranged);
    auto e = world_.entity(eid);
    auto *ai = e.try_get_mut<SoldierAI>();
    if (ai)
        ai->role = SoldierRole::ranged;
    auto *cs = e.try_get_mut<CombatStats>();
    if (cs) {
        cs->attack_range = 200.f;
        cs->attack = 2.5f;
        cs->defense = 1;
    }
    mark_dirty(eid);
    return eid;
}

void GameMode::spawn_guards(EntityId captain_eid, int count, Team team)
{
    auto const *cp = world_.entity(captain_eid).try_get<Position>();
    Vec2f center = cp ? cp->world_pos : Vec2f{};
    for (int g = 0; g < count; ++g) {
        float a = (float)g / count * 6.28318f;
        Vec2f gp{center.x + std::cos(a) * 50.f, center.y + std::sin(a) * 50.f};

        auto e = world_.entity();
        e.set<Position>(Position{gp});

        e.set<CombatStats>(CombatStats{team, 10, 10, 3, 2, 70.f});
        e.set<Collider>(Collider{10.f});
        e.set<Movement>(Movement{});
        e.set<SoldierAI>(SoldierAI{
            captain_eid, {gp.x - center.x, gp.y - center.y}, 32.f, 180.f});
        mark_dirty(e.id());
    }
}

void GameMode::cycle_stance(EntityId leader)
{
    world_.query<SoldierAI, Position>().each(
        [&](flecs::entity e, SoldierAI &ai, Position &pos) {
            if (ai.follow_target != leader)
                return;
            ai.stance = static_cast<SoldierStance>(
                (static_cast<uint8_t>(ai.stance) + 1) % 3);
            if (ai.stance == SoldierStance::guard)
                ai.guard_post = pos.world_pos;
            mark_dirty(e.id());
        });
}

Formation &GameMode::player_formation(EntityId player, uint8_t role)
{
    auto &map = player_formations_[player];
    auto it = map.find(role);
    if (it == map.end())
        map[role] = std::make_unique<WedgeFormation>();
    return *map[role];
}

char const *GameMode::formation_name(EntityId player, uint8_t role)
{
    return player_formation(player, role).name();
}

void GameMode::cycle_formation(EntityId player, uint8_t role_mask)
{
    auto &reg = formation_registry();
    int n = (int)reg.size();
    for (int r = 0; r < 8; ++r) {
        if (!(role_mask & (1 << r)))
            continue;
        auto &fm = player_formations_[player][(uint8_t)r];
        int idx = 0;
        for (int i = 0; i < n; ++i)
            if (fm && std::strcmp(fm->name(), reg[i].first) == 0) {
                idx = (i + 1) % n;
                break;
            }
        fm = reg[idx].second();
        spdlog::info("Player {} role {} formation: {}", player, r, fm->name());
    }

    // Re-layout soldiers immediately
    Vec2f facing{0, -1};
    if (auto *mov = world_.entity(player).try_get<Movement>())
        facing = mov->facing;

    std::unordered_map<uint8_t, int> counts;
    world_.query<SoldierAI, CombatStats>().each(
        [&](flecs::entity, SoldierAI &ai, CombatStats &cs) {
            if (!cs.alive)
                return;
            if (ai.follow_target == player &&
                ai.stance != SoldierStance::guard &&
                (role_mask & (1 << (uint8_t)ai.role)))
                ++counts[(uint8_t)ai.role];
        });
    std::unordered_map<uint8_t, int> indices;
    world_.query<SoldierAI, CombatStats>().each(
        [&](flecs::entity e, SoldierAI &ai, CombatStats &cs) {
            if (!cs.alive)
                return;
            if (ai.follow_target != player)
                return;
            if (ai.stance == SoldierStance::guard)
                return;
            uint8_t r = (uint8_t)ai.role;
            if (!(role_mask & (1 << r)))
                return;
            int idx = indices[r]++;
            ai.formation_offset = player_formation(player, r).compute_offset(
                idx, counts[r], facing);
            mark_dirty(e.id());
        });
}

void GameMode::handle_interaction(EntityId player)
{
    auto &dlg = player_dialogues_[player];
    if (dlg.active) {
        end_dialogue(player);
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
    dlg.active = true;
    dlg.npc_entity = eid;
    dlg.npc_id = npc->npc_id;
    dlg.npc_name = npc->display_name;
    dlg.history.clear();
    dlg.available_topics.clear();
    dlg.available_actions.clear();
    dlg.can_gift = false;
    dlg.can_threaten = false;
    dlg.history.push_back(
        {DialogueLine::npc, "", resp.text, true, npc->display_name});
    dlg.npc_trust = trust;
    for (auto const &[tid, _] : npc->knowledge)
        dlg.available_topics.push_back(tid);
    for (auto const &t : knowledge_.known_topics())
        if (!npc->knowledge.contains(t))
            dlg.available_actions.push_back("tell:" + t);
    if (npc->personality == "hostile") {
        dlg.available_topics.push_back("__attack__");
        dlg.can_threaten = true;
    }
    dlg.can_gift = true;
    for (auto const *q : quests_.available_quests())
        if (q->giver == npc->npc_id) {
            dlg.available_topics.push_back("__quest__");
            break;
        }
    for (auto const *q : quests_.active_quests())
        if (q->giver == npc->npc_id) {
            dlg.available_topics.push_back("__quest_turnin__");
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

void GameMode::do_dialogue_action(EntityId player, std::string const &action)
{
    auto &dlg = player_dialogues_[player];
    if (!dlg.active)
        return;
    if (action == "__end__") {
        end_dialogue(player);
        return;
    }
    auto eid = dlg.npc_entity;
    auto npc_entity = world_.entity(eid);
    auto *npc = npc_entity.try_get_mut<NPCState>();
    if (!npc)
        return;

    auto addHistory = [&](DialogueLine::Speaker s, std::string const &key,
                          std::string const &raw = "", bool use_raw = false) {
        dlg.history.push_back({s, key, raw, use_raw, npc->display_name});
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
        end_dialogue(player);
        return;
    }
    if (action == "__gift__") {
        relationships_.modify_trust(npc->npc_id, 8);
        dlg.npc_trust += 8;
        addHistory(DialogueLine::player, "resp.gift_you");
        addHistory(DialogueLine::npc, npc->personality == "friendly"
                                          ? "resp.gift_friendly"
                                          : "resp.gift_neutral");
        return;
    }
    if (action == "__threaten__") {
        relationships_.modify_fear(npc->npc_id, 15);
        dlg.npc_fear += 15;
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
                dlg.npc_trust += q->reward_trust;
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
            dlg.npc_trust += 5;
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
        dlg.npc_trust += resp.trust_delta;
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

void GameMode::end_dialogue(EntityId player)
{
    player_dialogues_[player] = {};
}

void GameMode::set_navigation(NavigationSystem const *nav)
{
    navigation_ = nav;
    if (collision_system_)
        collision_system_->set_navigation(nav);
}

EntityId GameMode::load_world(SaveManager::SaveData const &data)
{
    throw std::runtime_error("Load world not implemented yet");
    // world_.each([](flecs::entity e) { e.destruct(); });
    // npc_entities_.clear();
    //
    // auto pid = spawn_player({data.player_pos.x, data.player_pos.y});
    // auto *pcs = world_.entity(pid).try_get_mut<CombatStats>();
    // if (pcs) {
    //     pcs->hp = data.player_hp;
    //     pcs->max_hp = data.player_max_hp;
    // }
    //
    // world_state_.set_day(data.day);
    // world_state_.set_season(data.season);
    // for (auto const &t : data.seen_tiles)
    //     world_state_.reveal_tile(t);
    //
    // knowledge_.mark_topic_known("ugarit_sack");
    // knowledge_.mark_topic_known("sea_peoples");
    // knowledge_.mark_topic_known("byblos_king");
    // for (auto const &t : data.known_topics)
    //     knowledge_.mark_topic_known(t);
    //
    // relationships_ = {};
    // for (auto const &[nid, rel] : data.relations)
    //     relationships_.set_relation(nid, {rel[0], rel[1], rel[2]});
    //
    // for (auto const &nd : data.npcs) {
    //     std::vector<NPCKnowledgeEntry> facts;
    //     for (auto const &[fid, ver] : nd.knowledge)
    //         facts.push_back({fid, ver, 70, false, ""});
    //     spawn_npc(nd.id, nd.name, nd.position.x, nd.position.y,
    //     nd.personality,
    //               facts);
    //     EntityId eid = npc_entities_.back();
    //     auto *ncs = world_.entity(eid).try_get_mut<CombatStats>();
    //     if (ncs) {
    //         ncs->hp = nd.hp;
    //         ncs->max_hp = nd.max_hp;
    //         ncs->alive = nd.alive;
    //     }
    // }
    //
    // return pid;
}

std::vector<SaveManager::NPCData> GameMode::collect_npc_save_data()
{
    std::vector<SaveManager::NPCData> npcData;
    for (auto eid : npc_entities()) {
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

EntityId GameMode::spawn_player(Vec2f pos, Team team)
{
    auto e = world_.entity();
    auto eid = e.id();

    e.set<Position>(Position{pos});
    e.set<CombatStats>(CombatStats{team, 200, 200, 4, 3, 80.f});
    e.set<Movement>(Movement{.speed = 200});
    e.set<SurvivalState>(SurvivalState{});
    e.set<Collider>(Collider{14.f});
    e.set<Player>({});
    e.set<Vision>(Vision{});

    mark_dirty(eid);
    return eid;
}

void GameMode::check_event_spawns()
{
    assert(events_);
    auto cnt = events_->triggered_events().size();
    if (cnt <= last_event_count_)
        return;
    last_event_count_ = cnt;

    world_.query<Position>().each([&](flecs::entity e, Position &pp) {
        if (!e.has<Player>())
            return;
        auto &latest = events_->triggered_events().back();
        if (latest.type == GameEvent::Type::battle ||
            latest.type == GameEvent::Type::refugee_wave) {
            spawn_enemy_wave(8 + rand() % 10, pp.world_pos, 400.f, Team::enemy);
        }
    });
}

void GameMode::update(float dt)
{
    while (server_->messages().try_receive([this](boost::system::error_code,
                                                  std::shared_ptr<Session> t,
                                                  TransportMessage msg) {
        server_->handle_message(std::move(t), std::move(msg));
    })) {
    }

    while (server_->player_detachments().try_receive(
        [this](boost::system::error_code, EntityId eid) {
            remove_player(eid);
            server_->broadcast_entity_removed(eid);
        })) {
    }

    dt_ = std::chrono::duration<double>(dt);
    world_state_.update(dt);
    events_->update(world_state_.day());

    // Projectile tick (before combat — projectiles may deal damage this frame)
    spdlog::trace("GameMode: projectiles update");
    update_projectiles(world_, projectiles_, dt, pending_combat_events_);

    // Broadcast combat events (from projectile hits + previous frame)
    spdlog::trace("GameMode: broadcast combat events");
    for (auto const &ev : pending_combat_events_) {
        std::vector<uint8_t> p;
        auto push = [&](auto v) {
            auto *pb = (uint8_t *)&v;
            p.insert(p.end(), pb, pb + sizeof(v));
        };
        push(ev.attacker_id);
        push(ev.defender_id);
        push(ev.damage);
        p.push_back(ev.killed ? 1 : 0);
        for (auto &tg : server_->sessions_)
            Session::spawn(tg.get()->write({NetPacket::combat_event, p}));
    }
    pending_combat_events_.clear();

    // Broadcast new projectile visuals
    for (size_t i = last_projectile_count_; i < projectiles_.size(); ++i) {
        auto &pr = projectiles_[i];
        Vec2f dst = pr.pos + pr.direction * (pr.total_dist - pr.traveled);
        auto p = make_projectile_fired(pr.pos.x, pr.pos.y, dst.x, dst.y);
        for (auto &tg : server_->sessions_)
            Session::spawn(tg.get()->write({NetPacket::projectile_fired, p}));
    }
    last_projectile_count_ = projectiles_.size();

    // Check event spawns
    spdlog::trace("GameMode: event spawns check");
    check_event_spawns();

    spdlog::trace("GameMode: world.progress");
    world_.progress(dt);
    recompute_player_visibility();
    spdlog::trace("GameMode: broadcast sync");
    server_->broadcast_sync();
    spdlog::trace("GameMode: update done");
}

void GameMode::formation_update(EntityId player, Position &p, Movement &m)
{
    // First pass: count soldiers per role (alive only)
    std::unordered_map<uint8_t, int> counts;
    world_.query<SoldierAI, CombatStats>().each(
        [&](flecs::entity, SoldierAI &ai, CombatStats &cs) {
            if (cs.alive && ai.follow_target == player &&
                ai.stance != SoldierStance::guard)
                ++counts[static_cast<uint8_t>(ai.role)];
        });
    // Second pass: assign offsets per role group
    std::unordered_map<uint8_t, int> indices;
    world_.query<SoldierAI, CombatStats>().each(
        [&](flecs::entity e, SoldierAI &ai, CombatStats &cs) {
            if (!cs.alive)
                return;
            if (ai.follow_target != player)
                return;
            if (ai.stance == SoldierStance::guard)
                return;
            auto r = static_cast<uint8_t>(ai.role);
            int idx = indices[r]++;
            ai.formation_offset = player_formation(player, r).compute_offset(
                idx, counts[r], m.facing);
            mark_dirty(e.id());
        });
}

void GameMode::apply_player_input(EntityId entity, Vec2f dir)
{
    spdlog::debug(
        "GameMode: applying input for player {}: dir=({:.2f}, {:.2f})", entity,
        dir.x, dir.y);
    auto &mov = world_.entity(entity).get_mut<Movement>();
    mov.velocity = dir * mov.speed;
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

void GameMode::respawn_player(EntityId pid)
{
    auto e = world_.entity(pid);
    auto *pos = e.try_get_mut<Position>();
    if (pos)
        pos->world_pos = {0, 0};
    auto *cs = e.try_get_mut<CombatStats>();
    if (cs) {
        cs->hp = cs->max_hp;
        cs->alive = true;
    }
    auto *surv = e.try_get_mut<SurvivalState>();
    if (surv)
        *surv = SurvivalState{};
    mark_dirty(pid);
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
    if (e.has<Player>())
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
    if (e.has<Vision>())
        mask |= SyncComponent::vision;

    SyncWriter w(out);
    w.write(e.id());
    w.write(mask);

    // entity_kind (bit 0) — computed, not a component
    w.write(static_cast<uint8_t>(entity_kind(e)));

    // position (bit 1)
    e.get<Position>().write_sync(w);

    // combat (bit 2)
    e.get<CombatStats>().write_sync(w);

    // movement (bit 3)
    if (mask & SyncComponent::movement)
        e.get<Movement>().write_sync(w);

    // soldier_ai (bit 4)
    if (mask & SyncComponent::soldier_ai)
        e.get<SoldierAI>().write_sync(w);

    // interact (bit 5) — presence flag
    if (mask & SyncComponent::interact)
        e.get<Interactable>().write_sync(w);

    // survival (bit 6)
    if (mask & SyncComponent::survival)
        e.get<SurvivalState>().write_sync(w);

    // vision (bit 7)
    if (mask & SyncComponent::vision)
        e.get<Vision>().write_sync(w);
}

static void write_world_header(std::vector<uint8_t> &out, WorldState const &ws)
{
    write_bytes(out, ws.day());
    write_bytes(out, static_cast<std::uint8_t>(ws.season()));
    write_float(out, ws.time_of_day());
}

GameMode::PlayerSyncPayload
GameMode::build_dirty_payload(EntityId player_eid,
                              std::unordered_set<EntityId> const &prev_sent)
{
    PlayerSyncPayload result;
    write_world_header(result.bytes, world_state_);
    auto it = player_visible_tiles_.find(player_eid);
    auto const &visible =
        it != player_visible_tiles_.end() ? it->second : decltype(it->second){};

    auto emit = [&](flecs::entity e) {
        serialize_entity(e, result.bytes);
        result.entity_ids.insert(e.id());
    };

    // 1. Dirty entities on visible tiles
    for (auto eid : dirty_entities_) {
        auto e = world_.entity(eid);
        auto const *pos = e.try_get<Position>();
        if (!pos)
            continue;
        if (eid == player_eid ||
            visible.contains(world_to_tile(pos->world_pos)))
            emit(e);
    }

    // 2. Newly-visible entities (on visible tiles but not sent last frame)
    world_.query<Position, CombatStats>().each(
        [&](flecs::entity e, Position &pos, CombatStats &) {
            if (result.entity_ids.contains(e.id()))
                return;
            if (e.id() == player_eid)
                return;
            if (prev_sent.contains(e.id()))
                return;
            if (visible.contains(world_to_tile(pos.world_pos)))
                emit(e);
        });

    return result;
}

GameMode::PlayerSyncPayload
GameMode::build_full_payload(EntityId player_eid) const
{
    PlayerSyncPayload result;
    write_world_header(result.bytes, world_state_);
    auto it = player_visible_tiles_.find(player_eid);
    auto const &visible =
        it != player_visible_tiles_.end() ? it->second : decltype(it->second){};

    world_.query<Position, CombatStats>().each(
        [&](flecs::entity e, Position &pos, CombatStats &) {
            if (e.id() == player_eid ||
                visible.contains(world_to_tile(pos.world_pos))) {
                serialize_entity(e, result.bytes);
                result.entity_ids.insert(e.id());
            }
        });
    return result;
}

void GameMode::recompute_player_visibility()
{
    player_visible_tiles_.clear();
    world_.query<Player, Position, Movement>().each(
        [this](flecs::entity e, Player &, Position &pos, Movement &mov) {
            EntityId pid = e.id();
            Vec2i center = world_to_tile(pos.world_pos);
            auto const &vis = e.get<Vision>();
            auto visible =
                compute_visible_arc(center, vis.range, mov.facing, vis.arc);
            auto &explored = player_explored_tiles_[pid];
            explored.insert(visible.begin(), visible.end());
            player_visible_tiles_[pid] = std::move(visible);
        });
}

bool GameMode::is_entity_visible_to_player(EntityId player_eid,
                                           Vec2f world_pos) const
{
    auto it = player_visible_tiles_.find(player_eid);
    if (it == player_visible_tiles_.end())
        return false;
    return it->second.contains(world_to_tile(world_pos));
}

void GameMode::mark_frame_clean()
{
    dirty_entities_.clear();
}

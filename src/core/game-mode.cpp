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
#include <fstream>
#include <imgui.h>
#include <random>
#include <spdlog/spdlog.h>
#include <string_view>

static std::string readFile(std::string const &path)
{
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

GameMode::GameMode() = default;
GameMode::~GameMode()
{
    spdlog::info("GameMode: destructor called");
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

    combat_.set_dirty_callback([this](EntityId eid) { mark_dirty(eid); });
}

void GameMode::load_topics()
{
    // Topics
    topic_registry_.register_topic("ugarit_sack", "the Sack of Ugarit", "events");
    topic_registry_.register_topic("sea_peoples", "the Sea Peoples", "factions");
    topic_registry_.register_topic("byblos_king", "the King of Byblos", "people");
    topic_registry_.register_topic("copper_trade", "the Copper Trade", "resources");
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
                    f.relations[std::string(k)] = static_cast<int>(v.as_int64());
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
    events_ = std::make_unique<EventSimulator>(factions_, knowledge_, world_state_);
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
                ev.source_faction_id = std::string(obj.at("source_faction_id").as_string());
            if (obj.contains("target_faction_id"))
                ev.target_faction_id = std::string(obj.at("target_faction_id").as_string());
            if (obj.contains("power_shift"))
                ev.power_shift = static_cast<int>(obj.at("power_shift").as_int64());
            if (obj.contains("wealth_shift"))
                ev.wealth_shift = static_cast<int>(obj.at("wealth_shift").as_int64());
            if (obj.contains("cohesion_shift"))
                ev.cohesion_shift = static_cast<int>(obj.at("cohesion_shift").as_int64());
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
                        e.confidence = static_cast<int>(ko.at("confidence").as_int64());
                    if (ko.contains("witnessed"))
                        e.witnessed = ko.at("witnessed").as_bool();
                    if (ko.contains("source"))
                        e.source = std::string(ko.at("source").as_string());
                    facts.push_back(std::move(e));
                }
            auto npc_eid = spawn_npc(id, name, x, y, pers, facts);
            if (obj.contains("captain") && obj.at("captain").as_bool()) {
                int gc = obj.contains("guards") ? static_cast<int>(obj.at("guards").as_int64()) : 3;
                spawn_guards(npc_eid, gc);
            }
            relayout_formation(npc_eid);
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

    soldier_ai_sys_ = world_.system<SoldierAI, Transform, Movement, CombatStats>("SoldierAI")
                          .kind(flecs::PreUpdate)
                          .each([this](flecs::entity e, SoldierAI &ai, Transform &pos,
                                       Movement &mov, CombatStats &cs) {
                              run_soldier_ai(world_, e, ai, pos, mov, cs, navigation_, dt_.count(),
                                             [this](EntityId eid) { mark_dirty(eid); });
                          });
    soldier_ai_sys_.depends_on(survival_sys_);

    combat_resolution_sys_ =
        world_.system<>("CombatResolution").kind(flecs::PreUpdate).run([this](flecs::iter) {
            run_combat_batch(world_, dt_.count(), pending_combat_events_, projectiles_,
                             [this](EntityId eid) { mark_dirty(eid); });
        });
    combat_resolution_sys_.depends_on(soldier_ai_sys_);

    // -- OnUpdate phase: physics --

    movement_sys_ = world_.system<Transform, Movement>("Movement")
                        .kind(flecs::OnUpdate)
                        .each([this](flecs::entity e, Transform &p, Movement &m) {
                            constexpr auto eps = 1e-5F; // Epsilon
                            if (m.velocity.length() > eps) {
                                p.world_pos += m.velocity * dt_.count();
                                p.facing = m.velocity.normalized();
                                mark_dirty(e.id());

                                // FIXME: Temporarily skip checking
                                // if (e.has<PlayerTag>() || e.has<NPCState>())
                                relayout_formation(e.id());
                            }
                        });

    collision_system_ = std::make_unique<CollisionSystem>();
    entity_query_ = world_.query<Transform, Collider>();
    collision_sys_ = world_.system<Transform, Collider>("Collision")
                         .kind(flecs::OnUpdate)
                         .each([this](flecs::entity e, Transform &p, Collider &c) {
                             Vec2f const original = p.world_pos;
                             Vec2f pos = original;

                             // 1. Tile collision (hard push-out)
                             pos = collision_system_->resolve_tile_collisions(pos, c.radius);

                             // 2. Soft separation — accumulate forces from nearby entities
                             Vec2f separation_force = {0.F, 0.F};
                             bool const is_player = e.has<PlayerTag>();

                             entity_query_.each(
                                 [&](flecs::entity other, Transform const &op, Collider const &oc) {
                                     if (other == e)
                                         return;
                                     float const dx = pos.x - op.world_pos.x;
                                     float const dy = pos.y - op.world_pos.y;
                                     float const dist = std::hypot(dx, dy);
                                     float const min_dist = c.radius + oc.radius;

                                     if (dist < min_dist && dist > 1e-5F) {
                                         float force_factor = (min_dist - dist) / min_dist;
                                         float weight = other.has<PlayerTag>() ? 15.F : 1.F;
                                         separation_force.x += (dx / dist) * force_factor * weight;
                                         separation_force.y += (dy / dist) * force_factor * weight;
                                     }
                                 });

                             // Player is immune to separation push
                             if (is_player)
                                 separation_force = {0.F, 0.F};

                             constexpr float kSeparationPushSpeed = 3.5F;
                             pos.x += separation_force.x * kSeparationPushSpeed;
                             pos.y += separation_force.y * kSeparationPushSpeed;

                             // 3. Tile collision re-check
                             pos = collision_system_->resolve_tile_collisions(pos, c.radius);

                             if (pos.x != original.x || pos.y != original.y) {
                                 p.world_pos = pos;
                                 mark_dirty(e.id());
                             }
                         });
    collision_sys_.depends_on(movement_sys_);

    // -- PostUpdate phase: cleanup --

    death_marker_sys_ = world_.system<CombatStats>("Combat")
                            .kind(flecs::PostUpdate)
                            .each([this](flecs::entity e, CombatStats &cs) {
                                if (cs.alive && cs.hp <= 0) {
                                    cs.alive = false;
                                    // e.remove<Collider>();
                                    mark_dirty(e.id());
                                    for (auto &[pid, ds] : player_dialogues_)
                                        if (ds.active && ds.npc_entity == e.id())
                                            ds = {};
                                }
                            });
}

EntityId GameMode::spawn_npc(std::string const &id, std::string const &name, float x, float y,
                             std::string const &personality,
                             std::vector<NPCKnowledgeEntry> const &known_facts)
{
    auto e = world_.entity();
    auto eid = e.id();

    NPCState npc;
    npc.npc_id = id;
    npc.display_name = name;
    npc.personality = personality;
    for (auto const &kf : known_facts)
        npc.knowledge[kf.fact_id] = {
            .fact_id = kf.fact_id,
            .npc_version = kf.version,
            .confidence = kf.confidence,
            .witnessed = kf.witnessed,
            .source_npc_id = kf.source,
        };

    e.set(Transform{.world_pos = {x, y}, .facing = Vec2f{-x, -y}.normalized()});
    e.set(Interactable{.interact_radius = 64.F, .can_talk = true});
    e.set(npc);
    e.set(CombatStats{
        .team = personality == "hostile" ? Team::enemy : Team::neutral,
        .max_hp = 50,
        .hp = 50,
        .attack = 2,
        .defense = 4,
        .attack_range = 48.F,
    });
    e.set(Collider{14.F});

    relationships_.set_relation(id, {});
    mark_dirty(eid);
    return eid;
}

EntityId GameMode::spawn_soldier(EntityId captain_id, SoldierRole role)
{
    auto defaults = soldier_role_stats(role);
    auto target = world_.entity(captain_id);

    // Count how many soldiers share this new soldier's formation
    // (across all roles), so they are laid out as one group.
    auto &new_fm = formation(captain_id, role, captain_id);
    std::size_t num_soldiers = 0; // Count of soldiers in this formation

    auto mysoldiers =
        world_.query_builder<SoldierAI, CombatStats>().with<BelongsTo>(captain_id).build();
    mysoldiers.each([&](flecs::entity, SoldierAI &ai, CombatStats &cs) {
        if (!cs.alive)
            return;
        auto &fm = formation(captain_id, ai.role, target.id());
        if (std::string_view{fm.name()} == new_fm.name())
            ++num_soldiers;
    });
    auto target_trans = target.get<Transform>();
    auto offsets = new_fm.compute_offsets({
        .count = num_soldiers + 1,
        .target_facing = target_trans.facing,
        .target_position = target_trans.world_pos,
        .time = elapsed_,
    });
    auto pos = world_.entity(captain_id).get<Transform>().world_pos;

    auto e = world_.entity()
                 .add<BelongsTo>(captain_id)
                 .add<Follows>(captain_id)
                 .set(Transform{.world_pos = pos, .facing = target_trans.facing})
                 .set(Movement{.max_speed = defaults.max_speed, .velocity{}})
                 .set(CombatStats{
                     .team = world_.entity(captain_id).get<CombatStats>().team,
                     .max_hp = defaults.max_hp,
                     .hp = defaults.hp,
                     .attack = defaults.attack,
                     .defense = defaults.defense,
                     .attack_range = defaults.attack_range,
                 })
                 .set(Collider{14.F})
                 .set(SoldierAI{
                     .follow_distance = 2.F,
                     .formation_offset = {}, // Leaves empty, compute in `relayout_formation`.
                     .engage_range = defaults.engage_range,
                     .role = role,
                     .stance = defaults.stance,
                 });
    mark_dirty(e.id());
    relayout_formation(captain_id);
    return e.id();
}

EntityId GameMode::spawn_recruit(EntityId leader)
{
    return spawn_soldier(leader, SoldierRole::melee);
}

EntityId GameMode::spawn_recruit_ranged(EntityId leader)
{
    return spawn_soldier(leader, SoldierRole::ranged);
}

void GameMode::spawn_guards(EntityId captain_eid, int count)
{
    for (int g = 0; g < count; ++g) {
        spawn_soldier(captain_eid, SoldierRole::guard);
    }
}

void GameMode::cycle_stance(EntityId leader)
{
    auto q = world_.query_builder<SoldierAI>().with<BelongsTo>(leader);
    q.each([&](flecs::entity e, SoldierAI &ai) {
        ai.stance = static_cast<SoldierStance>((static_cast<uint8_t>(ai.stance) + 1) %
                                               static_cast<uint8_t>(SoldierStance::size_));
        mark_dirty(e.id());
    });
}

Formation &GameMode::formation(EntityId captain_id, SoldierRole role, EntityId target_id)
{
    auto key = FormationKey{.captain_id = captain_id, .soldier_role = role, .target = target_id};
    if (!formations_.contains(key))
        formations_.insert({key, std::make_unique<WedgeFormation>()});
    return *formations_[key];
}

void GameMode::relayout_formation(EntityId captain_id)
{
    struct FormationInfo {
        std::size_t soldier_count;
        std::vector<Vec2f> offsets;
    };

    auto captain = world_.entity(captain_id);

    auto squad_query =
        world_.query_builder<SoldierAI, CombatStats>().with<BelongsTo>(captain).build();

    std::unordered_map<Formation *, FormationInfo> forminfo;

    squad_query.each([&](flecs::entity e, SoldierAI &ai, CombatStats &cs) {
        if (!cs.alive)
            return;

        auto &fm = formation(captain_id, ai.role, e.target<Follows>());
        ++forminfo[&fm].soldier_count;
    });

    // Pre-compute offsets
    for (auto &[form, info] : forminfo) { // NOLINT
        FormationContext ctx{
            .count = info.soldier_count,
            .target_facing = captain.get<Transform>().facing,
            .target_position = captain.get<Transform>().world_pos,
            .time = elapsed_,
        };
        info.offsets = form->compute_offsets(ctx);
    }

    // Second Pass: 同样只遍历自己人，挨个发座位号
    std::unordered_map<Formation *, int> indices;
    squad_query.each([&](flecs::entity e, SoldierAI &ai, CombatStats &cs) {
        if (!cs.alive)
            return;

        auto &form = formation(captain_id, ai.role, e.target<Follows>());
        int idx = indices[&form]++;
        ai.formation_offset = forminfo[&form].offsets.at(idx);
        spdlog::debug("GameMode::relayout_formation: entity {} role {} formation {} idx "
                      "{} offset ({:.2f}, "
                      "{:.2f})",
                      e.id(), static_cast<int>(ai.role), form.name(), idx, ai.formation_offset.x,
                      ai.formation_offset.y);
        mark_dirty(e.id());
    });
}

void GameMode::cycle_formation(EntityId player, uint8_t role_mask)
{
    spdlog::warn("GameMode: cycle formation failed, not implemented. This may "
                 "be removed in the future.");

    // auto &reg = formation_registry();
    // int n = static_cast<int>(reg.size());
    // for (int r = 0; r < 8; ++r) {
    //     if ((role_mask & (1 << r)) == 0)
    //         continue;
    //     auto &fm = captain_formations_[player][static_cast<uint8_t>(r)];
    //     int idx = 0;
    //     for (int i = 0; i < n; ++i)
    //         if (fm && std::strcmp(fm->name(), reg[i].first) == 0) {
    //             idx = (i + 1) % n;
    //             break;
    //         }
    //     fm = reg[idx].second();
    //     spdlog::info("Player {} role {} formation: {}", player, r,
    //     fm->name());
    // }
    //
    // relayout_formation(player);
}

void GameMode::handle_interaction(EntityId player)
{
    auto &dlg = player_dialogues_[player];
    if (dlg.active) {
        end_dialogue(player);
        return;
    }
    auto const *pp = world_.entity(player).try_get<Transform>();
    if (!pp)
        return;
    auto eid = find_nearest_interactable(player, pp->world_pos);
    if (eid == invalid_entity) {
        spdlog::debug("No interactable NPC near player {} at ({}, {})", player, pp->world_pos.x,
                      pp->world_pos.y);
        return;
    }
    auto const *npc = world_.entity(eid).try_get<NPCState>();
    if (!npc)
        return;
    auto const *rel = relationships_.get_relation(npc->npc_id);
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
    dlg.history.push_back({DialogueLine::npc, "", resp.text, true, npc->display_name});
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
    world_.query<Transform, Interactable>().each(
        [&](flecs::entity e, Transform &pos, Interactable &) {
            if (e.id() == player)
                return;
            auto const *cs = e.try_get<CombatStats>();
            if (cs && !cs->alive)
                return;
            float d = std::hypot(pos.world_pos.x - player_pos.x, pos.world_pos.y - player_pos.y);
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
        addHistory(DialogueLine::npc,
                   npc->personality == "hostile" ? "resp.attack_hostile" : "resp.attack_neutral");
        end_dialogue(player);
        return;
    }
    if (action == "__gift__") {
        relationships_.modify_trust(npc->npc_id, 8);
        dlg.npc_trust += 8;
        addHistory(DialogueLine::player, "resp.gift_you");
        addHistory(DialogueLine::npc,
                   npc->personality == "friendly" ? "resp.gift_friendly" : "resp.gift_neutral");
        return;
    }
    if (action == "__threaten__") {
        relationships_.modify_fear(npc->npc_id, 15);
        dlg.npc_fear += 15;
        addHistory(DialogueLine::player, "resp.threaten_you");
        addHistory(DialogueLine::npc, npc->personality == "hostile" ? "resp.threaten_hostile"
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
        addHistory(DialogueLine::npc, known ? "resp.already_known" : "resp.learned");
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
    auto resp =
        dialogue_engine_.generate_ask_response(*npc, action, dn.empty() ? action : dn, trust);
    addHistory(DialogueLine::player, "dialogue.ask_prefix");
    addHistory(DialogueLine::npc, "", resp.text, true);
    quests_.report_talk(npc->npc_id);
    if (resp.trust_delta != 0) {
        relationships_.modify_trust(npc->npc_id, resp.trust_delta);
        dlg.npc_trust += resp.trust_delta;
    }
    if (resp.is_truthful && !resp.fact_id.empty() && npc->knowledge.contains(action)) {
        Fact f;
        f.id = action + "_from_" + npc->npc_id;
        f.description = npc->knowledge[action].npc_version;
        f.origins.push_back({Fact::Origin::Source::npc_testimony, npc->npc_id, "",
                             world_state_.day(), npc->knowledge[action].confidence});
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

EntityId GameMode::spawn_player(Vec2f pos, Team team)
{
    auto e = world_.entity()
                 .add<PlayerTag>()
                 .set(Transform{.world_pos = pos})
                 .set(CombatStats{.team = team,
                                  .max_hp = 200,
                                  .hp = 200,
                                  .attack = 4,
                                  .defense = 3,
                                  .attack_range = 48.F})
                 .set(Movement{.max_speed = 200, .velocity = {}})
                 .set(SurvivalState{})
                 .set(Collider{14.F})
                 .set(Vision{});
    mark_dirty(e.id());
    return e.id();
}

void GameMode::check_event_spawns()
{
    assert(events_);
    auto cnt = events_->triggered_events().size();
    if (cnt <= last_event_count_)
        return;
    last_event_count_ = cnt;

    thread_local std::mt19937 rng{std::random_device{}()};
    world_.query<Transform>().each([&](flecs::entity e, Transform &pp) {
        if (!e.has<PlayerTag>())
            return;
        auto const &latest = events_->triggered_events().back();
        if (latest.type == GameEvent::Type::battle ||
            latest.type == GameEvent::Type::refugee_wave) {
            std::uniform_int_distribution<int> dist(0, 9);
            spawn_enemy_wave(8 + dist(rng), pp.world_pos, 400.F, Team::enemy);
        }
    });
}

void GameMode::update(float dt)
{
    server_->poll_messages(*this);

    dt_ = std::chrono::duration<double>(dt);
    elapsed_ += dt;
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
            auto const *pb = reinterpret_cast<uint8_t const *>(&v);
            p.insert(p.end(), pb, pb + sizeof(v));
        };
        push(ev.attacker_id);
        push(ev.defender_id);
        push(ev.damage);
        p.push_back(ev.killed ? 1 : 0);
        server_->broadcast_to_all(NetPacket::combat_event, std::move(p));
    }
    pending_combat_events_.clear();

    // Broadcast new projectile visuals
    for (size_t i = last_projectile_count_; i < projectiles_.size(); ++i) {
        auto &pr = projectiles_[i];
        Vec2f dst = pr.pos + pr.direction * (pr.total_dist - pr.traveled);
        auto p = make_projectile_fired(pr.pos.x, pr.pos.y, dst.x, dst.y);
        server_->broadcast_to_all(NetPacket::projectile_fired, std::move(p));
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

void GameMode::apply_player_input(EntityId entity, Vec2f dir)
{
    spdlog::debug("GameMode: applying input for player {}: dir=({:.2f}, {:.2f})", entity, dir.x,
                  dir.y);
    auto &mov = world_.entity(entity).get_mut<Movement>();
    mov.velocity = dir * mov.max_speed;
    mark_dirty(entity);
}

void GameMode::spawn_enemy_wave(int count, Vec2f center, float spread, Team team)
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
            surv->health = std::clamp(surv->health + amount, 0.F, 100.F);
            mark_dirty(entity);
        }
    }
}

void GameMode::respawn_player(EntityId pid)
{
    if (!world_.entity(pid).has<PlayerTag>()) {
        throw std::invalid_argument("Attempted to respawn non-player entity {}");
        return;
    }

    auto e = world_.entity(pid);
    e.get_mut<Transform>().world_pos = {0, 0};
    auto &cs = e.get_mut<CombatStats>();
    cs.hp = cs.max_hp;
    cs.alive = true;
    auto &surv = e.get_mut<SurvivalState>();
    surv = SurvivalState{};

    mark_dirty(pid);
}

void GameMode::sync_entity_state(EntityId entity, Vec2f pos, int hp, int max_hp, bool alive)
{
    auto *p = world_.entity(entity).try_get_mut<Transform>();
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
    if (e.has<PlayerTag>())
        return EntityKind::player;
    if (e.has<NPCState>())
        return EntityKind::npc;
    if (e.has<SoldierAI>())
        return EntityKind::soldier;
    if (e.has<DefenseStructure>())
        return EntityKind::structure;
    return EntityKind::enemy;
}

void GameMode::serialize_entity(flecs::entity e, std::vector<uint8_t> &out) const
{
    auto const *ep = e.try_get<Transform>();
    auto const *ec = e.try_get<CombatStats>();
    if (!ep || !ec)
        return;

    uint16_t mask = SyncComponent::entity_kind | SyncComponent::position | SyncComponent::combat;
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
    e.get<Transform>().write_sync(w);

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
GameMode::build_dirty_payload(EntityId player_eid, std::unordered_set<EntityId> const &prev_sent)
{
    PlayerSyncPayload result;
    write_world_header(result.bytes, world_state_);

    // Explored tiles (server-authoritative, sent every frame for now)
    {
        auto eit = player_explored_tiles_.find(player_eid);
        auto count =
            static_cast<uint16_t>(eit != player_explored_tiles_.end() ? eit->second.size() : 0);
        write_bytes(result.bytes, count);
        if (eit != player_explored_tiles_.end())
            for (auto const &t : eit->second) {
                write_bytes(result.bytes, static_cast<int32_t>(t.x));
                write_bytes(result.bytes, static_cast<int32_t>(t.y));
            }
    }

    auto it = player_visible_tiles_.find(player_eid);
    if (it == player_visible_tiles_.end())
        throw std::runtime_error("Player visible tiles not found for player_eid: " +
                                 std::to_string(player_eid));
    auto const &visible = it->second;

    auto emit = [&](flecs::entity e) {
        serialize_entity(e, result.bytes);
        result.entity_ids.insert(e.id());
    };

    // 1. Dirty entities on visible tiles
    for (auto eid : dirty_entities_) {
        auto e = world_.entity(eid);
        auto const *pos = e.try_get<Transform>();
        if (!pos)
            continue;
        // if (visible.contains(world_to_tile(pos->world_pos)))
        emit(e);
    }

    // 2. Newly-visible entities (on visible tiles but not sent last frame)
    world_.query<Transform, CombatStats>().each(
        [&](flecs::entity e, Transform &pos, CombatStats &) {
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

GameMode::PlayerSyncPayload GameMode::build_full_payload(EntityId player_eid) const
{
    PlayerSyncPayload result;
    write_world_header(result.bytes, world_state_);

    // Explored tiles (server-authoritative)
    {
        auto eit = player_explored_tiles_.find(player_eid);
        auto count =
            static_cast<uint16_t>(eit != player_explored_tiles_.end() ? eit->second.size() : 0);
        write_bytes(result.bytes, count);
        if (eit != player_explored_tiles_.end())
            for (auto const &t : eit->second) {
                write_bytes(result.bytes, static_cast<int32_t>(t.x));
                write_bytes(result.bytes, static_cast<int32_t>(t.y));
            }
    }

    auto it = player_visible_tiles_.find(player_eid);
    auto const &visible = it != player_visible_tiles_.end() ? it->second : decltype(it->second){};

    world_.query<Transform, CombatStats>().each(
        [&](flecs::entity e, Transform &pos, CombatStats &) {
            if (e.id() == player_eid || visible.contains(world_to_tile(pos.world_pos))) {
                serialize_entity(e, result.bytes);
                result.entity_ids.insert(e.id());
            }
        });
    return result;
}

void GameMode::recompute_player_visibility()
{
    player_visible_tiles_.clear();
    auto q = world_.query_builder<Transform, Movement>().with<PlayerTag>().build();
    q.each([this](flecs::entity e, Transform &pos, Movement &mov) {
        EntityId pid = e.id();
        Vec2i center = world_to_tile(pos.world_pos);
        auto const &vis = e.get<Vision>();
        auto visible = compute_visible_arc(center, vis.range, pos.facing, vis.arc);
        auto &explored = player_explored_tiles_[pid];
        explored.insert(visible.begin(), visible.end());
        player_visible_tiles_[pid] = std::move(visible);
    });
}

bool GameMode::is_entity_visible_to_player(EntityId player_eid, Vec2f world_pos) const
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

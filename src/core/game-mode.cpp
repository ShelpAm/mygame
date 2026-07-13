#include "core/game-mode.hpp"
#include "components/building-data.hpp"
#include "components/collider.hpp"
#include "components/combat-stats.hpp"
#include "components/defense-structure.hpp"
#include "components/interactable.hpp"
#include "components/movement.hpp"
#include "components/player.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"
#include "components/survival-state.hpp"
#include "components/vision.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "dialogue/relationship-table.hpp"
#include "dialogue/topic-registry.hpp"
#include "factions/event-simulator.hpp"
#include "factions/faction-network.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "knowledge/rumor-propagator.hpp"
#include "net/server.hpp"
#include "systems/collision-system.hpp"
#include "systems/combat-system.hpp"
#include "systems/combat-utils.hpp"
#include "systems/formation.hpp"
#include "systems/quest-manager.hpp"
#include "world/location-store.hpp"
#include "world/map-data.hpp"
#include "world/world-state.hpp"
#include <boost/json.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <spdlog/spdlog.h>
#include <string_view>

static std::string readFile(std::string const &path)
{
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

GameMode::GameMode() : factory_(world_)
{
}

GameMode::~GameMode()
{
    spdlog::info("GameMode: destructor called");
}

void GameMode::init_world()
{
    // 开启 REST 服务（默认监听 27750 端口）
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
    factory_.set_dirty_callback([this](EntityId eid) { mark_dirty(eid); });
    factory_.set_relationship_table(&relationships_);

    factory_.spawn_building_entities(*location_defs_, *map_data_);
    factory_.spawn_town_npcs();
}

void GameMode::load_topics()
{
    // Topics
    topic_registry_.register_topic("ugarit_sack", "the Fall of Thornhaven", "events");
    topic_registry_.register_topic("sea_peoples", "the Ashen Pact", "factions");
    topic_registry_.register_topic("byblos_king", "the Warden of Ironholt", "people");
    topic_registry_.register_topic("copper_trade", "the Relic Trade", "resources");
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
                    e.locale_key = std::string(ko.at("locale_key").as_string());
                    e.version = std::string(ko.at("version").as_string());
                    if (ko.contains("confidence"))
                        e.confidence = static_cast<int>(ko.at("confidence").as_int64());
                    if (ko.contains("witnessed"))
                        e.witnessed = ko.at("witnessed").as_bool();
                    if (ko.contains("source"))
                        e.source = std::string(ko.at("source").as_string());
                    facts.push_back(std::move(e));
                }
            auto npc_eid = factory_.spawn_npc(id, name, x, y, pers, facts);
            // Parse location_id if present
            if (obj.contains("location_id") && npc_eid != invalid_entity) {
                std::string loc_id = std::string(obj.at("location_id").as_string());
                auto *npc = world_.entity(npc_eid).try_get_mut<NPCState>();
                if (npc)
                    npc->location_id = std::move(loc_id);
            }
            if (obj.contains("captain") && obj.at("captain").as_bool()) {
                int gc = static_cast<int>(obj.at("guards").as_int64());
                for (int gi = 0; gi < gc; ++gi)
                    factory_.spawn_soldier(
                        npc_eid, SoldierRole::guard, 0.F,
                        [this](EntityId cid, SoldierRole r, EntityId tid) -> Formation & {
                            return formation(cid, r, tid);
                        });
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

    town_proximity_sys_ =
        world_.system<Transform, PlayerTag>("TownProximity")
            .kind(flecs::PreUpdate)
            .each([this](flecs::entity e, Transform &pos, PlayerTag const &) {
                EntityId pid = e.id();
                std::string closest_town;

                for (auto const &loc : *location_defs_) {
                    float dist = (pos.world_pos - loc.world_pos).length();
                    if (dist < loc.discovery_radius) {
                        closest_town = loc.id;
                        auto *ls = world_state_.location_mutable(loc.id);
                        if (!ls) {
                            WorldState::LocationState new_ls;
                            new_ls.name = loc.id;
                            world_state_.add_location(loc.id, std::move(new_ls));
                        }
                        ls = world_state_.location_mutable(loc.id);
                        if (ls && !ls->player_has_visited) {
                            ls->player_has_visited = true;
                            ls->last_visit_day = world_state_.day();
                            pending_town_discoveries_.push_back({loc.id, loc.display_name});
                        }
                        break;
                    }
                }

                // Track entry/exit
                auto prev_it = current_town_for_player_.find(pid);
                std::string prev =
                    (prev_it != current_town_for_player_.end()) ? prev_it->second : std::string();

                if (closest_town != prev) {
                    if (!closest_town.empty()) {
                        current_town_for_player_[pid] = closest_town;
                    }
                    else if (prev_it != current_town_for_player_.end()) {
                        current_town_for_player_.erase(prev_it);
                    }
                    if (!prev.empty() && closest_town != prev) {
                        pending_town_left_ = true;
                    }
                }
            });

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
    collision_sys_ =
        world_.system<Transform, Collider>("Collision")
            .kind(flecs::OnUpdate)
            .each([this](flecs::entity e, Transform &p, Collider &c) {
                Vec2f const original = p.world_pos;
                Vec2f pos = original;

                // 1. Tile collision (hard push-out)
                pos = collision_system_->resolve_tile_collisions(pos, c);

                // 2. Soft separation — skip structures (they don't move)
                Vec2f separation_force = {0.F, 0.F};
                bool const is_player = e.has<PlayerTag>();
                bool const is_structure = e.has<BuildingData>() || e.has<DefenseStructure>();

                if (!is_structure) {
                    Vec2f this_min = pos + c.min;
                    Vec2f this_max = pos + c.max;
                    entity_query_.each([&](flecs::entity other, Transform const &op,
                                           Collider const &oc) {
                        if (other == e)
                            return;
                        if (other.has<BuildingData>() || other.has<DefenseStructure>())
                            return;

                        Vec2f other_min = op.world_pos + oc.min;
                        Vec2f other_max = op.world_pos + oc.max;

                        // AABB overlap check
                        if (this_min.x >= other_max.x || this_max.x <= other_min.x)
                            return;
                        if (this_min.y >= other_max.y || this_max.y <= other_min.y)
                            return;

                        // Overlap amount on each axis
                        float overlap_x =
                            std::min(this_max.x - other_min.x, other_max.x - this_min.x);
                        float overlap_y =
                            std::min(this_max.y - other_min.y, other_max.y - this_min.y);

                        // Push along the shorter axis
                        float weight = other.has<PlayerTag>() ? 15.F : 1.F;
                        float const combined_sz = (c.max.x - c.min.x) + (oc.max.x - oc.min.x) +
                                                  (c.max.y - c.min.y) + (oc.max.y - oc.min.y);
                        float norm = combined_sz > 0.F ? 4.F / combined_sz : 0.F;

                        if (overlap_x < overlap_y) {
                            float dir =
                                (this_max.x - other_min.x < other_max.x - this_min.x) ? -1.F : 1.F;
                            separation_force.x += dir * overlap_x * norm * weight;
                        }
                        else {
                            float dir =
                                (this_max.y - other_min.y < other_max.y - this_min.y) ? -1.F : 1.F;
                            separation_force.y += dir * overlap_y * norm * weight;
                        }
                    });

                    // Player is immune to separation push
                    if (is_player)
                        separation_force = {0.F, 0.F};

                    constexpr float kSeparationPushSpeed = 3.5F;
                    pos.x += separation_force.x * kSeparationPushSpeed;
                    pos.y += separation_force.y * kSeparationPushSpeed;
                }

                // 3. Tile collision re-check
                pos = collision_system_->resolve_tile_collisions(pos, c);

                if (pos.x != original.x || pos.y != original.y) {
                    p.world_pos = pos;
                    mark_dirty(e.id());
                }
            });
    collision_sys_.depends_on(movement_sys_);

    // -- PostUpdate phase: cleanup --

    death_marker_sys_ = world_.system<CombatStats>("DeathMarker")
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

    player_visibility_sys_ =
        world_.system<Transform, Movement, Vision>("PlayerVisibility")
            .with<PlayerTag>()
            .kind(flecs::PostUpdate)
            .each([this](flecs::entity e, Transform &pos, Movement &, Vision &vis) {
                EntityId pid = e.id();
                Vec2i center = world_to_tile(pos.world_pos);
                auto visible = compute_visible_arc(center, vis.range, pos.facing, vis.arc);
                auto &explored = player_explored_tiles_[pid];
                explored.insert(visible.begin(), visible.end());
                player_visible_tiles_[pid] = std::move(visible);
            });
    player_visibility_sys_.depends_on(death_marker_sys_);
}

EntityId GameMode::spawn_recruit(EntityId leader)
{
    return factory_.spawn_soldier(leader, SoldierRole::melee, 0.F,
                                  [this](EntityId cid, SoldierRole r, EntityId tid) -> Formation & {
                                      return formation(cid, r, tid);
                                  });
}

EntityId GameMode::spawn_recruit_ranged(EntityId leader)
{
    return factory_.spawn_soldier(leader, SoldierRole::ranged, 0.F,
                                  [this](EntityId cid, SoldierRole r, EntityId tid) -> Formation & {
                                      return formation(cid, r, tid);
                                  });
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
        spdlog::trace("GameMode: relayout_formation: entity {} role {} formation {} idx "
                      "{} offset ({:.2f}, "
                      "{:.2f})",
                      e.id(), static_cast<int>(ai.role), form.name(), idx, ai.formation_offset.x,
                      ai.formation_offset.y);
        mark_dirty(e.id());
    });
}

void GameMode::cycle_formation(EntityId /*player*/, uint8_t /*role_mask*/)
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
    assert(pp);
    auto eid = find_nearest_interactable(player, pp->world_pos);

    // If no NPC or entity found by distance, check tiles adjacent to the player
    // for a building (player can't reach the building collider, so proximity
    // search to the building entity center never works).
    if (eid == invalid_entity && map_data_) {
        Vec2i player_tile = world_to_tile(pp->world_pos);
        float best_dist = std::numeric_limits<float>::max();
        auto check_tile = [&](int tx, int ty) {
            if (!map_data_->in_bounds(tx, ty))
                return;
            int bg = map_data_->tile(tx, ty).building_group;
            if (bg == 0)
                return;
            world_.query<BuildingData>().each([&](flecs::entity e, BuildingData &bd) {
                if (bd.group_id != bg)
                    return;
                Vec2f dpos = pp->world_pos - e.get<Transform>().world_pos;
                float dist = dpos.x * dpos.x + dpos.y * dpos.y;
                if (dist < best_dist) {
                    best_dist = dist;
                    eid = e.id();
                }
            });
        };
        // Facing direction tile (most precise)
        Vec2i face_tile = world_to_tile(pp->world_pos + pp->facing * (tile_size * 1.2F));
        check_tile(face_tile.x, face_tile.y);
        // 4 cardinal tiles as fallback
        Vec2i const dirs[] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (auto const &d : dirs)
            check_tile(player_tile.x + d.x, player_tile.y + d.y);
    }

    if (eid == invalid_entity) {
        spdlog::debug("No interactable NPC near player {} at ({}, {})", player, pp->world_pos.x,
                      pp->world_pos.y);
        return;
    }

    // Check for building entity interaction
    auto const *building = world_.entity(eid).try_get<BuildingData>();
    if (building) {
        // Generic buildings (decorative only) — no dialogue
        if (building->type == BuildingData::Type::generic)
            return;

        // Map building type to the corresponding action
        std::string action;
        std::string greeting;
        std::string label;
        switch (building->type) {
        case BuildingData::Type::inn:
            action = "__inn__";
            label = "Innkeeper";
            greeting = "Welcome to the inn. Would you like to rest for the night?";
            break;
        case BuildingData::Type::market:
            action = "__market__";
            label = "Merchant";
            greeting = "Fresh supplies just arrived! Need provisions for the road?";
            break;
        case BuildingData::Type::temple:
            action = "__temple__";
            label = "Priest";
            greeting = "The sacred flame burns bright. Seek purification, weary traveler?";
            break;
        case BuildingData::Type::blacksmith:
            action = "__blacksmith__";
            label = "Blacksmith";
            greeting = "The forge is hot and ready. Want to upgrade your gear?";
            break;
        default:
            return;
        }

        dlg.active = true;
        dlg.npc_entity = eid;
        dlg.npc_id = "__building__";
        dlg.npc_name =
            !building->display_name.empty()
                ? building->display_name
                : (std::string(label) + " at " +
                   (current_town_for_player_.contains(player) ? current_town_for_player_[player]
                                                              : "town"));
        dlg.history.clear();
        dlg.available_topics.clear();
        dlg.available_actions = {std::move(action)};

        DialogueLine line;
        line.speaker = DialogueLine::npc;
        line.use_raw = true;
        line.raw_text = std::move(greeting);
        line.npc_name = dlg.npc_name;
        dlg.history.push_back(std::move(line));
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
    {
        DialogueLine line;
        line.speaker = DialogueLine::npc;
        line.use_template = true;
        line.template_type = resp.template_type;
        line.variant_index = resp.variant_index;
        line.raw_text = resp.text; // server-rendered fallback
        line.npc_name = npc->display_name;
        dlg.history.push_back(std::move(line));
    }
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
        [&](flecs::entity e, Transform &pos, Interactable &inter) {
            if (e.id() == player)
                return;
            auto const *cs = e.try_get<CombatStats>();
            if (cs && !cs->alive)
                return;
            float d = std::hypot(pos.world_pos.x - player_pos.x, pos.world_pos.y - player_pos.y);
            if (d < inter.interact_radius && d < nearestDist) {
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

    // Town services (generic town or specific building)
    if (dlg.npc_id == "__town__" || dlg.npc_id == "__building__") {
        if (action == "__inn__") {
            auto *cs = world_.entity(player).try_get_mut<CombatStats>();
            auto *sv = world_.entity(player).try_get_mut<SurvivalState>();
            if (cs) {
                cs->hp = std::min(cs->hp + 20, cs->max_hp);
                mark_dirty(player);
            }
            if (sv) {
                sv->health = std::min(sv->health + 30.F, 100.F);
                sv->energy = 100.F;
            }
            DialogueLine line;
            line.speaker = DialogueLine::npc;
            line.use_raw = true;
            line.raw_text = "You rest at the inn. HP restored, energy replenished.";
            line.npc_name = dlg.npc_name;
            dlg.history.push_back(std::move(line));
        }
        else if (action == "__market__") {
            auto *sv = world_.entity(player).try_get_mut<SurvivalState>();
            if (sv) {
                sv->food = std::min(sv->food + 40.F, 100.F);
                sv->water = std::min(sv->water + 40.F, 100.F);
            }
            DialogueLine line;
            line.speaker = DialogueLine::npc;
            line.use_raw = true;
            line.raw_text = "You buy supplies at the market. Food and water restocked.";
            line.npc_name = dlg.npc_name;
            dlg.history.push_back(std::move(line));
        }
        else if (action == "__temple__") {
            auto *sv = world_.entity(player).try_get_mut<SurvivalState>();
            if (sv) {
                sv->health = std::min(sv->health + 50.F, 100.F);
                // Cure poison/ailments by resetting to full health
                sv->energy = std::max(sv->energy, 80.F);
            }
            DialogueLine line;
            line.speaker = DialogueLine::npc;
            line.use_raw = true;
            line.raw_text =
                "The temple's sacred light washes over you. Ailments cured, spirit renewed.";
            line.npc_name = dlg.npc_name;
            dlg.history.push_back(std::move(line));
        }
        else if (action == "__blacksmith__") {
            auto *cs = world_.entity(player).try_get_mut<CombatStats>();
            if (cs) {
                cs->attack = std::min(cs->attack + 2, 20);
                cs->defense = std::min(cs->defense + 1, 15);
                mark_dirty(player);
            }
            DialogueLine line;
            line.speaker = DialogueLine::npc;
            line.use_raw = true;
            line.raw_text =
                "The blacksmith hammers away. Your weapon is sharper and armor reinforced.";
            line.npc_name = dlg.npc_name;
            dlg.history.push_back(std::move(line));
        }
        return;
    }

    auto eid = dlg.npc_entity;
    auto npc_entity = world_.entity(eid);
    auto *npc = npc_entity.try_get_mut<NPCState>();
    assert(npc);

    auto addHistory = [&](DialogueLine::Speaker s, std::string const &key,
                          std::string const &raw = "", bool use_raw = false) {
        dlg.history.push_back({s, key, raw, use_raw, npc->display_name, false, {}, {}});
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
        {
            DialogueLine line;
            line.speaker = DialogueLine::player;
            line.use_template = true;
            line.template_type = "tell_about_topic";
            line.slots["topic"] = "topic." + tid;
            dlg.history.push_back(std::move(line));
        }
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
    // Player's ask line (template-based)
    {
        DialogueLine line;
        line.speaker = DialogueLine::player;
        line.use_template = true;
        line.template_type = "ask_about_topic";
        line.slots["topic"] = "topic." + action;
        dlg.history.push_back(std::move(line));
    }
    // NPC's response (template-based)
    {
        DialogueLine line;
        line.speaker = DialogueLine::npc;
        line.use_template = true;
        line.template_type = resp.template_type;
        line.variant_index = resp.variant_index;
        line.slots = std::move(resp.slots);
        line.raw_text = resp.text; // server-rendered fallback
        line.npc_name = npc->display_name;
        dlg.history.push_back(std::move(line));
    }
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
    return factory_.spawn_player(pos, team);
}

void GameMode::respawn_player(EntityId pid)
{
    assert(world_.entity(pid).has<PlayerTag>());

    auto e = world_.entity(pid);
    e.get_mut<Transform>().world_pos = {0, 0};
    auto &cs = e.get_mut<CombatStats>();
    cs.hp = cs.max_hp;
    cs.alive = true;
    auto &surv = e.get_mut<SurvivalState>();
    surv = SurvivalState{};

    mark_dirty(pid);
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
        server_->broadcast_to_all(ServerMsgType::combat_event, std::move(p));
    }
    pending_combat_events_.clear();

    // Broadcast new projectile visuals
    for (size_t i = last_projectile_count_; i < projectiles_.size(); ++i) {
        auto &pr = projectiles_[i];
        Vec2f dst = pr.pos + pr.direction * (pr.total_dist - pr.traveled);
        auto p = make_projectile_fired(pr.pos.x, pr.pos.y, dst.x, dst.y);
        server_->broadcast_to_all(ServerMsgType::projectile_fired, std::move(p));
    }
    last_projectile_count_ = projectiles_.size();

    // Check event spawns
    spdlog::trace("GameMode: event spawns check");
    check_event_spawns();

    spdlog::trace("GameMode: world.progress");
    player_visible_tiles_.clear();
    world_.progress(dt);
    // Broadcast town discovery/leave events queued by the TownProximity system
    for (auto const &[loc_id, locale_key] : pending_town_discoveries_)
        server_->broadcast_to_all(ServerMsgType::town_discovered,
                                  make_town_discovered(loc_id, locale_key));
    pending_town_discoveries_.clear();
    if (pending_town_left_) {
        server_->broadcast_to_all(ServerMsgType::town_left, {});
        pending_town_left_ = false;
    }
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
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> angle_dist(0.f, 2.f * std::numbers::pi_v<float>);
    std::uniform_real_distribution<float> dist_dist(0.f, spread);

    for (int i = 0; i < count; ++i) {
        float ang = angle_dist(rng);
        float dist = dist_dist(rng);
        Vec2f pos{center.x + std::cos(ang) * dist, center.y + std::sin(ang) * dist};
        factory_.spawn_enemy(pos, team);
    }
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

sync_util::SyncState GameMode::sync_state() const
{
    return sync_util::SyncState{
        .world_state = world_state_,
        .player_explored_tiles = player_explored_tiles_,
        .player_visible_tiles = player_visible_tiles_,
        .dirty_entities = dirty_entities_,
        .ecs_world = world_,
    };
}

void GameMode::mark_frame_clean()
{
    dirty_entities_.clear();
}

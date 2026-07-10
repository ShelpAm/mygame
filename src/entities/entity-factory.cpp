#include "entities/entity-factory.hpp"

#include "components/building-data.hpp"
#include "components/collider.hpp"
#include "components/combat-stats.hpp"
#include "components/interactable.hpp"
#include "components/movement.hpp"
#include "components/npc-state.hpp"
#include "components/player.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"
#include "components/survival-state.hpp"
#include "components/vision.hpp"
#include "dialogue/relationship-table.hpp"
#include "systems/formation.hpp"
#include "world/map-data.hpp"
#include "world/terrain-generator.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <random>
#include <spdlog/spdlog.h>
#include <stdexcept>

// ── helpers to read fields from a Generic object ────────────────────────────

namespace {

using Obj = rfl::Generic::Object;

/// Get a sub-object by key from a Generic (must be an Object).
Obj const &obj_of(rfl::Generic const &g, std::string const &key)
{
    return std::get<Obj>(std::get<Obj>(g.get()).at(key).get());
}

Obj const &obj_of(Obj const &o, std::string const &key)
{
    return std::get<Obj>(o.at(key).get());
}

/// Read an int from a Generic object field.
int int_of(rfl::Generic const &g, std::string const &key, int fallback)
{
    auto &obj = std::get<Obj>(g.get());
    auto it = std::find_if(obj.begin(), obj.end(), [&](auto const &p) { return p.first == key; });
    if (it == obj.end()) {
        spdlog::warn("int_of: key '{}' not found, using default {}", key, fallback);
        return fallback;
    }
    auto const *v = std::get_if<int64_t>(&it->second.get());
    if (!v)
        spdlog::warn("int_of: key '{}' is not an integer, using default {}", key, fallback);
    return v ? static_cast<int>(*v) : fallback;
}

float float_of(rfl::Generic const &g, std::string const &key, float fallback)
{
    auto &obj = std::get<Obj>(g.get());
    auto it = std::find_if(obj.begin(), obj.end(), [&](auto const &p) { return p.first == key; });
    if (it == obj.end()) {
        spdlog::warn("float_of: key '{}' not found, using default {}", key, fallback);
        return fallback;
    }
    auto const &v = it->second.get();
    if (auto *d = std::get_if<double>(&v))
        return static_cast<float>(*d);
    if (auto *i = std::get_if<int64_t>(&v))
        return static_cast<float>(*i);
    spdlog::warn("float_of: key '{}' is not a number, using default {}", key, fallback);
    return fallback;
}

float float_or_int_of(rfl::Generic const &g, std::string const &key, float fallback)
{
    auto &obj = std::get<Obj>(g.get());
    for (auto const &[k, v] : obj) {
        if (k != key)
            continue;
        if (auto *d = std::get_if<double>(&v.get()))
            return static_cast<float>(*d);
        if (auto *i = std::get_if<int64_t>(&v.get()))
            return static_cast<float>(*i);
    }
    spdlog::warn("float_or_int_of: key '{}' is not a number, using default {}", key, fallback);
    return fallback;
}

bool has_key(rfl::Generic const &g, std::string const &key)
{
    auto &obj = std::get<Obj>(g.get());
    return std::find_if(obj.begin(), obj.end(), [&](auto const &p) { return p.first == key; }) !=
           obj.end();
}

/// Helper: extract a float from a reflect-cpp variant (double, int64_t, …).
static float try_float(rfl::Generic const &g)
{
    auto const &v = g.get();
    if (auto *d = std::get_if<double>(&v))
        return static_cast<float>(*d);
    if (auto *i = std::get_if<int64_t>(&v))
        return static_cast<float>(*i);
    return 0.f;
}

/// Parse collider from YAML object field.
/// Expected format:
///   collider:
///     min: [x, y]
///     max: [x, y]
/// Returns nullopt if the field is missing or unparseable.
static std::optional<Collider> collider_of(Obj const &obj, std::string const &key)
{
    auto it = std::find_if(obj.begin(), obj.end(), [&](auto const &p) { return p.first == key; });
    if (it == obj.end())
        return std::nullopt;

    // Must be a sub-object
    auto *sub = std::get_if<Obj>(&it->second.get());
    if (!sub)
        return std::nullopt;

    auto min_it =
        std::find_if(sub->begin(), sub->end(), [](auto const &p) { return p.first == "min"; });
    auto max_it =
        std::find_if(sub->begin(), sub->end(), [](auto const &p) { return p.first == "max"; });
    if (min_it == sub->end() || max_it == sub->end())
        return std::nullopt;

    auto *min_arr = std::get_if<std::vector<rfl::Generic>>(&min_it->second.get());
    auto *max_arr = std::get_if<std::vector<rfl::Generic>>(&max_it->second.get());
    if (!min_arr || !max_arr || min_arr->size() < 2 || max_arr->size() < 2)
        return std::nullopt;

    return Collider{
        .min = {try_float((*min_arr)[0]), try_float((*min_arr)[1])},
        .max = {try_float((*max_arr)[0]), try_float((*max_arr)[1])},
    };
}

SoldierStance parse_stance(std::string const &s)
{
    if (s == "passive")
        return SoldierStance::passive;
    if (s == "defensive")
        return SoldierStance::defensive;
    if (s == "offensive")
        return SoldierStance::offensive;
    throw std::runtime_error("parse_stance: unknown stance \"" + s + "\"");
}

} // anonymous namespace

// ── Constructor: load config via reflect-cpp YAML reader ────────────────────

EntityFactory::EntityFactory(flecs::world &world) : world_(world)
{
    auto read_file = [](std::string const &p) {
        std::ifstream f(p);
        return std::string{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    };
    try {
        auto yaml_str = read_file("assets/data/entities.yaml");
        auto result = rfl::yaml::read<rfl::Generic>(yaml_str);
        if (result) {
            entities_cfg_ = std::move(result.value());
            auto &root_obj = std::get<Obj>(entities_cfg_.get());
            spdlog::info("EntityFactory: loaded {} entity types", root_obj.size());
        }
        else {
            spdlog::error("EntityFactory: failed to parse entities.yaml: {}",
                          result.error().what());
        }
    }
    catch (std::exception const &e) {
        spdlog::error("EntityFactory: exception loading entities.yaml: {}", e.what());
    }
}

// ── helpers that apply a sub-object to a flecs entity ──────────────────────

static void apply_combat(flecs::entity e, rfl::Generic const &src, Team team)
{
    if (!has_key(src, "max_hp")) {
        spdlog::error("apply_combat: 'max_hp' is required but missing");
        return;
    }
    CombatStats cs;
    cs.team = team;
    cs.max_hp = int_of(src, "max_hp", 10);
    cs.hp = int_of(src, "hp", 10);
    cs.attack = int_of(src, "attack", 2);
    cs.defense = int_of(src, "defense", 1);
    cs.attack_range = float_of(src, "attack_range", 96.f);
    e.set(cs);
}

static void apply_movement(flecs::entity e, rfl::Generic const &src)
{
    if (!has_key(src, "max_speed")) {
        spdlog::error("apply_movement: 'max_speed' is required but missing");
        return;
    }
    Movement m;
    m.max_speed = float_of(src, "max_speed", 100.f);
    e.set(m);
}

static void apply_vision(flecs::entity e, rfl::Generic const &src)
{
    if (!has_key(src, "range")) {
        spdlog::error("apply_vision: 'range' is required but missing");
        return;
    }
    Vision v;
    v.range = int_of(src, "range", 6);
    v.arc = float_of(src, "arc", 360.f);
    e.set(v);
}

// ── Entity creation ─────────────────────────────────────────────────────────

EntityId EntityFactory::spawn_player(Vec2f pos, Team team)
{
    auto &root_obj = std::get<Obj>(entities_cfg_.get());
    auto &cfg = root_obj.at("player");
    auto &cobj = std::get<Obj>(cfg.get());

    auto e = world_.entity().add<PlayerTag>().set(Transform{.world_pos = pos});

    if (has_key(cobj, "combat"))
        apply_combat(e, obj_of(cobj, "combat"), team);
    if (has_key(cobj, "movement"))
        apply_movement(e, obj_of(cobj, "movement"));
    if (has_key(cobj, "vision"))
        apply_vision(e, obj_of(cobj, "vision"));
    if (auto col = collider_of(cobj, "collider"))
        e.set(*col);
    e.set(SurvivalState{});
    mark_dirty(e.id());
    return e.id();
}

EntityId EntityFactory::spawn_soldier(
    EntityId captain_id, SoldierRole role, float elapsed,
    std::function<Formation &(EntityId, SoldierRole, EntityId)> const &get_formation)
{
    char const *role_key = "soldier_melee";
    switch (role) {
    case SoldierRole::melee:
        role_key = "soldier_melee";
        break;
    case SoldierRole::ranged:
        role_key = "soldier_ranged";
        break;
    case SoldierRole::guard:
        role_key = "soldier_guard";
        break;
    }

    auto &root_s = std::get<Obj>(entities_cfg_.get());
    auto &cfg = std::get<Obj>(root_s.at(role_key).get());

    auto target = world_.entity(captain_id);
    auto &new_fm = get_formation(captain_id, role, captain_id);
    std::size_t num_soldiers = 0;

    auto mysoldiers =
        world_.query_builder<SoldierAI, CombatStats>().with<BelongsTo>(captain_id).build();
    mysoldiers.each([&](flecs::entity, SoldierAI &ai, CombatStats &cs) {
        if (!cs.alive)
            return;
        auto &fm = get_formation(captain_id, ai.role, target.id());
        if (std::string_view{fm.name()} == new_fm.name())
            ++num_soldiers;
    });
    auto target_trans = target.get<Transform>();
    new_fm.compute_offsets({
        .count = num_soldiers + 1,
        .target_facing = target_trans.facing,
        .target_position = target_trans.world_pos,
        .time = elapsed,
    });

    auto pos = world_.entity(captain_id).get<Transform>().world_pos;
    auto e = world_.entity()
                 .add<BelongsTo>(captain_id)
                 .add<Follows>(captain_id)
                 .set(Transform{.world_pos = pos, .facing = target_trans.facing});

    if (has_key(cfg, "movement"))
        apply_movement(e, obj_of(cfg, "movement"));
    if (has_key(cfg, "combat"))
        apply_combat(e, obj_of(cfg, "combat"), world_.entity(captain_id).get<CombatStats>().team);
    if (auto col = collider_of(cfg, "collider"))
        e.set(*col);
    if (has_key(cfg, "ai")) {
        auto &ai_cfg = obj_of(cfg, "ai");
        SoldierAI ai;
        ai.follow_distance = 2.F;
        ai.engage_range = float_of(ai_cfg, "engage_range", 200.f);
        ai.role = role;
        ai.stance = parse_stance([&]() -> std::string {
            for (auto const &[k, v] : ai_cfg)
                if (k == "stance" && std::get_if<std::string>(&v.get()))
                    return *std::get_if<std::string>(&v.get());
            return "defensive";
        }());
        e.set(ai);
    }
    mark_dirty(e.id());
    return e.id();
}

EntityId EntityFactory::spawn_npc(std::string const &id, std::string const &name, float x, float y,
                                  std::string const &personality,
                                  std::vector<NPCKnowledgeEntry> const &known_facts)
{
    bool hostile = personality == "hostile";
    auto &root_n = std::get<Obj>(entities_cfg_.get());
    auto &cfg = std::get<Obj>(root_n.at(hostile ? "npc_hostile" : "npc_friendly").get());

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
            .locale_key = kf.locale_key,
            .confidence = kf.confidence,
            .witnessed = kf.witnessed,
            .source_npc_id = kf.source,
        };

    e.set(Transform{.world_pos = {x, y}, .facing = Vec2f{-x, -y}.normalized()});
    e.set(Interactable{.interact_radius = float_or_int_of(cfg, "interact_radius", 64.f),
                       .can_talk = true});
    e.set(std::move(npc));

    if (has_key(cfg, "combat"))
        apply_combat(e, obj_of(cfg, "combat"), hostile ? Team::enemy : Team::neutral);
    if (auto col = collider_of(cfg, "collider"))
        e.set(*col);

    if (relationships_)
        relationships_->set_relation(id, {});
    mark_dirty(eid);
    return eid;
}

void EntityFactory::spawn_building_entities(std::vector<LocationDefinition> const &loc_defs,
                                            MapData const &map_data)
{
    auto &root_b = std::get<Obj>(entities_cfg_.get());
    auto &bldg_cfg = std::get<Obj>(root_b.at("building").get());

    static constexpr BuildingData::Type special_types[] = {
        BuildingData::Type::inn,
        BuildingData::Type::market,
        BuildingData::Type::temple,
        BuildingData::Type::blacksmith,
    };

    for (auto const &loc : loc_defs) {
        Vec2i center = loc.tile_center;
        int radius = town_build_radius(loc.building_count);

        struct GroupData {
            int count = 0;
            float sum_x = 0.F, sum_y = 0.F;
        };
        std::unordered_map<int, GroupData> groups;

        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx) {
                Vec2i tile{center.x + dx, center.y + dy};
                if (!map_data.in_bounds(tile.x, tile.y))
                    continue;
                int g = map_data.tile(tile.x, tile.y).building_group;
                if (g == 0)
                    continue;
                auto &gd = groups[g];
                ++gd.count;
                gd.sum_x += static_cast<float>(tile.x);
                gd.sum_y += static_cast<float>(tile.y);
            }

        // Assign types: one of each special first, then generic for the rest.
        std::vector<BuildingData::Type> assignments;
        assignments.reserve(groups.size());
        for (size_t gi = 0; gi < groups.size(); ++gi)
            assignments.push_back(gi < std::size(special_types) ? special_types[gi]
                                                                : BuildingData::Type::generic);

        size_t ai = 0;
        for (auto const &[g, gd] : groups) {
            float avg_x = gd.sum_x / static_cast<float>(gd.count);
            float avg_y = gd.sum_y / static_cast<float>(gd.count);
            Vec2f world_pos{(avg_x + 0.5F) * tile_size, (avg_y + 0.5F) * tile_size};

            auto btype = assignments[ai++];

            auto e = world_.entity()
                         .set(Transform{.world_pos = world_pos})
                         .set(BuildingData{btype, loc.id, "", g})
                         .set(Interactable{.interact_radius = 48.F, .can_talk = false});
            if (has_key(bldg_cfg, "combat"))
                apply_combat(e, obj_of(bldg_cfg, "combat"), Team::neutral);
            if (auto col = collider_of(bldg_cfg, "collider"))
                e.set(*col);
            mark_dirty(e.id());
        }
    }
}

void EntityFactory::spawn_town_npcs()
{
    int count = 0;

    struct ServiceBldg {
        Vec2f world_pos;
        std::string town_id;
        std::string role;
        std::string personality;
    };
    std::vector<ServiceBldg> services;

    world_.query<BuildingData, Transform>().each(
        [&](flecs::entity, BuildingData const &bd, Transform const &bt) {
            char const *role{}, *pers{};
            switch (bd.type) {
            case BuildingData::Type::inn:
                role = "Innkeeper";
                pers = "friendly";
                break;
            case BuildingData::Type::market:
                role = "Merchant";
                pers = "friendly";
                break;
            case BuildingData::Type::temple:
                role = "Priest";
                pers = "friendly";
                break;
            case BuildingData::Type::blacksmith:
                role = "Blacksmith";
                pers = "friendly";
                break;
            default:
                return;
            }
            services.push_back({bt.world_pos, bd.town_id, role, pers});
        });

    if (services.empty())
        return;

    std::seed_seq seed{42};
    std::mt19937 rng(seed);
    std::shuffle(services.begin(), services.end(), rng);

    size_t to_spawn = std::max<size_t>(1, services.size() / 3);
    std::uniform_real_distribution<float> angle_dist(0.F, 2.F * 3.14159F);
    std::uniform_real_distribution<float> radius_dist(30.F, 80.F);

    for (size_t i = 0; i < to_spawn && i < services.size(); ++i) {
        auto const &s = services[i];
        float a = angle_dist(rng), r = radius_dist(rng);
        Vec2f npc_pos = s.world_pos + Vec2f{std::cos(a) * r, std::sin(a) * r};
        std::string npc_id = s.town_id + "_" + s.role;

        auto npc_eid = spawn_npc(npc_id, s.role, npc_pos.x, npc_pos.y, s.personality, {});
        if (npc_eid != invalid_entity) {
            if (auto *ns = world_.entity(npc_eid).try_get_mut<NPCState>())
                ns->location_id = s.town_id;
            ++count;
        }
    }
    spdlog::info("spawn_town_npcs: spawned {} resident NPCs (from {} service buildings)", count,
                 services.size());
}

void EntityFactory::spawn(std::string const &kind, Vec2f pos)
{
    auto &root_obj = std::get<Obj>(entities_cfg_.get());
    auto it = std::find_if(root_obj.begin(), root_obj.end(),
                           [&](auto const &p) { return p.first == kind; });
    if (it == root_obj.end()) {
        spdlog::warn("spawn: unknown entity kind \"{}\"", kind);
        return;
    }
    auto &cfg = std::get<Obj>(it->second.get());

    auto e = world_.entity().set(Transform{.world_pos = pos});

    if (has_key(cfg, "combat"))
        apply_combat(e, obj_of(cfg, "combat"), Team::neutral);
    if (has_key(cfg, "movement"))
        apply_movement(e, obj_of(cfg, "movement"));
    if (has_key(cfg, "vision"))
        apply_vision(e, obj_of(cfg, "vision"));
    if (auto col = collider_of(cfg, "collider"))
        e.set(*col);
    mark_dirty(e.id());
}

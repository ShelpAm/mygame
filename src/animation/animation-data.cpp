#include "animation/animation-data.hpp"
#include "core/resource-manager.hpp"
#include "net/net-packet.hpp" // EntityKind, kind_key
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <rfl.hpp>
#include <rfl/yaml.hpp>
#include <spdlog/spdlog.h>

// ── Internal helpers ─────────────────────────────────────────────────────

static void add_frames(AnimationClip &clip, std::string const &base, int count, float dur)
{
    for (int i = 0; i < count; ++i) {
        clip.frame_textures.push_back(base + "_" + std::to_string(i));
        clip.frame_durations.push_back(dur);
    }
}

static void register_group(ResourceManager &resources, std::string const &prefix,
                           std::vector<AnimationClip> const &clips)
{
    for (auto const &c : clips)
        resources.register_clip(prefix + "_" + c.name, c);
}

// ── YAML loader ───────────────────────────────────────────────────────────

void register_default_clips(ResourceManager &resources)
{
    using Generic = rfl::Generic;
    using Obj = Generic::Object;

    auto path = std::filesystem::path("assets/data/textures.yaml");
    if (!std::filesystem::exists(path)) {
        spdlog::error("register_default_clips: textures.yaml not found");
        return;
    }

    auto content = [](std::string const &p) {
        std::ifstream f(p);
        return std::string{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }(path.string());

    if (content.empty()) {
        spdlog::error("register_default_clips: textures.yaml is empty or not found");
        return;
    }

    auto result = rfl::yaml::read<Generic>(content);
    if (!result) {
        spdlog::error("register_default_clips: failed to parse textures.yaml: {}",
                      result.error().what());
        return;
    }

    auto const &root = result.value();
    auto const &root_obj = std::get<Obj>(root.get());

    auto get_str = [](Obj const &o, std::string const &key) -> std::optional<std::string> {
        for (auto const &[k, v] : o)
            if (k == key)
                if (auto *s = std::get_if<std::string>(&v.get()))
                    return *s;
        return std::nullopt;
    };
    auto get_int = [](Obj const &o, std::string const &key) -> std::optional<int> {
        for (auto const &[k, v] : o)
            if (k == key)
                if (auto *i = std::get_if<int64_t>(&v.get()))
                    return static_cast<int>(*i);
        return std::nullopt;
    };
    auto get_dur = [](Obj const &o, std::string const &key) -> std::optional<float> {
        for (auto const &[k, v] : o)
            if (k == key) {
                if (auto *d = std::get_if<double>(&v.get()))
                    return static_cast<float>(*d);
                if (auto *i = std::get_if<int64_t>(&v.get()))
                    return static_cast<float>(*i);
            }
        return std::nullopt;
    };

    // ── groups ──
    for (auto const &[gk, gv] : root_obj) {
        if (gk != "groups")
            continue;
        for (auto const &[grp_name, grp_val] : std::get<Obj>(gv.get())) {
            auto const &grp = std::get<Obj>(grp_val.get());

            std::vector<AnimationClip> clips;
            for (auto const &[ak, av] : grp) {
                if (ak != "anims")
                    continue;
                for (auto const &a_val : std::get<std::vector<Generic>>(av.get())) {
                    auto const &a = std::get<Obj>(a_val.get());
                    auto name = get_str(a, "name");
                    auto prefix = get_str(a, "prefix");
                    auto frames = get_int(a, "frames");
                    auto dur = get_dur(a, "dur");
                    if (!name || !prefix || !frames || !dur)
                        continue;
                    clips.emplace_back();
                    auto &clip = clips.back();
                    clip.name = *name;
                    add_frames(clip, *prefix, *frames, *dur);
                }
            }
            register_group(resources, grp_name, clips);
        }
    }

    // 1-frame static clip for structures and other non-animated entities
    resources.register_clip("structure_idle", {"idle", {"tile_0"}, {0.f}, false});
}

// ── Runtime animation logic ─────────────────────────────────────────────

static char const *kind_key(uint8_t entity_kind, uint8_t team, uint8_t role)
{
    using namespace EntityKind;
    switch (entity_kind) {
    case player:
        return "player";
    case soldier:
        return (role == 1) ? "archer" : (team >= static_cast<uint8_t>(100)) ? "goblin" : "knight";
    case enemy:
        return "goblin";
    case npc:
        return "villager";
    case structure:
        return "structure";
    default:
        return "";
    }
}

std::string determine_clip_name(float hurt_timer, float attack_timer,
                                 Vec2f velocity, bool alive)
{
    if (!alive)
        return "die";
    if (hurt_timer > 0.f)
        return "hurt";
    if (attack_timer > 0.f)
        return "attack";
    if (std::abs(velocity.x) > 10.f || std::abs(velocity.y) > 10.f)
        return "run";
    return "idle";
}

AnimationClip const *get_clip(std::string const &clip_name,
                               uint8_t entity_kind, uint8_t team, uint8_t role,
                               ResourceManager const &resources)
{
    char const *key_prefix = kind_key(entity_kind, team, role);
    if (!key_prefix)
        return nullptr;

    std::string key = std::string(key_prefix) + "_" + clip_name;
    return resources.clip(key);
}

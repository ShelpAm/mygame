#include "animation/animation-data.hpp"
#include "core/resource-manager.hpp"
#include "components/building-data.hpp"
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
        clip.frame_sprites.push_back(base + "_" + std::to_string(i));
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

    auto read_whole = [](std::string const &p) {
        std::ifstream f(p);
        return std::string{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    };

    // ── Read multi-frame group clips from clips.yaml ──────────────────────
    auto clip_path = std::filesystem::path("assets/data/clips.yaml");
    if (!std::filesystem::exists(clip_path)) {
        spdlog::error("register_default_clips: clips.yaml not found");
        return;
    }

    auto content = read_whole(clip_path.string());
    if (content.empty()) {
        spdlog::error("register_default_clips: clips.yaml is empty");
        return;
    }

    auto result = rfl::yaml::read<Generic>(content);
    if (!result) {
        spdlog::error("register_default_clips: failed to parse clips.yaml: {}",
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
    auto get_float = [](Obj const &o, std::string const &key) -> std::optional<float> {
        for (auto const &[k, v] : o)
            if (k == key) {
                if (auto *d = std::get_if<double>(&v.get()))
                    return static_cast<float>(*d);
                if (auto *i = std::get_if<int64_t>(&v.get()))
                    return static_cast<float>(*i);
            }
        return std::nullopt;
    };

    // ── groups: multi-frame clips registered as "{group}_{clip_name}" ─────
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
                    auto dur = get_float(a, "dur");
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

    // ── singles: explicit 1-frame clips registered by their key ──────────
    for (auto const &[sk, sv] : root_obj) {
        if (sk != "singles")
            continue;
        for (auto const &[key, val] : std::get<Obj>(sv.get())) {
            auto const &s = std::get<Obj>(val.get());
            auto name = get_str(s, "name").value_or("idle");

            // Extract sprites array
            auto sit = std::find_if(s.begin(), s.end(),
                [](auto const &p) { return p.first == "sprites"; });
            if (sit == s.end()) continue;
            auto const &spr_arr = std::get<std::vector<Generic>>(sit->second.get());

            // Extract durations array
            auto dit = std::find_if(s.begin(), s.end(),
                [](auto const &p) { return p.first == "durations"; });
            if (dit == s.end()) continue;
            auto const &dur_arr = std::get<std::vector<Generic>>(dit->second.get());

            std::vector<std::string> frame_spr;
            std::vector<float> frame_dur;
            for (auto const &g : spr_arr)
                frame_spr.push_back(std::get<std::string>(g.get()));
            for (auto const &g : dur_arr) {
                if (auto *d = std::get_if<double>(&g.get()))
                    frame_dur.push_back(static_cast<float>(*d));
                else if (auto *i = std::get_if<int64_t>(&g.get()))
                    frame_dur.push_back(static_cast<float>(*i));
            }
            resources.register_clip(key, {name, std::move(frame_spr), std::move(frame_dur), false});
        }
    }
}

// ── Runtime animation logic ─────────────────────────────────────────────

static char const *kind_key(uint8_t entity_kind, uint8_t team, uint8_t role,
                             uint8_t subtype = 0)
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
    case building: {
        using BDT = BuildingData::Type;
        switch (static_cast<BDT>(subtype)) {
        case BDT::inn:        return "inn";
        case BDT::market:     return "market";
        case BDT::temple:     return "temple";
        case BDT::blacksmith: return "blacksmith";
        default:              return "structure";
        }
    }
    default:
        return "";
    }
}

std::string determine_clip_name(float hurt_timer, float attack_timer, Vec2f velocity, bool alive)
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

AnimationClip const *get_clip(std::string const &clip_name, uint8_t entity_kind, uint8_t team,
                              uint8_t role, ResourceManager const &resources, uint8_t subtype)
{
    char const *key_prefix = kind_key(entity_kind, team, role, subtype);
    if (!key_prefix)
        return nullptr;

    std::string key = std::string(key_prefix) + "_" + clip_name;
    return resources.clip(key);
}

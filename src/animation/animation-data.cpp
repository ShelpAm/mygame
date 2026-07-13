#include "animation/animation-data.hpp"
#include "core/resource-manager.hpp"
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

    // ── groups: {group_name} → anims[] ──────────────────────────
    //
    // 一个 clip 有两种写法:
    //
    //   1. 按 prefix+frames 自动生成 frame 名:
    //        - name: idle
    //          prefix: knight_idle      → "knight_idle_0", "knight_idle_1", …
    //          frames: 8
    //          dur: 0.12
    //
    //   2. 显式列出 sprites（单帧或非连续 sprite 名）:
    //        - name: idle
    //          sprites: [house_0]       → 直接使用 "house_0"
    //          durations: [0.0]
    //
    // 最终注册的 clip key = "{group_name}_{name}"（例如 "structure_idle"）。
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
                    if (!name)
                        continue;

                    clips.emplace_back();
                    auto &clip = clips.back();
                    clip.name = *name;

                    // Two modes: explicit sprites list, or prefix+frames+dur
                    auto sit = std::find_if(a.begin(), a.end(),
                                            [](auto const &p) { return p.first == "sprites"; });
                    if (sit != a.end()) {
                        // ── Explicit sprite list ──
                        auto const &spr_arr = std::get<std::vector<Generic>>(sit->second.get());
                        for (auto const &g : spr_arr)
                            clip.frame_sprites.push_back(std::get<std::string>(g.get()));
                        auto dit = std::find_if(a.begin(), a.end(), [](auto const &p) {
                            return p.first == "durations";
                        });
                        if (dit != a.end())
                            for (auto const &g :
                                 std::get<std::vector<Generic>>(dit->second.get())) {
                                if (auto *d = std::get_if<double>(&g.get()))
                                    clip.frame_durations.push_back(static_cast<float>(*d));
                                else if (auto *i = std::get_if<int64_t>(&g.get()))
                                    clip.frame_durations.push_back(static_cast<float>(*i));
                            }

                        if (clip.frame_durations.size() != clip.frame_sprites.size()) {
                            throw std::runtime_error(
                                std::format("register_default_clips: clip '{}' has mismatched "
                                            "sprites and durations",
                                            clip.name));
                        }
                    }
                    else {
                        // ── Auto-generate from prefix + frames + dur ──
                        auto prefix = get_str(a, "prefix");
                        auto frames = get_int(a, "frames");
                        auto dur = get_float(a, "dur");
                        if (!prefix || !frames || !dur)
                            continue;
                        add_frames(clip, *prefix, *frames, *dur);
                    }
                }
            }
            register_group(resources, grp_name, clips);
        }
    }
}

// ── Runtime animation logic ─────────────────────────────────────────────

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

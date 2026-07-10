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

// ── reflect-cpp structs for textures.yaml ────────────────────────────────────

struct AnimEntry {
    std::string name;
    std::string prefix;
    int frames;
    float dur;
    std::optional<std::string> file;
};

struct Group {
    std::string base;
    std::optional<int> sheet;
    std::vector<AnimEntry> anims;
};

struct TilesConfig {
    std::string base;
    std::string ext;
    int count;
    std::string name_prefix;
};

struct AnimConfig {
    std::map<std::string, Group> groups;
    struct Singles {
        TilesConfig tiles;
        std::string entity_dead;
    } singles;
};

// ── YAML loader ───────────────────────────────────────────────────────────

void register_default_clips(ResourceManager &resources)
{
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

    auto result = rfl::yaml::read<AnimConfig>(content);
    if (!result) {
        spdlog::error("register_default_clips: failed to parse textures.yaml: {}",
                      result.error().what());
        return;
    }
    auto const &config = result.value();

    for (auto const &[grp_name, group] : config.groups) {
        std::vector<AnimationClip> clips;
        for (auto const &anim : group.anims) {
            clips.emplace_back();
            auto &clip = clips.back();
            clip.name = anim.name;
            add_frames(clip, anim.prefix, anim.frames, anim.dur);
        }
        register_group(resources, grp_name, clips);
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

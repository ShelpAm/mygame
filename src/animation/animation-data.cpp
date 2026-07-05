#include "animation/animation-data.hpp"
#include "core/resource-manager.hpp"
#include "net/net-packet.hpp" // EntityKind
#include <algorithm>
#include <cmath>

// ── Internal helpers ─────────────────────────────────────────────────────

static void add_frames(AnimationClip &clip, std::string const &base, int count, float dur)
{
    for (int i = 0; i < count; ++i) {
        clip.frame_names.push_back(base + "_" + std::to_string(i));
        clip.frame_durations.push_back(dur);
    }
}

static void register_group(ResourceManager &resources, std::string const &prefix,
                           std::vector<AnimationClip> const &clips)
{
    for (auto const &c : clips)
        resources.register_clip(prefix + "_" + c.name, c);
}

// ── Clip factories ───────────────────────────────────────────────────────

static std::vector<AnimationClip> knight_clips()
{
    AnimationClip idle{"idle", {}, {}, true, false};
    add_frames(idle, "knight_idle", 8, 0.12f);
    AnimationClip run{"run", {}, {}, true, false};
    add_frames(run, "knight_run", 8, 0.08f);
    AnimationClip attack{"attack", {}, {}, false, false};
    add_frames(attack, "knight_attack", 8, 0.07f);
    AnimationClip hurt{"hurt", {}, {}, false, false};
    add_frames(hurt, "knight_hurt", 8, 0.07f);
    AnimationClip die{"die", {}, {}, false, false};
    add_frames(die, "knight_die", 8, 0.12f);
    return {idle, run, attack, hurt, die};
}

static std::vector<AnimationClip> archer_clips()
{
    AnimationClip idle{"idle", {}, {}, true, false};
    add_frames(idle, "archer_idle", 4, 0.15f);
    AnimationClip run{"run", {}, {}, true, false};
    add_frames(run, "archer_walk", 5, 0.10f);
    AnimationClip attack{"attack", {}, {}, false, false};
    add_frames(attack, "archer_attack", 6, 0.07f);
    AnimationClip hurt{"hurt", {}, {}, false, false};
    add_frames(hurt, "archer_hit", 5, 0.07f);
    AnimationClip die{"die", {}, {}, false, false};
    add_frames(die, "archer_die", 19, 0.10f);
    return {idle, run, attack, hurt, die};
}

static std::vector<AnimationClip> goblin_clips()
{
    AnimationClip idle{"idle", {}, {}, true, false};
    add_frames(idle, "goblin_idle", 4, 0.15f);
    AnimationClip run{"run", {}, {}, true, false};
    add_frames(run, "goblin_walk", 5, 0.10f);
    AnimationClip attack{"attack", {}, {}, false, false};
    add_frames(attack, "goblin_attack", 8, 0.07f);
    AnimationClip hurt{"hurt", {}, {}, false, false};
    add_frames(hurt, "goblin_hit", 5, 0.07f);
    AnimationClip die{"die", {}, {}, false, false};
    add_frames(die, "goblin_die", 17, 0.10f);
    return {idle, run, attack, hurt, die};
}

static std::vector<AnimationClip> villager_clips()
{
    AnimationClip idle{"idle", {}, {}, true, false};
    add_frames(idle, "villager_idle", 4, 0.15f);
    AnimationClip run{"run", {}, {}, true, false};
    add_frames(run, "villager_walk", 5, 0.10f);
    AnimationClip hurt{"hurt", {}, {}, false, false};
    add_frames(hurt, "villager_hit", 5, 0.07f);
    AnimationClip die{"die", {}, {}, false, false};
    add_frames(die, "villager_die", 14, 0.10f);
    return {idle, run, hurt, die};
}

static std::vector<AnimationClip> twknight_clips()
{
    AnimationClip idle{"idle", {}, {}, true, false};
    add_frames(idle, "twk_idle", 4, 0.15f);
    AnimationClip run{"run", {}, {}, true, false};
    add_frames(run, "twk_walk", 5, 0.10f);
    AnimationClip attack{"attack", {}, {}, false, false};
    add_frames(attack, "twk_attack", 6, 0.07f);
    AnimationClip hurt{"hurt", {}, {}, false, false};
    add_frames(hurt, "twk_hit", 5, 0.07f);
    AnimationClip die{"die", {}, {}, false, false};
    add_frames(die, "twk_die", 19, 0.10f);
    return {idle, run, attack, hurt, die};
}

// ── Public API ───────────────────────────────────────────────────────────

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

void register_default_clips(ResourceManager &resources)
{
    register_group(resources, "player", knight_clips());
    register_group(resources, "archer", archer_clips());
    register_group(resources, "goblin", goblin_clips());
    register_group(resources, "villager", villager_clips());
    register_group(resources, "knight", twknight_clips());

    // 1-frame static clip for structures and other non-animated entities
    resources.register_clip("structure_idle", {"idle", {"tile_0"}, {0.f}, false, false});
}

std::string determine_clip_name(Visual &vis, Vec2f velocity, bool alive, float dt)
{
    vis.hurt_timer = std::max(0.f, vis.hurt_timer - dt);
    vis.attack_timer = std::max(0.f, vis.attack_timer - dt);

    if (!alive)
        return "die";
    if (vis.hurt_timer > 0.f)
        return "hurt";
    if (vis.attack_timer > 0.f)
        return "attack";
    if (std::abs(velocity.x) > 10.f || std::abs(velocity.y) > 10.f)
        return "run";
    return "idle";
}

void switch_clip(Visual &vis, std::string const &clip_name, uint8_t entity_kind, uint8_t team,
                 uint8_t role, ResourceManager const &resources)
{
    char const *key_prefix = kind_key(entity_kind, team, role);
    if (!key_prefix)
        return;

    std::string key = std::string(key_prefix) + "_" + clip_name;
    AnimationClip const *c = resources.clip(key);
    if (!c)
        return;

    // No-op if already on this clip
    if (vis.clip == c)
        return;

    vis.clip = c;
    vis.frame_index = 0;
    vis.frame_timer = 0.f;
    vis.frame_durations = c->frame_durations;
}

char const *tick_animation(Visual &vis, Vec2f velocity, float dt)
{
    if (!vis.clip || vis.clip->frame_names.empty())
        return "entity";

    float dur =
        vis.frame_index < vis.frame_durations.size() ? vis.frame_durations[vis.frame_index] : 0.1F;
    vis.frame_timer += dt;
    while (vis.frame_timer >= dur) {
        vis.frame_timer -= dur;
        int next = vis.frame_index + 1;
        if (next < (int)vis.clip->frame_names.size())
            vis.frame_index = next;
        else if (vis.clip->loop)
            vis.frame_index = 0;
        else
            break;
        dur = vis.frame_index < vis.frame_durations.size() ? vis.frame_durations[vis.frame_index]
                                                           : 0.1F;
    }

    if (velocity.x > 1.f)
        vis.flip = !vis.clip->faces_right;
    else if (velocity.x < -1.f)
        vis.flip = vis.clip->faces_right;

    return vis.clip->frame_names[vis.frame_index].c_str();
}

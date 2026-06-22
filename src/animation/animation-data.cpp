#include "animation/animation-data.hpp"
#include "net/net-packet.hpp" // EntityKind
#include <cmath>

// -- static knight animation clip data --

static std::vector<AnimationClip> knight_clips;
static bool knight_clips_init = false;

static void init_knight_clips()
{
    if (knight_clips_init)
        return;
    knight_clips_init = true;

    auto idle =
        AnimationClip{.name = "idle", .frame_duration = 0.12f, .loop = true, .faces_right = false};
    for (int i = 0; i < 8; ++i)
        idle.frame_names.push_back("knight_idle_" + std::to_string(i));

    auto run = AnimationClip{"run", {}, 0.08f, true, false};
    for (int i = 0; i < 8; ++i)
        run.frame_names.push_back("knight_run_" + std::to_string(i));

    auto attack = AnimationClip{"attack", {}, 0.07f, false, false};
    for (int i = 0; i < 8; ++i)
        attack.frame_names.push_back("knight_attack_" + std::to_string(i));

    auto hurt = AnimationClip{"hurt", {}, 0.07f, false, false};
    for (int i = 0; i < 8; ++i)
        hurt.frame_names.push_back("knight_hurt_" + std::to_string(i));

    auto die = AnimationClip{"die", {}, 0.12f, false, false};
    for (int i = 0; i < 8; ++i)
        die.frame_names.push_back("knight_die_" + std::to_string(i));

    knight_clips = {std::move(idle), std::move(run), std::move(attack), std::move(hurt),
                    std::move(die)};
}

static std::vector<AnimationClip> empty_clips;

std::vector<AnimationClip> const &animation_clips_for_kind(uint8_t entity_kind, uint8_t /*team*/)
{
    using namespace EntityKind;
    switch (entity_kind) {
    case player:
    case soldier:
    case enemy:
    case npc:
        init_knight_clips();
        return knight_clips;
    default:
        return empty_clips;
    }
}

AnimationClip const *find_clip(std::vector<AnimationClip> const &clips, std::string const &name)
{
    for (auto &c : clips)
        if (c.name == name)
            return &c;
    return nullptr;
}

AnimationClip const *determine_clip(std::vector<AnimationClip> const &clips, AnimationState &state,
                                    Vec2f velocity, bool alive, float dt)
{
    // Tick down transient triggers
    state.hurt_timer = std::max(0.f, state.hurt_timer - dt);
    state.attack_timer = std::max(0.f, state.attack_timer - dt);

    if (!alive)
        return find_clip(clips, "die");
    if (state.hurt_timer > 0.f)
        return find_clip(clips, "hurt");
    if (state.attack_timer > 0.f)
        return find_clip(clips, "attack");
    if (std::abs(velocity.x) > 10.f || std::abs(velocity.y) > 10.f)
        return find_clip(clips, "run");
    return find_clip(clips, "idle");
}

void switch_clip(AnimationState &state, AnimationClip const *clip)
{
    if (state.clip == clip)
        return;
    state.clip = clip;
    state.frame_index = 0;
    state.frame_timer = 0.f;
}

char const *tick_animation(AnimationState &state, Vec2f velocity, float dt)
{
    if (!state.clip || state.clip->frame_names.empty())
        return "entity";

    // Advance timer
    state.frame_timer += dt;
    while (state.frame_timer >= state.clip->frame_duration) {
        state.frame_timer -= state.clip->frame_duration;
        int next = state.frame_index + 1;
        if (next < (int)state.clip->frame_names.size())
            state.frame_index = next;
        else if (state.clip->loop)
            state.frame_index = 0;
        // else: stay on last frame (non-looping clip finished)
    }

    // Update flip based on horizontal velocity vs clip's default facing
    if (velocity.x > 1.f)
        state.flip = !state.clip->faces_right;
    else if (velocity.x < -1.f)
        state.flip = state.clip->faces_right;

    return state.clip->frame_names[state.frame_index].c_str();
}

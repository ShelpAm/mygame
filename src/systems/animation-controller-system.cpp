#include "systems/animation-controller-system.hpp"

#include "animation/animation-data.hpp"
#include "components/building-data.hpp"
#include "components/combat-stats.hpp"
#include "components/entity-kind.hpp"
#include "components/movement.hpp"
#include "components/soldier-ai.hpp"
#include "components/visual-fx.hpp"
#include "components/visual/animation.hpp"
#include "components/visual/sprite.hpp"
#include "core/resource-manager.hpp"

AnimationControllerSystem::AnimationControllerSystem(flecs::world &world)
    : query_(world.query<Animation, Sprite, EntityKind>())
{
}

void AnimationControllerSystem::update(flecs::world &world, ResourceManager const &resources,
                                       float delta_time)
{
    query_.each([&](flecs::entity e, Animation &anim, Sprite &s, EntityKind const &kind) {
        // Copy HitFlash into local timers so determine_clip_name
        // can switch to hurt / attack clips.
        float hurt_timer = 0.F;
        float attack_timer = 0.F;
        if (auto const *hf = e.try_get<HitFlash>()) {
            hurt_timer = hf->remaining;
            attack_timer = hf->remaining;
        }

        auto const *mov = e.try_get<Movement>();
        auto const *cs = e.try_get<CombatStats>();
        auto clip_name = determine_clip_name(
            hurt_timer, attack_timer, mov ? mov->velocity : Vec2f{0, 0}, cs ? cs->alive : true);

        auto const &clipconf = resources.entity_config(kind.prototype).clip;
        if (!clipconf)
            throw std::runtime_error("Entity prototype " + kind.prototype +
                                     " has no clip defined in entities.yaml");
        std::string clip_key = clipconf.value() + "_" + clip_name;
        auto const *clip = resources.clip(clip_key);
        std::string fallback{"unavailable_idle"};
        assert(resources.clip(fallback));

        // If the primary clip wasn't found, fall back to structure_idle
        if (!clip) {
            clip = resources.clip("unavailable_idle");
            spdlog::warn("AnimationControllerSystem::update: clip group '{}' has no '{}' clip, "
                         "using fallback '{}'",
                         clipconf.value(), clip_name, fallback);
        }
        assert(clip);

        if (clip != anim.clip) {
            anim.clip = clip;
            anim.frame_index = 0;
            anim.frame_timer = clip->frame_durations.empty() ? 0.1F : clip->frame_durations[0];
        }

        // Flip based on movement direction
        if (mov && mov->velocity.x > 1.f)
            s.flip = clip ? !clip->faces_right : false;
        else if (mov && mov->velocity.x < -1.f)
            s.flip = clip ? clip->faces_right : false;
    });

    // Tick down HitFlash timers and clean up expired ones
    world.each([delta_time](flecs::entity e, HitFlash &hf) {
        hf.remaining -= delta_time;
        if (hf.remaining <= 0.f)
            e.remove<HitFlash>();
    });
}

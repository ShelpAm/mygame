#include "systems/animation-controller-system.hpp"

#include "animation/animation-data.hpp"
#include "components/combat-stats.hpp"
#include "components/entity-kind.hpp"
#include "components/movement.hpp"
#include "components/soldier-ai.hpp"
#include "components/visual/animation.hpp"
#include "components/visual/sprite.hpp"
#include "components/visual-fx.hpp"
#include "core/resource-manager.hpp"

void AnimationControllerSystem::update(flecs::world &world,
                                        ResourceManager const &resources,
                                        float /*delta_time*/)
{
    world.query<Animation, Sprite, Movement, CombatStats, KindTag>().each(
        [&](flecs::entity e, Animation &anim, Sprite &s, Movement const &mov,
            CombatStats const &cs, KindTag const &kt) {
            // Copy HitFlash into local timers so determine_clip_name
            // can switch to hurt / attack clips.
            float hurt_timer = 0.f;
            float attack_timer = 0.f;
            if (auto *hf = e.try_get<HitFlash>()) {
                hurt_timer = hf->remaining;
                attack_timer = hf->remaining;
            }

            auto clip_name = determine_clip_name(hurt_timer, attack_timer,
                                                  mov.velocity, cs.alive);

            uint8_t role = 0;
            if (auto *ai = e.try_get<SoldierAI>())
                role = static_cast<uint8_t>(ai->role);

            auto *clip = get_clip(clip_name, kt.value,
                                  static_cast<uint8_t>(cs.team),
                                  role, resources);

            if (clip && clip != anim.clip) {
                anim.clip = clip;
                anim.frame_index = 0;
                anim.frame_timer = clip->frame_durations.empty()
                                       ? 0.1F
                                       : clip->frame_durations[0];
            }

            // Flip based on movement direction
            if (mov.velocity.x > 1.f)
                s.flip = clip ? !clip->faces_right : false;
            else if (mov.velocity.x < -1.f)
                s.flip = clip ? clip->faces_right : false;
        });
}

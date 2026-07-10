#include "systems/animation-system.hpp"
#include "components/visual/animation.hpp"
#include "components/visual/sprite.hpp"
#include "core/resource-manager.hpp"
#include <cassert>

void AnimationSystem::update(flecs::world &world, ResourceManager const &resources, float delta_time)
{
    world.query<Animation, Sprite>().each(
        [delta_time, &resources](flecs::entity, Animation &anim, Sprite &s) {
            assert(anim.clip != nullptr && "AnimationSystem: entity missing initial clip");

            anim.frame_timer -= delta_time;
            if (anim.frame_timer <= 0) {
                ++anim.frame_index;
                if (anim.frame_index >= anim.clip->frame_sprites.size()) {
                    if (anim.looping)
                        anim.frame_index = 0;
                    else
                        anim.frame_index = anim.clip->frame_sprites.size() - 1;
                }
                anim.frame_timer = anim.clip->frame_durations.empty()
                                       ? 0.1F
                                       : anim.clip->frame_durations[anim.frame_index];
            }

            // frame_sprites[frame_index] is a sprite name keying into sprites.yaml.
            s.texture_name = anim.clip->frame_sprites[anim.frame_index];

            // Resolve the sprite name through sprites.yaml to get the actual
            // texture key and clipping rect.
            auto *def = resources.resolve_sprite(s.texture_name);
            if (def) {
                s.texture_name = def->texture;
                s.offset = {def->clip[0], def->clip[1]};
                s.size   = {def->clip[2], def->clip[3]};
            } else {
                // No sprite def — texture_name stays as-is, no clip.
                s.offset = {0, 0};
                s.size   = {1, 1};
            }
        });
}

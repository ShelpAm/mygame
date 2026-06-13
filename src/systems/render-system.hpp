#pragma once

#include "core/math.hpp"
#include "entities/components/visual-fx.hpp"
#include "entities/entity-manager.hpp"
#include <SDL3/SDL.h>
#include <flecs.h>
#include <vector>
class ResourceManager;
class CameraSystem;
class WorldState;
class NavigationSystem;

struct CombatEvent;

class RenderSystem {
  public:
    RenderSystem(SDL_Renderer *renderer, ResourceManager &resources,
                 CameraSystem &camera);

    void render(flecs::world &world, WorldState const &world_state,
                NavigationSystem const &nav,
                std::vector<CombatEvent> const &combat_events,
                Vec2f player_pos, EntityId player_id);

  private:
    SDL_Renderer *renderer_;
    ResourceManager &resources_;
    CameraSystem &camera_;

    void render_tile_map(WorldState const &world_state,
                         NavigationSystem const &nav);
    void render_entities(flecs::world &world, Vec2f player_pos,
                         EntityId player_id);
    void render_health_bars(flecs::world &world);
    void render_damage_numbers(flecs::world &world,
                               std::vector<CombatEvent> const &events);

    std::vector<FloatingText> floating_texts_;
};

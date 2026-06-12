#pragma once

#include "core/math.hpp"
#include "entities/components/visual-fx.hpp"
#include "entities/entity-manager.hpp"
#include <SDL3/SDL.h>
#include <vector>
class ResourceManager;
class CameraSystem;
class EntityManager;
class WorldState;
class NavigationSystem;

struct CombatEvent;

class RenderSystem {
  public:
    RenderSystem(SDL_Renderer *renderer, ResourceManager &resources,
                 CameraSystem &camera);

    void render(EntityManager &entities, WorldState const &world_state,
                NavigationSystem const &nav,
                std::vector<CombatEvent> const &combat_events,
                Vec2f player_pos, EntityId player_id);

  private:
    SDL_Renderer *renderer_;
    ResourceManager &resources_;
    CameraSystem &camera_;

    void render_tile_map(WorldState const &world_state,
                         NavigationSystem const &nav);
    void render_entities(EntityManager &entities, Vec2f player_pos,
                         EntityId player_id);
    void render_health_bars(EntityManager &entities);
    void render_damage_numbers(EntityManager &entities,
                               std::vector<CombatEvent> const &events);

    std::vector<FloatingText> floating_texts_;
};

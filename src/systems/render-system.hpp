#pragma once

#include "entities/components/visual-fx.hpp"
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
                std::vector<CombatEvent> const &combat_events);

  private:
    SDL_Renderer *renderer_;
    ResourceManager &resources_;
    CameraSystem &camera_;

    void render_tile_map(WorldState const &world_state,
                         NavigationSystem const &nav);
    void render_entities(EntityManager &entities);
    void render_health_bars(EntityManager &entities);
    void render_damage_numbers(std::vector<CombatEvent> const &events);

    std::vector<FloatingText> floating_texts_;
};

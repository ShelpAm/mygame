#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/visual-fx.hpp"
#include <SDL3/SDL.h>
#include <vector>
class ResourceManager;
class CameraSystem;
class PlayerVisibility;
class Client;
class WorldState;
class NavigationSystem;

struct CombatEvent;

class RenderSystem {
  public:
    RenderSystem(SDL_Renderer *renderer, ResourceManager &resources,
                 CameraSystem &camera);

    void render(Client &client, NavigationSystem const &nav);

  private:
    SDL_Renderer *renderer_;
    ResourceManager &resources_;
    CameraSystem &camera_;

    void render_tile_map(PlayerVisibility const &vis,
                         NavigationSystem const &nav);
    void render_entities(Client &client, Vec2f player_pos, EntityId player_id);
    void render_projectiles(Client &client);
    void render_health_bars(Client &client);
    void render_damage_numbers(Client &client,
                               std::vector<CombatEvent> const &events);

    std::vector<FloatingText> floating_texts_;
};

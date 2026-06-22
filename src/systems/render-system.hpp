#pragma once

#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/visual-fx.hpp"
#include <SDL3/SDL.h>
#include <vector>
class ResourceManager;
class CameraSystem;
struct PlayerVisibility;
class Client;
class WorldState;
class NavigationSystem;

struct CombatEvent;

class RenderSystem {
  public:
    RenderSystem(SDL_Renderer *renderer, ResourceManager &resources,
                 CameraSystem &camera);

    void render(Client const &client, NavigationSystem const &nav);

  private:
    static constexpr auto max_alpha = 255;
    SDL_Renderer *renderer_;
    ResourceManager &resources_;
    CameraSystem &camera_;

    void render_tile_map(PlayerVisibility const &vis,
                         NavigationSystem const &nav);
    void render_entities(Client const &client, Vec2f player_pos,
                         EntityId player_id);
    void render_projectiles(Client const &client);
    void render_health_bars(Client const &client, Vec2f player_pos);
    void render_damage_numbers(Client const &client,
                               std::vector<CombatEvent> const &events);
    void render_fog_overlay(PlayerVisibility const &vis);

    void draw_sprite(Vec2f center, float width, std::string const &tex,
                     uint8_t alpha, bool flip);

    // distance -> alpha mapping (smooth fade at boundary)
    static std::uint8_t alpha_for(float d)
    {
        return static_cast<std::uint8_t>(
            max_alpha * std::max(0.F, 1 - ((d * d) / (500 * 500))));
    };

    std::vector<FloatingText> floating_texts_;
};

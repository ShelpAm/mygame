#pragma once

#include "core/font.hpp"
#include "core/game-types.hpp"
#include "core/math.hpp"
#include "entities/components/visual-fx.hpp"
#include "world/location-store.hpp"
#include <SDL3/SDL.h>
#include <vector>
class ResourceManager;
class CameraSystem;
struct PlayerVisibility;
class Client;
class WorldState;
class NavigationSystem;
class MapData;

struct CombatEvent;

class RenderSystem {
  public:
    RenderSystem(SDL_Window *window, SDL_Renderer *renderer, ResourceManager *resources,
                 CameraSystem *camera, Font *font);

    void render(Client const &client, NavigationSystem const &nav);
    void toggle_debug_mode() { debug_mode_ = !debug_mode_; }

    void set_map_data(MapData const *md) { map_data_ = md; }
    void set_location_defs(std::vector<LocationDefinition> const *defs) { location_defs_ = defs; }

    SDL_Window *window() const { return window_; }
    SDL_Renderer *renderer() const { return renderer_; }

  private:
    static constexpr auto max_alpha = 255;
    SDL_Window *window_;
    SDL_Renderer *renderer_;
    ResourceManager *resources_;
    CameraSystem *camera_;
    Font *font_;
    MapData const *map_data_ = nullptr;
    std::vector<LocationDefinition> const *location_defs_ = nullptr;
    bool debug_mode_ = false;

    void render_tile_map(PlayerVisibility const &vis, NavigationSystem const &nav) const;
    void render_town_labels(Client const &client) const;
    void render_entities(Client const &client, Vec2f pos, EntityId eid);
    void render_projectiles(Client const &client);
    void render_health_bars(Client const &client, Vec2f player_pos);
    void render_damage_numbers(Client const &client, std::vector<CombatEvent> const &events);
    void render_fog_overlay(PlayerVisibility const &vis);

    void draw_sprite(Vec2f center, float width, std::string const &tex, uint8_t alpha,
                     bool flip) const;

    // distance -> alpha mapping (smooth fade at boundary)
    static std::uint8_t alpha_for(float d)
    {
        return static_cast<std::uint8_t>(max_alpha * std::max(0.F, 1 - ((d * d) / (500 * 500))));
    };

    std::vector<FloatingText> floating_texts_;
};

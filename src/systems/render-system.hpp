#pragma once

#include "components/visual-fx.hpp"
#include "core/font.hpp"
#include "core/game-types.hpp"
#include "core/math.hpp"
#include "world/location-store.hpp"
#include <array>
#include <SDL3/SDL.h>
#include <vector>
class ResourceManager;
class CameraSystem;
struct PlayerVisibility;
class Client;
class WorldState;
class NavigationSystem;
class MapData;
struct Sprite;

struct CombatEvent;

// Core thought -- layered resource pipeline:
// 1. Texture - Physical image data
// 2. Sprite - Adding clipping rect information
// 3. Animation/AnimationClip (optional for entity) - Sequence of Sprites with timing

class RenderSystem {
  public:
    RenderSystem(SDL_Window *window, SDL_Renderer *renderer, ResourceManager *resources,
                 CameraSystem *camera, Font *font);

    void render(Client &client);

    void toggle_debug_mode() { debug_mode_ = !debug_mode_; }
    bool debug_mode() const { return debug_mode_; }

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

    // Pre-resolved tile sprites. Index = auto-tiling 4-bit value (0–15).
    // Populated once from the "terrain_tiles" clip + sprites.yaml.
    struct CachedTile {
        std::string texture_name;
        Vec2f clip_offset;
        Vec2f clip_size;
    };
    std::array<CachedTile, 16> tile_cache_{};
    bool tile_cache_ready_ = false;
    void init_tile_cache();

    void render_tile_map() const;
    void render_town(Client const &client) const;
    void render_entities(Client const &client, Vec2f local_player_pos);
    void render_projectiles(Client const &client);
    void render_damage_numbers(Client &client, std::vector<CombatEvent> const &events);
    void render_fog_overlay(PlayerVisibility const &vis);
    void render_debug(Client const &client) const;

    enum class RectMode : std::uint8_t {
        fill, // Fill
        line, // Outline
    };
    void draw_rectangle(Vec2f left_up, Vec2f size, SDL_Color color, RectMode mode) const;
    // Low-level draw: texture name + parameters. origin is normalized [0,1].
    // clip_offset / clip_size are normalized (0..1) — the sub-rect within the
    // texture.  clip_size=(0,0) means full texture (fallback).
    // Prefer draw(Sprite, pos) when a Sprite component is available.
    void draw_sprite(Vec2f foot_pos, float scale, std::string const &tex, uint8_t alpha, bool flip,
                     Vec2f origin, Vec2f clip_offset = {0, 0}, Vec2f clip_size = {1, 1}) const;
    void draw(Sprite const &s, Vec2f pos, uint8_t alpha = max_alpha) const;

    void draw_text_with_bg(Vec2f pos, std::string const &text, SDL_Color text_color,
                           SDL_Color bg_color) const;

    // distance -> alpha mapping (smooth fade at boundary)
    static std::uint8_t alpha_for(float d)
    {
        return static_cast<std::uint8_t>(max_alpha * std::max(0.F, 1 - ((d * d) / (500 * 500))));
    };
};

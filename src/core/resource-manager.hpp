#pragma once

#include "core/camera-device.hpp"
#include "core/game-types.hpp"
#include "core/math.hpp"
#include "game-data.hpp"
#include <array>
#include <math.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <unordered_map>

/// A named sprite definition in the sprite layer.
/// Maps a sprite name → (texture key + normalized clip rect).
struct SpriteDef {
    std::string texture;
    std::array<float, 4> clip; // [x, y, w, h] normalized (0..1)
};

class ResourceManager {
  public:
    ResourceManager()
        : entity_database_{read_entity_database_from_yaml("assets/data/entities.yaml")}
    {
    }
    ResourceManager(ResourceManager const &) = delete;
    ResourceManager(ResourceManager &&) = delete;
    ResourceManager &operator=(ResourceManager const &) = delete;
    ResourceManager &operator=(ResourceManager &&) = delete;
    ~ResourceManager();

    /// Load a single texture from a PNG file.
    SDL_Texture *load_texture(SDL_Renderer *renderer, std::string const &name,
                              std::string const &path);

    SDL_Texture *texture(std::string const &name) const;
    Vec2f texture_size(std::string const &name) const;

    /// Load a horizontal spritesheet and split into individual frame textures.
    /// Frames are named `{base_name}_0`, `{base_name}_1`, ..., `{base_name}_{n-1}`.
    /// @param frame_w width of a single frame (the spritesheet's height = frame height).
    /// @throws std::runtime_error on any loading/processing failure.
    void load_spritesheet(SDL_Renderer *renderer, std::string const &base_name,
                          std::string const &path, int frame_w);

    /// Register a named animation clip.
    void register_clip(std::string const &name, AnimationClip clip);

    /// Look up a clip by name. Returns nullptr if not found.
    AnimationClip const *clip(std::string const &name) const;

    /// ── Sprite layer (sprites.yaml) ────────────────────────────────────────────

    /// Load sprite definitions from a YAML file.
    void load_sprites(std::string const &path);

    /// Resolve a sprite name to its SpriteDef.
    /// Returns nullptr if no definition exists for the given name.
    SpriteDef const *resolve_sprite(std::string const &sprite_name) const;

    EntityConfig const &entity_config(std::string const &entity_type) const;

    CameraDevice &camera() { return *camera_; }

    void clear();

  private:
    std::unordered_map<std::string, SDL_Texture *> textures_;
    std::unordered_map<std::string, AnimationClip> clips_;
    std::unordered_map<std::string, SpriteDef> sprites_;
    std::unique_ptr<CameraDevice> camera_;
    EntityDatabase entity_database_;
};

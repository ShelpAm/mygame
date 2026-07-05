#pragma once

#include "core/camera-device.hpp"
#include "core/game-types.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <unordered_map>

class ResourceManager {
  public:
    ResourceManager() = default;
    ResourceManager(ResourceManager const &) = delete;
    ResourceManager(ResourceManager &&) = delete;
    ResourceManager &operator=(ResourceManager const &) = delete;
    ResourceManager &operator=(ResourceManager &&) = delete;
    ~ResourceManager();

    /// Load a single texture from a PNG file.
    SDL_Texture *load_texture(SDL_Renderer *renderer, std::string const &name,
                              std::string const &path);

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

    SDL_Texture *texture(std::string const &name);

    CameraDevice &camera() { return *camera_; }

    void clear();

  private:
    std::unordered_map<std::string, SDL_Texture *> textures_;
    std::unordered_map<std::string, AnimationClip> clips_;
    std::unique_ptr<CameraDevice> camera_;
};

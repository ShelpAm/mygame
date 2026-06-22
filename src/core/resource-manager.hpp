#pragma once

#include "core/camera-device.hpp"
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

    SDL_Texture *load_texture(SDL_Renderer *renderer, std::string const &name,
                              std::string const &path);
    SDL_Texture *texture(std::string const &name);

    CameraDevice &camera() { return *camera_; }

    void clear();

  private:
    std::unordered_map<std::string, SDL_Texture *> textures_;
    // std::unordered_map<std::string, TTF_Font *> fonts_;
    std::unique_ptr<CameraDevice> camera_;
};

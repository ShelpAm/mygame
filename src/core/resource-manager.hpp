#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <unordered_map>

class ResourceManager {
  public:
    ~ResourceManager();

    SDL_Texture *load_texture(SDL_Renderer *renderer, std::string const &name,
                              std::string const &path);
    SDL_Texture *texture(std::string const &name);
    void clear();

  private:
    std::unordered_map<std::string, SDL_Texture *> textures_;
};

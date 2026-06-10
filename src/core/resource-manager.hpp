#pragma once

#include <SDL3/SDL.h>
#include <unordered_map>
#include <string>
#include <memory>

class ResourceManager {
public:
    ~ResourceManager();

    SDL_Texture* load_texture(SDL_Renderer* renderer, const std::string& name, const std::string& path);
    SDL_Texture* texture(const std::string& name);
    void clear();

private:
    std::unordered_map<std::string, SDL_Texture*> textures_;
};

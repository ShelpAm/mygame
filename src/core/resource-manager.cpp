#include "core/resource-manager.hpp"

#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

ResourceManager::~ResourceManager()
{
    clear();
}

SDL_Texture *ResourceManager::load_texture(SDL_Renderer *renderer,
                                           std::string const &name,
                                           std::string const &path)
{
    if (textures_.contains(name)) {
        spdlog::warn("ResourceManager: texture {} already loaded, returning "
                     "existing one",
                     name);
        return textures_.at(name);
    }

    SDL_Surface *surface = IMG_Load(path.c_str());
    if (!surface) {
        spdlog::error("ResourceManager: failed to load texture {}: {}", path,
                      SDL_GetError());
        return nullptr;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        spdlog::error("ResourceManager: failed to create texture from {}: {}",
                      path, SDL_GetError());
        return nullptr;
    }
    textures_[name] = texture;
    return texture;
}

SDL_Texture *ResourceManager::texture(std::string const &name)
{
    auto it = textures_.find(name);
    return it != textures_.end() ? it->second : nullptr;
}

void ResourceManager::clear()
{
    for (auto &[name, tex] : textures_) {
        SDL_DestroyTexture(tex);
    }
    textures_.clear();
}

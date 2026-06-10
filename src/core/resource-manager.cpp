#include "core/resource-manager.hpp"
ResourceManager::~ResourceManager() {
    clear();
}

SDL_Texture* ResourceManager::load_texture(SDL_Renderer* renderer, const std::string& name, const std::string& path) {
    SDL_Surface* surface = SDL_LoadBMP(path.c_str());
    if (!surface) {
        SDL_Log("Failed to load texture %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        SDL_Log("Failed to create texture from %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    textures_[name] = texture;
    return texture;
}

SDL_Texture* ResourceManager::texture(const std::string& name) {
    auto it = textures_.find(name);
    return it != textures_.end() ? it->second : nullptr;
}

void ResourceManager::clear() {
    for (auto& [name, tex] : textures_) {
        SDL_DestroyTexture(tex);
    }
    textures_.clear();
}

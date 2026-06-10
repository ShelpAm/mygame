#include "core/ResourceManager.hpp"

ResourceManager::~ResourceManager() {
    clear();
}

SDL_Texture* ResourceManager::loadTexture(SDL_Renderer* renderer, const std::string& name, const std::string& path) {
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
    m_textures[name] = texture;
    return texture;
}

SDL_Texture* ResourceManager::texture(const std::string& name) {
    auto it = m_textures.find(name);
    return it != m_textures.end() ? it->second : nullptr;
}

void ResourceManager::clear() {
    for (auto& [name, tex] : m_textures) {
        SDL_DestroyTexture(tex);
    }
    m_textures.clear();
}

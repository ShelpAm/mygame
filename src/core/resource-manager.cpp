#include "core/resource-manager.hpp"

#include <format>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

ResourceManager::~ResourceManager()
{
    clear();
}

SDL_Texture *ResourceManager::load_texture(SDL_Renderer *renderer, std::string const &name,
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
        spdlog::error("ResourceManager: failed to load texture {}: {}", path, SDL_GetError());
        return nullptr;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        spdlog::error("ResourceManager: failed to create texture from {}: {}", path,
                      SDL_GetError());
        return nullptr;
    }
    textures_[name] = texture;
    return texture;
}

void ResourceManager::load_spritesheet(SDL_Renderer *renderer, std::string const &base_name,
                                       std::string const &path, int frame_w)
{
    SDL_Surface *sheet = IMG_Load(path.c_str());
    if (!sheet)
        throw std::runtime_error(std::format("ResourceManager: failed to load spritesheet {}: {}",
                                             path, SDL_GetError()));

    int const frame_h = sheet->h;
    int const count = sheet->w / frame_w;

    for (int i = 0; i < count; ++i) {
        std::string key = base_name + "_" + std::to_string(i);

        SDL_Surface *frame = SDL_CreateSurface(frame_w, frame_h, sheet->format);
        if (!frame) {
            SDL_DestroySurface(sheet);
            throw std::runtime_error(
                std::format("ResourceManager: failed to create frame surface: {}", SDL_GetError()));
        }

        SDL_Rect src = {i * frame_w, 0, frame_w, frame_h};
        if (!SDL_BlitSurface(sheet, &src, frame, nullptr)) {
            SDL_DestroySurface(frame);
            SDL_DestroySurface(sheet);
            throw std::runtime_error(
                std::format("ResourceManager: blit failed for frame {}: {}", i, SDL_GetError()));
        }

        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, frame);
        if (!tex) {
            SDL_DestroySurface(frame);
            SDL_DestroySurface(sheet);
            throw std::runtime_error(std::format(
                "ResourceManager: failed to create texture for frame {}: {}", i, SDL_GetError()));
        }
        textures_[key] = tex;
        SDL_DestroySurface(frame);
    }

    SDL_DestroySurface(sheet);
}

SDL_Texture *ResourceManager::texture(std::string const &name) const
{
    auto it = textures_.find(name);
    if (it == textures_.end())
        throw std::runtime_error("couldn't find texture " + name);
    return it->second;
}

Vec2f ResourceManager::texture_size(std::string const &name) const
{
    auto *tex = texture(name);
    if (!tex)
        throw std::runtime_error("ResourceManager: texture not found: " + name);

    float w{};
    float h{};
    SDL_GetTextureSize(tex, &w, &h);
    return {w, h};
}

void ResourceManager::clear()
{
    for (auto &[name, tex] : textures_) {
        SDL_DestroyTexture(tex);
    }
    textures_.clear();
}

void ResourceManager::register_clip(std::string const &name, AnimationClip c)
{
    clips_[name] = std::move(c);
}

AnimationClip const *ResourceManager::clip(std::string const &name) const
{
    auto it = clips_.find(name);
    return it != clips_.end() ? &it->second : nullptr;
}

// ── Sprite layer ──────────────────────────────────────────────────────────

#include <filesystem>
#include <fstream>
#include <rfl.hpp>
#include <rfl/yaml.hpp>

void ResourceManager::load_sprites(std::string const &path)
{
    namespace fs = std::filesystem;
    if (!fs::exists(path)) {
        spdlog::warn("ResourceManager: sprites.yaml not found at {}", path);
        return;
    }

    auto read_file = [](std::string const &p) {
        std::ifstream f(p);
        return std::string{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    };

    auto content = read_file(path);
    if (content.empty()) {
        spdlog::warn("ResourceManager: sprites.yaml is empty");
        return;
    }

    struct SpriteEntry {
        std::string texture;
        std::array<float, 4> clip;
    };
    struct SpriteFileConfig {
        std::map<std::string, SpriteEntry> sprites;
    };

    auto result = rfl::yaml::read<SpriteFileConfig>(content);
    if (!result) {
        spdlog::error("ResourceManager: failed to parse sprites.yaml: {}", result.error().what());
        return;
    }

    auto const &cfg = result.value();
    for (auto const &[name, entry] : cfg.sprites) {
        sprites_[name] = {entry.texture, entry.clip};
    }
    spdlog::info("ResourceManager: loaded {} sprite definitions from sprites.yaml", sprites_.size());
}

SpriteDef const *ResourceManager::resolve_sprite(std::string const &sprite_name) const
{
    auto it = sprites_.find(sprite_name);
    return it != sprites_.end() ? &it->second : nullptr;
}

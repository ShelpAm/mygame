#pragma once

#include "core/math.hpp"
#include <filesystem>
#include <format>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdexcept>
#include <string>
#include <unordered_map>

/// @brief Wrapper aound TTF_Font and TTF_TextEngine.
///
/// Draw fonts in three steps:
/// - load font file
/// - create text engine
/// - make text from engine
class Font {
  public:
    Font(Font const &) = default;
    Font(Font &&) = delete;
    Font &operator=(Font const &) = default;
    Font &operator=(Font &&) = delete;

    Font(SDL_Renderer *renderer, std::filesystem::path const &path, float size)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        : font_(TTF_OpenFont(reinterpret_cast<char const *>(path.u8string().c_str()), size)),
          engine_(TTF_CreateRendererTextEngine(renderer)),
          text_(TTF_CreateText(engine_, font_, "", 0))
    {
        if (!font_)
            throw std::runtime_error(
                std::format("Failed to load font {}: {}", path.string(), SDL_GetError()));

        if (!engine_)
            throw std::runtime_error(
                std::format("Failed to create text engine: {}", SDL_GetError()));

        if (!text_)
            throw std::runtime_error(std::format("Failed to create text: {}", SDL_GetError()));
    }

    ~Font()
    {
        if (font_)
            TTF_CloseFont(font_);

        if (engine_)
            TTF_DestroyRendererTextEngine(engine_);

        if (text_)
            TTF_DestroyText(text_);
    }

    void patch_fallback(Font const &f) { TTF_AddFallbackFont(font_, f.font_); }

    void draw(Vec2f pos, SDL_Color c, std::string_view str)
    {
        TTF_SetTextColor(text_, c.r, c.g, c.b, c.a);
        TTF_SetTextString(text_, str.data(), str.size());
        TTF_DrawRendererText(text_, pos.x, pos.y);
    }

    /// @return Size in pixels of the given string when rendered with this font.
    Vec2i mesure_string_size(std::string_view str)
    {
        Vec2i res;
        TTF_GetStringSize(font_, str.data(), str.size(), &res.x, &res.y);
        return res;
    }

  private:
    TTF_Font *font_;
    TTF_TextEngine *engine_;
    TTF_Text *text_;
};

class FontManager {
  public:
    FontManager(SDL_Renderer *r) : renderer_(r) {}
    FontManager(FontManager const &) = delete;
    FontManager(FontManager &&) = delete;
    FontManager &operator=(FontManager const &) = delete;
    FontManager &operator=(FontManager &&) = delete;
    ~FontManager() = default;

    static void init()
    {
        if (!TTF_Init())
            throw std::runtime_error(std::format("Failed to initialize SDL_ttf: ", SDL_GetError()));
    }

    static void shutdown() { TTF_Quit(); }

    Font *load_font(std::string const &path, float size)
    {
        std::string key = path + ":" + std::to_string(size);
        if (!fonts_.contains(path)) {
            fonts_[key] = std::make_unique<Font>(renderer_, path, size);
        }
        return fonts_[key].get();
    }

    Font *font(std::string const &name)
    {
        if (!fonts_.contains(name))
            throw std::runtime_error(std::format("Font not found: {}", name));
        return fonts_[name].get();
    }

  private:
    SDL_Renderer *renderer_;
    std::unordered_map<std::string, std::unique_ptr<Font>> fonts_;
};

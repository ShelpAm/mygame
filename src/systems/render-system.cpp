#include "systems/render-system.hpp"
#include "animation/animation-data.hpp"
#include "components/building-data.hpp"
#include "components/collider.hpp"
#include "components/combat-stats.hpp"
#include "components/entity-kind.hpp"
#include "components/interactable.hpp"
#include "components/interpolation-target.hpp"
#include "components/position.hpp"
#include "components/soldier-ai.hpp"
#include "components/visual-fx.hpp"
#include "components/visual/sprite.hpp"
#include "core/resource-manager.hpp"
#include "net/client.hpp"
#include "systems/camera-system.hpp"
#include "systems/navigation-system.hpp"
#include "world/map-data.hpp"
#include <cassert>
#include <ranges>
#include <string>
#include <unordered_map>

RenderSystem::RenderSystem(SDL_Window *window, SDL_Renderer *renderer, ResourceManager *resources,
                           CameraSystem *camera, Font *font)
    : window_(window), renderer_(renderer), resources_(resources), camera_(camera), font_(font)
{
}

void RenderSystem::render(Client &client)
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    render_tile_map();
    render_town(client);
    render_entities(client, client.player_position());
    render_projectiles(client);
    render_damage_numbers(client, client.combat_events());
    render_fog_overlay(client.player_visibility());
}

void RenderSystem::render_tile_map() const
{
    assert(map_data_);
    auto vp = camera_->viewport();
    auto left_up = world_to_tile(Vec2f(vp.x, vp.y));
    auto right_down = world_to_tile(Vec2f(vp.x + vp.w, vp.y + vp.h));

    for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
        for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
            Vec2i tile(x, y);
            auto lu = lu_of_tile(tile);

            // Layer 1: auto-tiled terrain for every tile
            int idx{};
            std::vector<std::pair<int, int>> const surround{{-1, -1}, {-1, 0}, {0, -1}, {0, 0}};
            for (auto [i, d] : std::views::enumerate(surround)) {
                auto [ny, nx] = d;
                ny += y;
                nx += x;
                if (map_data_->tile(nx, ny).type != TileType::water)
                    idx += 1 << i;
            }
            draw_sprite(lu, tile_size / 128, "tile_" + std::to_string(idx), max_alpha, false,
                        {0.5F, 0.5F});
        }
    }

    if (debug_mode_) {
        // Draw debug text
        SDL_SetRenderDrawColor(renderer_, 255, 0, 0, 100);
        // Grid lines
        for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
            for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
                auto tile = Vec2i(x, y);
                auto lu = lu_of_tile(tile);
                auto c = camera_->world_to_screen(lu);
                font_->draw({c.x, c.y}, SDL_Color{.r = 255, .g = 0, .b = 0, .a = 150},
                            std::format("({}, {})", x, y));
            }
        }
    }
}

void RenderSystem::render_town(Client const &client) const
{
    assert(map_data_);
    assert(location_defs_);

    auto vp = camera_->viewport();
    auto left_up = world_to_tile(Vec2f(vp.x, vp.y));
    auto right_down = world_to_tile(Vec2f(vp.x + vp.w, vp.y + vp.h));

    auto const &discovered = client.discovered_towns();

    for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
        for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
            Vec2i tile(x, y);
            if (!map_data_->in_bounds(x, y))
                continue;

            TileType type = map_data_->tile(x, y).type;
            if (type == TileType::grass || type == TileType::water || type == TileType::mountain || type == TileType::building)
                continue;

            // Draw overlay for road/wall
            Vec2f center = center_of_tile(tile);
            Vec2f screen = camera_->world_to_screen(center);
            auto const zoom = camera_->zoom();
            float const size = tile_size * zoom + 2.F;
            SDL_FRect overlay{
                .x = screen.x - size / 2.F, .y = screen.y - size / 2.F, .w = size, .h = size};

            if (type == TileType::road) {
                SDL_SetRenderDrawColor(renderer_, 180, 160, 110, 200);
                SDL_RenderFillRect(renderer_, &overlay);
            }
            else if (type == TileType::wall) {
                SDL_SetRenderDrawColor(renderer_, 80, 75, 70, 230);
                SDL_RenderFillRect(renderer_, &overlay);
                SDL_SetRenderDrawColor(renderer_, 110, 105, 95, 255);
                SDL_RenderRect(renderer_, &overlay);
            }
        }
    }

    // Town name labels
    if (discovered.empty())
        return;

    for (auto const &def : *location_defs_) {
        bool found = false;
        for (auto const &dt : discovered) {
            if (dt.loc_id == def.id) {
                found = true;
                break;
            }
        }
        if (!found)
            continue;

        Vec2f center = center_of_tile(def.tile_center);
        Vec2f screen = camera_->world_to_screen(center);
        float label_y = screen.y - tile_size;

        // Shadow for readability over bright terrain
        font_->draw({screen.x + 1.F, label_y + 1.F}, SDL_Color{.r = 0, .g = 0, .b = 0, .a = 180},
                    def.display_name_en);
        font_->draw({screen.x, label_y}, SDL_Color{.r = 255, .g = 240, .b = 200, .a = 255},
                    def.display_name_en);
    }
}

void RenderSystem::render_entities(Client const &client, Vec2f local_player_pos)
{
    auto const &vis = client.player_visibility();

    // --- 1. Live entities via ECS query ---
    auto query = client.world().query<Transform, Sprite, CombatStats>();
    query.each([&](flecs::entity e, Transform const &t, Sprite const &s, CombatStats const &cs) {
        // Skip structures (but not buildings)
        auto const *kt = e.try_get<KindTag>();
        if (kt && kt->value == EntityKind::structure)
            return;

        // Skip if not explored
        auto tile = world_to_tile(t.world_pos);
        auto tv = vis.query(tile);
        if (tv == TileVisibility::Unexplored)
            return;

        float dist = (t.world_pos - local_player_pos).length();
        auto alpha = alpha_for(dist);
        // Draw entity sprite
        draw(s, t.world_pos, alpha);

        // Common properties for debug and other renderings
        Vec2f screen = camera_->world_to_screen(t.world_pos);
        auto tex_sz = resources_->texture_size(s.texture_name);
        auto scaled_size = s.scale * s.size * tex_sz;
        Vec2f sprite_top_left = screen - (s.origin * scaled_size);

        // Interactable hint
        // TODO: use Focused tag to identify currently focused entity.
        if (e.has<Interactable>() && dist <= 48) {
            float hintW = 4.F;
            float hintH = 16.F;

            // X 轴居中：左上角 X + (角色动画宽 - 提示宽) * 0.5
            float hintX = sprite_top_left.x + (scaled_size.x - hintW) * 0.5F;

            // Y 轴悬浮：放在最头顶。
            float hintY = sprite_top_left.y - hintH;

            draw_rectangle({hintX, hintY}, {hintW, hintH}, {255, 255, 100, 220}, RectMode::fill);
        }

        // health bar
        float barW = 32.F;
        float barH = 4.F;
        // 居中对齐：左上角 X + (图片缩放宽 - 血条宽) / 2
        float barX = sprite_top_left.x + (scaled_size.x - barW) * 0.5F;
        // 顶部对齐：左上角 Y - 血条高 - 间距
        float barY = sprite_top_left.y - barH - 4.F;

        // Full width
        draw_rectangle({barX, barY}, {barW, barH}, {40, 10, 10, alpha}, RectMode::fill);

        // Actual width
        float ratio = static_cast<float>(cs.hp) / static_cast<float>(cs.max_hp);
        bool hostile = is_hostile(cs.team, client.player_team());
        SDL_FColor col_f = hostile ? SDL_FColor{.r = 0.9F, .g = 0.2F, .b = 0.1F, .a = 1.F}
                                   : SDL_FColor{.r = 0.2F, .g = 0.8F, .b = 0.3F, .a = 1.F};
        SDL_Color color{static_cast<Uint8>(col_f.r * 255), static_cast<Uint8>(col_f.g * 255),
                        static_cast<Uint8>(col_f.b * 255), alpha};
        draw_rectangle({barX, barY}, {barW * ratio, barH}, color, RectMode::fill);

        if (debug_mode_) {
            font_->draw({screen.x, screen.y - 24.F}, SDL_Color{.r = 255, .g = 0, .b = 0, .a = 150},
                        std::format("id={} team={} tex={}", e.id(), static_cast<int>(cs.team),
                                    s.texture_name));

            // Green dot for entity pos
            constexpr Vec2f tag_size{8.F, 8.F};
            draw_rectangle(screen - tag_size * 0.5, tag_size, {0, 255, 0, 255}, RectMode::fill);

            // Cyan box for sprite size (respects clip rect)
            auto origin = s.origin * scaled_size;
            draw_rectangle(screen - origin, scaled_size, SDL_Color{0, 255, 255, 160},
                           RectMode::box);

            // Orange box for actual collider (world-space AABB)
            if (auto const *c = e.try_get<Collider>()) {
                Vec2f world_min = t.world_pos + c->min;
                Vec2f world_max = t.world_pos + c->max;
                Vec2f screen_min = camera_->world_to_screen(world_min);
                Vec2f screen_max = camera_->world_to_screen(world_max);
                Vec2f coll_size = screen_max - screen_min;
                draw_rectangle(screen_min, coll_size, SDL_Color{255, 165, 0, 160}, RectMode::box);
            }
        }
    });
}

void RenderSystem::render_projectiles(Client const &client)
{
    for (auto const &pv : client.projectile_visuals()) {
        Vec2f screen = camera_->world_to_screen(pv.pos);
        float size = 6.F;
        SDL_FRect rect{.x = screen.x - size, .y = screen.y - size, .w = size * 2, .h = size * 2};
        SDL_SetRenderDrawColor(renderer_, 200, 180, 100, 255);
        SDL_RenderFillRect(renderer_, &rect);
    }
}

void RenderSystem::render_damage_numbers(Client &client, std::vector<CombatEvent> const &events)
{
    auto &world = client.world();

    // Spawn a flecs entity per combat event with FloatingText + Transform.
    // A separate flecs system drifts the text upward, fades alpha, and
    // destructs expired entities each frame.
    for (auto const &ev : events) {
        if (ev.damage <= 0)
            continue;

        Vec2f pos{};
        if (ev.defender_id == client.player_id()) {
            pos = client.player_position();
        }
        else {
            auto def = world.entity(ev.defender_id);
            auto const *t = def.is_alive() ? def.try_get<Transform>() : nullptr;
            if (t)
                pos = t->world_pos;
        }

        world.entity()
            .set<FloatingText>(FloatingText{
                .text = std::to_string(ev.damage),
                .lifetime = 0.8F,
                .elapsed = 0.F,
                .velocity = {0.F, -40.F},
                .color = {.r = 1.F, .g = 0.3F, .b = 0.2F, .a = 1.F},
            })
            .set<Transform>(Transform{pos});
    }

    // Draw all active floating texts.
    world.query<FloatingText const, Transform const>().each(
        [this](FloatingText const &ft, Transform const &t) {
            Vec2f screen = camera_->world_to_screen(t.world_pos);
            float a = std::clamp(1.F - (ft.elapsed / ft.lifetime), 0.F, 1.F);
            draw_rectangle({screen.x - 6, screen.y - 6}, {12, 12},
                           SDL_Color{.r = static_cast<uint8_t>(ft.color.r * 255),
                                     .g = static_cast<uint8_t>(ft.color.g * 255),
                                     .b = static_cast<uint8_t>(ft.color.b * 255),
                                     .a = static_cast<uint8_t>(a * 255)},
                           RectMode::fill);
        });
}

void RenderSystem::render_fog_overlay(PlayerVisibility const &vis)
{
    auto vp = camera_->viewport();
    auto left_up = world_to_tile(Vec2f(vp.x, vp.y));
    auto right_down = world_to_tile(Vec2f(vp.x + vp.w, vp.y + vp.h));
    // Render all tiles in viewport range
    for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
        for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
            Vec2i tile(x, y);
            Vec2f screen = camera_->world_to_screen(lu_of_tile(tile));
            auto const zoom = camera_->zoom();
            constexpr auto padding = 2.F; // 防止tile之间有缝隙
            SDL_FRect rect{
                .x = screen.x,
                .y = screen.y,
                .w = (tile_size * zoom) + padding,
                .h = (tile_size * zoom) + padding,
            };

            switch (vis.query(tile)) {
            case TileVisibility::Unexplored:
                SDL_SetRenderDrawColor(renderer_, 8, 8, 14, 255);
                SDL_RenderFillRect(renderer_, &rect);
                break;
            default:
                break;
            }
        }
    }
}

void RenderSystem::draw_rectangle(Vec2f left_up, Vec2f size, SDL_Color color, RectMode mode) const
{
    SDL_FRect rect{.x = left_up.x, .y = left_up.y, .w = size.x, .h = size.y};
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    if (mode == RectMode::fill)
        SDL_RenderFillRect(renderer_, &rect);
    else
        SDL_RenderRect(renderer_, &rect);
}

void RenderSystem::draw_sprite(Vec2f foot_pos, float scale, std::string const &tex, uint8_t alpha,
                               bool flip, Vec2f origin, Vec2f clip_offset, Vec2f clip_size) const
{
    auto *texture = resources_->texture(tex);
    Vec2f screen = camera_->world_to_screen(foot_pos);

    float tex_w, tex_h;
    SDL_GetTextureSize(texture, &tex_w, &tex_h);

    // clip_offset/clip_size are normalized (0..1).  Default (0,0,1,1) = full texture.
    float clip_x = clip_offset.x * tex_w;
    float clip_y = clip_offset.y * tex_h;
    float clip_w = clip_size.x * tex_w;
    float clip_h = clip_size.y * tex_h;

    SDL_FRect srcrect{.x = clip_x, .y = clip_y, .w = clip_w, .h = clip_h};

    // origin is relative to the clip rect
    float ox = origin.x * clip_w * scale;
    float oy = origin.y * clip_h * scale;

    SDL_FRect dst{.x = screen.x - ox, .y = screen.y - oy, .w = clip_w * scale, .h = clip_h * scale};

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, alpha);

    SDL_RenderTextureRotated(renderer_, texture, &srcrect, &dst, 0.0, nullptr,
                             flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

    SDL_SetTextureAlphaMod(texture, max_alpha);
}

void RenderSystem::draw(Sprite const &s, Vec2f pos, uint8_t alpha) const
{
    draw_sprite(pos, s.scale, s.texture_name, alpha, s.flip, s.origin, s.offset, s.size);
}

#include "systems/render-system.hpp"
#include "animation/animation-data.hpp"
#include "core/resource-manager.hpp"
#include "entities/components/building-data.hpp"
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

void RenderSystem::render(Client const &client)
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    render_tile_map();
    render_town(client);
    render_entities(client, client.player_position(), client.player_id());
    render_projectiles(client);
    render_health_bars(client, client.player_position());
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
            std::vector<std::pair<int, int>> const surround{
                {-1, -1},
                {-1, 0},
                {0, -1},
                {0, 0},
            };
            for (auto [i, d] : std::views::enumerate(surround)) {
                auto [ny, nx] = d;
                ny += y;
                nx += x;
                if (map_data_->tile(nx, ny).type != TileType::water)
                    idx += 1 << i;
            }
            draw_sprite(lu, tile_size, "tile_" + std::to_string(idx), max_alpha, false);
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

    // Build a map: tile → building_type from synced structure entities
    // so we can colour each building tile correctly.
    std::unordered_map<Vec2i, uint8_t> building_tiles;
    for (auto const &re : client.remote_entities()) {
        if (re.kind != EntityKind::structure)
            continue;
        Vec2i tile = world_to_tile(re.position);
        building_tiles[tile] = re.building_type;
    }

    for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
        for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
            Vec2i tile(x, y);
            if (!map_data_->in_bounds(x, y))
                continue;

            TileType type = map_data_->tile(x, y).type;
            if (type == TileType::grass || type == TileType::water || type == TileType::mountain)
                continue;

            // Draw overlay for building/road
            Vec2f center = center_of_tile(tile);
            Vec2f screen = camera_->world_to_screen(center);
            auto const zoom = camera_->zoom();
            float const size = tile_size * zoom + 2.F;
            SDL_FRect overlay{
                .x = screen.x - size / 2.F, .y = screen.y - size / 2.F, .w = size, .h = size};

            if (type == TileType::building) {
                // Colour per building type — look up from synced data
                auto it = building_tiles.find(tile);
                uint8_t bt = it != building_tiles.end() ? it->second : 0;
                SDL_Color col;
                char const *label = nullptr;
                using BDT = BuildingData::Type;
                switch (static_cast<BDT>(bt)) {
                case BDT::inn:
                    col = {.r = 76, .g = 179, .b = 76, .a = 240};
                    label = "Inn";
                    break;
                case BDT::market:
                    col = {.r = 76, .g = 125, .b = 204, .a = 240};
                    label = "Market";
                    break;
                case BDT::temple:
                    col = {.r = 229, .g = 178, .b = 51, .a = 240};
                    label = "Temple";
                    break;
                case BDT::blacksmith:
                    col = {.r = 217, .g = 76, .b = 38, .a = 240};
                    label = "Smithy";
                    break;
                default:
                    col = {.r = 130, .g = 90, .b = 50, .a = 240};
                    label = "House";
                    break;
                }
                SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b, col.a);
                SDL_RenderFillRect(renderer_, &overlay);
                // Thin border
                SDL_SetRenderDrawColor(renderer_, 240, 220, 180, 200);
                SDL_RenderRect(renderer_, &overlay);
                // Label
                if (label) {
                    auto lp = center_of_tile(tile);
                    auto ls = camera_->world_to_screen(lp);
                    font_->draw({ls.x, ls.y - tile_size / 2.F},
                                SDL_Color{.r = 255, .g = 255, .b = 255, .a = 255}, label);
                }
            }
            else if (type == TileType::road) {
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

void RenderSystem::render_entities(Client const &client, Vec2f local_player_pos, EntityId eid)
{
    auto const &vis = client.player_visibility();

    // --- 1. Live entities ---
    for (auto const &re : client.remote_entities()) {
        // if (!re.cs.alive || !re.visible)
        //     continue;

        // ---- Non-structure handling ----
        if (re.kind == EntityKind::structure)
            continue;

        // ---- Skip if not explored ----
        auto tile = world_to_tile(re.position);
        auto tv = vis.query(tile);
        if (tv == TileVisibility::Unexplored)
            continue;

        char const *tex = re.visual.current_frame_name().c_str();
        float dist = (re.position - local_player_pos).length();

        draw_sprite(re.position, 48.F * re.visual.scale, tex, alpha_for(dist), re.visual.flip,
                    re.visual.origin);

        // '!' mark
        if (re.id != eid && re.interactable && dist < 48) { // character width
            Vec2f screen = camera_->world_to_screen(re.position);
            float size = 24.F * re.visual.scale;
            SDL_FRect hint{.x = screen.x - 4, .y = screen.y - size - 12, .w = 4, .h = 14};
            SDL_SetRenderDrawColor(renderer_, 255, 255, 100, 220);
            SDL_RenderFillRect(renderer_, &hint);
        }

        if (debug_mode_) {
            // Draw debug text
            auto c = camera_->world_to_screen(re.position);
            font_->draw({c.x, c.y - 24.F}, SDL_Color{.r = 255, .g = 0, .b = 0, .a = 150},
                        std::format("id={}, team={}, text={}", re.id, re.cs.team, tex));

            // Look up actual texture dimensions
            auto *sdl_tex = resources_->texture(tex);
            float tex_w = 32.F;
            float tex_h = 32.F;
            if (sdl_tex) {
                SDL_GetTextureSize(sdl_tex, &tex_w, &tex_h);
            }

            // Compute screen-space origin offset
            float ox = (re.visual.origin.x < 0) ? tex_w * re.visual.scale * 0.5F
                                                : re.visual.origin.x * re.visual.scale;
            float oy = (re.visual.origin.y < 0) ? tex_h * re.visual.scale * 0.5F
                                                : re.visual.origin.y * re.visual.scale;

            // ── Texture bounding box (cyan) ──
            draw_rectangle({c.x - ox, c.y - oy}, {tex_w * re.visual.scale, tex_h * re.visual.scale},
                           SDL_Color{0, 255, 255, 160}, false);

            // ── Collision volume box (orange) ──
            float constexpr coll_ratio = 0.70F; // ~70% of texture size
            float coll_w = tex_w * re.visual.scale * coll_ratio;
            float coll_h = tex_h * re.visual.scale * coll_ratio;
            draw_rectangle({c.x - coll_w * 0.5F, c.y - coll_h * 0.5F}, {coll_w, coll_h},
                           SDL_Color{255, 165, 0, 160}, false);
        }
    }

    // TODO: should be applied afterwards
    // --- 2. Snapshots (frozen memory, always rendered on Explored tiles) ---
    // for (auto &sn : client.snapshots()) {
    //     auto tile = world_to_tile(sn.position);
    //     if (!vis.is_explored(tile))
    //         continue;
    //
    //     auto it = dist.find(tile);
    //     int d = (it != dist.end()) ? it->second : 4;
    //     uint8_t alpha = alpha_for(d);
    //     // Snapshots are additionally dimmed
    //     alpha = static_cast<uint8_t>(alpha * 120 / 255);
    //
    //     draw_sprite(sn.position, sn.scale, sn.texture_name, alpha, false);
    // }
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

void RenderSystem::render_health_bars(Client const &client, Vec2f player_pos)
{
    for (auto const &re : client.remote_entities()) {
        if (!re.cs.alive)
            continue;
        // if
        // (!client.player_visibility().is_visible(world_to_tile(re.position)))
        //     continue;

        // 淡出
        auto alpha = alpha_for((player_pos - re.position).length());

        Vec2f screen = camera_->world_to_screen(re.position);
        float barW = 32.F;
        float barH = 4.F;
        float barY = screen.y - 30.F;
        float barX = screen.x - (barW / 2.F);
        SDL_FRect bg{.x = barX, .y = barY, .w = barW, .h = barH};
        SDL_SetRenderDrawColor(renderer_, 40, 10, 10, alpha);
        SDL_RenderFillRect(renderer_, &bg);

        float ratio = static_cast<float>(re.cs.hp) / static_cast<float>(re.cs.max_hp);
        SDL_FRect fill{.x = barX, .y = barY, .w = barW * ratio, .h = barH};
        bool hostile = is_hostile(re.cs.team, client.player_team());
        SDL_FColor col = // hostile->red otherwise green
            hostile ? SDL_FColor{.r = 0.9F, .g = 0.2F, .b = 0.1F, .a = 1.F}
                    : SDL_FColor{.r = 0.2F, .g = 0.8F, .b = 0.3F, .a = 1.F};
        SDL_SetRenderDrawColor(renderer_, col.r * 255, col.g * 255, col.b * 255, alpha);
        SDL_RenderFillRect(renderer_, &fill);
    }
}

void RenderSystem::render_damage_numbers(Client const &client,
                                         std::vector<CombatEvent> const &events)
{
    for (auto const &ev : events) {
        if (ev.damage <= 0)
            continue;
        FloatingText ft;
        ft.text = std::to_string(ev.damage);

        Vec2f defPos{};
        if (ev.defender_id == client.player_id())
            defPos = client.player_position();
        else
            for (auto const &re : client.remote_entities())
                if (re.id == ev.defender_id) {
                    defPos = re.position;
                    break;
                }

        ft.world_pos = defPos;
        ft.velocity = {0.F, -40.F};
        ft.color = {.r = 1.F, .g = 0.3F, .b = 0.2F, .a = 1.F};
        ft.lifetime = 0.8F;
        floating_texts_.push_back(std::move(ft));
    }

    float dt = 1.F / 60.F;
    for (auto &text : floating_texts_) {
        text.elapsed += dt;
        text.world_pos = text.world_pos + text.velocity * dt;
        text.color.a = 1.F - (text.elapsed / text.lifetime);

        Vec2f screen = camera_->world_to_screen(text.world_pos);
        SDL_FRect rect{.x = screen.x - 6, .y = screen.y - 6, .w = 12, .h = 12};
        SDL_SetRenderDrawColor(renderer_, text.color.r * 255, text.color.g * 255,
                               255 * text.color.b, static_cast<uint8_t>(text.color.a * 255));
        SDL_RenderFillRect(renderer_, &rect);
    }

    std::erase_if(floating_texts_, [](auto const &text) { return text.elapsed >= text.lifetime; });
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

void RenderSystem::draw_rectangle(Vec2f left_up, Vec2f size, SDL_Color color, bool fill) const
{
    SDL_FRect rect{.x = left_up.x, .y = left_up.y, .w = size.x, .h = size.y};
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    if (fill)
        SDL_RenderFillRect(renderer_, &rect);
    else
        SDL_RenderRect(renderer_, &rect);
}

void RenderSystem::draw_sprite(Vec2f foot_pos, float width, std::string const &tex, uint8_t alpha,
                               bool flip, Vec2f origin) const
{
    auto *texture = resources_->texture(tex);
    Vec2f screen = camera_->world_to_screen(foot_pos);
    float original_w{};
    float original_h{};
    SDL_GetTextureSize(texture, &original_w, &original_h);
    auto scale = width / original_w;
    auto height = original_h * scale;

    // origin = {-1,-1} means centre of texture (backward-compatible default)
    if (origin.x < 0)
        origin = {original_w / 2.F, original_h / 2.F};
    float ox = origin.x * scale;
    float oy = origin.y * scale;
    SDL_FRect dst{.x = screen.x - ox, .y = screen.y - oy, .w = width, .h = height};
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, alpha);
    SDL_RenderTextureRotated(renderer_, texture, nullptr, &dst, 0.0, nullptr,
                             flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(texture, max_alpha);
}

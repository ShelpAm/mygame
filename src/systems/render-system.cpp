#include "systems/render-system.hpp"
#include "animation/animation-data.hpp"
#include "core/resource-manager.hpp"
#include "net/client.hpp"
#include "systems/camera-system.hpp"
#include "systems/navigation-system.hpp"
#include "world/map-data.hpp"
#include "world/world-state.hpp"
#include <cmath>
#include <deque>
#include <ranges>
#include <string>
#include <unordered_map>

RenderSystem::RenderSystem(SDL_Renderer *renderer, ResourceManager *resources, CameraSystem *camera,
                           Font *font)
    : renderer_(renderer), resources_(resources), camera_(camera), font_(font)
{
}

void RenderSystem::render(Client const &client, NavigationSystem const &nav)
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    render_tile_map(client.player_visibility(), nav);
    render_entities(client, client.player_position(), client.player_id());
    render_projectiles(client);
    render_health_bars(client, client.player_position());
    render_damage_numbers(client, client.combat_events());
    render_fog_overlay(client.player_visibility());
    render_network_stats(client);
}

void RenderSystem::render_tile_map(PlayerVisibility const &vis, NavigationSystem const &nav) const
{
    auto vp = camera_->viewport();
    auto left_up = world_to_tile(Vec2f(vp.x, vp.y));
    auto right_down = world_to_tile(Vec2f(vp.x + vp.w, vp.y + vp.h));

    // Dual-grid tile
    for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
        for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
            auto tile = Vec2i(x, y);
            auto lu = lu_of_tile(tile);
            int idx{};
            // dy, dx, w
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
                if (nav.is_walkable({nx, ny}))
                    idx += 1 << i;
            }
            auto s = std::to_string(idx);

            draw_sprite(lu, tile_size, "tile_" + s, max_alpha, false);
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
    // for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
    //     for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
    //         Vec2i tile(x, y);
    //         Vec2f screen = camera_.world_to_screen(lu_of_tile(tile));

    // Render all tiles in viewport range, layer 2
    // for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
    //     for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
    //         Vec2i tile(x, y);
    //         Vec2f screen = camera_.world_to_screen(lu_of_tile(tile));
    //         auto const zoom = camera_.zoom();
    //         // constexpr auto padding = 2.F; // 防止tile之间有缝隙
    //         SDL_FRect rect{
    //             .x = screen.x,
    //             .y = screen.y,
    //             .w = tile_size * zoom,
    //             .h = tile_size * zoom,
    //         };
    //
    //         bool blocked = !nav.is_walkable(tile);
    //         switch (vis.query(tile)) {
    //         case TileVisibility::Unexplored:
    //             SDL_SetRenderDrawColor(renderer_, 8, 8, 14, max_alpha);
    //             SDL_RenderFillRect(renderer_, &rect);
    //             break;
    //         case TileVisibility::Explored:
    //             // Parchment map — dimmer
    //             if (blocked)
    //                 SDL_SetRenderDrawColor(renderer_, 155, 140, 110,
    //                 max_alpha);
    //             else
    //                 SDL_SetRenderDrawColor(renderer_, 180, 170, 145, 255);
    //             SDL_RenderFillRect(renderer_, &rect);
    //             SDL_SetRenderDrawColor(renderer_, 130, 115, 90, 255);
    //             SDL_RenderRect(renderer_, &rect);
    //             break;
    //         case TileVisibility::Visible:
    //             // Parchment map — brighter, with grid
    //             if (blocked)
    //                 SDL_SetRenderDrawColor(renderer_, 185, 165, 130, 255);
    //             else
    //                 SDL_SetRenderDrawColor(renderer_, 210, 200, 170, 255);
    //             SDL_RenderFillRect(renderer_, &rect);
    //             SDL_SetRenderDrawColor(renderer_, 155, 140, 110, 255);
    //             SDL_RenderRect(renderer_, &rect);
    //             break;
    //         }
    //     }
    // }
}

void RenderSystem::render_entities(Client const &client, Vec2f pos, EntityId eid)
{
    auto const &vis = client.player_visibility();

    // --- 1. Live entities ---
    for (auto const &re : client.remote_entities()) {
        // if (!re.alive || !re.visible)
        //     continue;

        auto tile = world_to_tile(re.position);
        auto tv = vis.query(tile);
        if (tv == TileVisibility::Unexplored)
            continue;

        float d = (re.position - pos).length();

        char const *tex{};
        if (re.alive) {
            if (re.anim_state.clip)
                tex = re.anim_state.clip->frame_names.at(re.anim_state.frame_index).c_str();
            else
                tex = re.texture_name.c_str();
        }
        else {
            tex = "entity_dead";
        }
        draw_sprite(re.position, 48.F * re.scale, tex, alpha_for(d), re.anim_state.flip);

        // '!' mark (only when clearly visible)
        if (d == 0 && re.id != eid && re.interactable) {

            Vec2f screen = camera_->world_to_screen(re.position);
            float size = 24.F * re.scale;
            float dist2 = std::hypot(re.position.x - pos.x, re.position.y - pos.y);
            if (dist2 < tile_size) {
                SDL_FRect hint{.x = screen.x - 4, .y = screen.y - size - 12, .w = 4, .h = 14};
                SDL_SetRenderDrawColor(renderer_, 255, 255, 100, 220);
                SDL_RenderFillRect(renderer_, &hint);
            }
        }

        if (debug_mode_) {
            // Draw debug text
            SDL_SetRenderDrawColor(renderer_, 255, 0, 0, 100);
            // Grid lines
            auto c = camera_->world_to_screen(re.position);
            font_->draw({c.x, c.y}, SDL_Color{.r = 255, .g = 0, .b = 0, .a = 150},
                        std::format("id={}, team={}, text={}", re.id, re.team, tex));
        }
    }

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
        if (!re.alive)
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

        float ratio = static_cast<float>(re.hp) / static_cast<float>(re.max_hp);
        SDL_FRect fill{.x = barX, .y = barY, .w = barW * ratio, .h = barH};
        bool hostile = is_hostile(re.team, client.player_team());
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

void RenderSystem::draw_sprite(Vec2f center, float width, std::string const &tex, uint8_t alpha,
                               bool flip) const
{
    auto *texture = resources_->texture(tex);
    Vec2f screen = camera_->world_to_screen(center);
    float original_w{};
    float original_h{};
    SDL_GetTextureSize(texture, &original_w, &original_h);
    auto scale = width / original_w;
    auto height = original_h * scale;
    SDL_FRect dst{
        .x = screen.x - (width / 2), .y = screen.y - (height / 2), .w = width, .h = height};
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, alpha);
    SDL_RenderTextureRotated(renderer_, texture, nullptr, &dst, 0.0, nullptr,
                             flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(texture, max_alpha);
}

void RenderSystem::render_network_stats(Client const &client)
{
    // If the client hasn't connected yet, skip
    if (client.player_id() == invalid_entity)
        return;

    int32_t rtt = static_cast<int32_t>(client.rtt_ms());
    int32_t age = static_cast<int32_t>(client.last_sync_age());

    // Green when healthy, yellow/orange/red as lag increases
    auto color = SDL_Color{100, 255, 100, 230};
    if (age > 100)
        color = SDL_Color{255, 255, 100, 230};
    if (age > 300)
        color = SDL_Color{255, 200, 50, 230};
    if (age > 1000)
        color = SDL_Color{255, 80, 80, 230};

    font_->draw({10.F, 10.F}, color,
                std::format("RTT: {}ms  (last sync: {}ms ago)", rtt, age));
}

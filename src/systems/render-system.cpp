#include "systems/render-system.hpp"
#include "animation/animation-data.hpp"
#include "core/resource-manager.hpp"
#include "net/client.hpp"
#include "systems/camera-system.hpp"
#include "systems/navigation-system.hpp"
#include "world/map-data.hpp"
#include "world/world-state.hpp"
#include <cmath>
#include <string>

RenderSystem::RenderSystem(SDL_Renderer *renderer, ResourceManager &resources,
                           CameraSystem &camera)
    : renderer_(renderer), resources_(resources), camera_(camera)
{
}

void RenderSystem::render(Client &client, NavigationSystem const &nav)
{
    render_tile_map(client.player_visibility(), nav);
    render_entities(client, client.player_position(), client.local_player());
    render_projectiles(client);
    render_health_bars(client);
    render_damage_numbers(client, client.combat_events());
}

void RenderSystem::render_tile_map(PlayerVisibility const &vis,
                                   NavigationSystem const &nav)
{
    auto vp = camera_.viewport();
    auto left_up = world_to_tile(Vec2f(vp.x, vp.y));
    auto right_down = world_to_tile(Vec2f(vp.x + vp.w, vp.y + vp.h));
    // Render all tiles in viewport range
    for (int y = left_up.y - 1; y <= right_down.y + 1; ++y) {
        for (int x = left_up.x - 1; x <= right_down.x + 1; ++x) {
            Vec2i tile(x, y);
            Vec2f screen = camera_.world_to_screen(Vec2f(x, y) * tile_size);
            constexpr auto padding = 2.F;
            SDL_FRect rect{.x = screen.x,
                           .y = screen.y,
                           .w = tile_size + padding,
                           .h = tile_size + padding};

            bool blocked = !nav.is_walkable(tile);
            switch (vis.query(tile)) {
            case TileVisibility::Unexplored:
                SDL_SetRenderDrawColor(renderer_, 8, 8, 14, 255);
                SDL_RenderFillRect(renderer_, &rect);
                break;
            case TileVisibility::Explored:
                if (blocked)
                    SDL_SetRenderDrawColor(renderer_, 60, 50, 40, 255);
                else
                    SDL_SetRenderDrawColor(renderer_, 40, 40, 50, 255);
                SDL_RenderFillRect(renderer_, &rect);
                break;
            case TileVisibility::Visible:
                if (blocked)
                    SDL_SetRenderDrawColor(renderer_, 60, 50, 40, 255);
                else
                    SDL_SetRenderDrawColor(renderer_, 40, 40, 50, 255);
                SDL_RenderFillRect(renderer_, &rect);
                SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
                SDL_RenderRect(renderer_, &rect);
                break;
            }
        }
    }
}

void RenderSystem::render_entities(Client &client, Vec2f player_pos,
                                   EntityId player_id)
{
    for (auto &re : client.remote_entities()) {
        if (!re.alive || !re.visible)
            continue;

        Vec2f screen = camera_.world_to_screen(re.position);
        float size = 24.f * re.scale;

        SDL_FRect rect{screen.x - size, screen.y - size, size * 2, size * 2};

        SDL_FRect dst = rect;
        char const *frame_name =
            re.anim_state.clip
                ? re.anim_state.clip->frame_names[re.anim_state.frame_index]
                      .c_str()
                : re.texture_name;
        auto texture = resources_.texture(frame_name);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART);

        if (re.snapshot) {
            SDL_SetTextureColorMod(texture, 60, 60, 70);
            SDL_SetTextureAlphaMod(texture, 120);
        }

        SDL_RenderTextureRotated(renderer_, texture, NULL, &dst, 0.0, NULL,
                                 re.anim_state.flip ? SDL_FLIP_HORIZONTAL
                                                    : SDL_FLIP_NONE);

        if (re.snapshot) {
            SDL_SetTextureColorMod(texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(texture, 255);
        }

        // '!' mark
        if (!re.snapshot && re.id != player_id && re.interactable) {
            float dist = std::hypot(re.position.x - player_pos.x,
                                    re.position.y - player_pos.y);
            if (dist < tile_size) {
                SDL_FRect hint{screen.x - 4, screen.y - size - 12, 4, 14};
                SDL_SetRenderDrawColor(renderer_, 255, 255, 100, 220);
                SDL_RenderFillRect(renderer_, &hint);
            }
        }
    }
}

void RenderSystem::render_projectiles(Client &client)
{
    for (auto &pv : client.projectile_visuals()) {
        Vec2f screen = camera_.world_to_screen(pv.pos);
        float size = 6.f;
        SDL_FRect rect{screen.x - size, screen.y - size, size * 2, size * 2};
        SDL_SetRenderDrawColor(renderer_, 200, 180, 100, 255);
        SDL_RenderFillRect(renderer_, &rect);
    }
}

void RenderSystem::render_health_bars(Client &client)
{
    for (auto &re : client.remote_entities()) {
        if (!re.alive || re.snapshot)
            continue;

        Vec2f screen = camera_.world_to_screen(re.position);
        float barW = 32.f, barH = 4.f;
        float barY = screen.y - 30.f;
        float barX = screen.x - barW / 2.f;

        SDL_FRect bg{barX, barY, barW, barH};
        SDL_SetRenderDrawColor(renderer_, 40, 10, 10, 255);
        SDL_RenderFillRect(renderer_, &bg);

        float ratio = (float)re.hp / (float)re.max_hp;
        SDL_FRect fill{barX, barY, barW * ratio, barH};
        bool hostile = is_hostile(re.team, client.player_team());
        SDL_FColor col = hostile ? SDL_FColor{0.9f, 0.2f, 0.1f, 1.f}  // red
                                 : SDL_FColor{0.2f, 0.8f, 0.3f, 1.f}; // green
        SDL_SetRenderDrawColor(renderer_, col.r * 255, col.g * 255, col.b * 255,
                               255);
        SDL_RenderFillRect(renderer_, &fill);
    }
}

void RenderSystem::render_damage_numbers(Client &client,
                                         std::vector<CombatEvent> const &events)
{
    for (auto &ev : events) {
        if (ev.damage <= 0)
            continue;
        FloatingText ft;
        ft.text = std::to_string(ev.damage);

        Vec2f defPos;
        if (ev.defender_id == client.player_id())
            defPos = client.player_position();
        else
            for (auto &re : client.remote_entities())
                if (re.id == ev.defender_id) {
                    defPos = re.position;
                    break;
                }

        ft.world_pos = defPos;
        ft.velocity = {0.f, -40.f};
        ft.color = {1.f, 0.3f, 0.2f, 1.f};
        ft.lifetime = 0.8f;
        floating_texts_.push_back(std::move(ft));
    }

    float dt = 1.f / 60.f;
    for (auto it = floating_texts_.begin(); it != floating_texts_.end();) {
        it->elapsed += dt;
        if (it->elapsed >= it->lifetime) {
            it = floating_texts_.erase(it);
            continue;
        }
        it->world_pos = it->world_pos + it->velocity * dt;
        float alpha = 1.f - (it->elapsed / it->lifetime);
        it->color.a = alpha;
        Vec2f screen = camera_.world_to_screen(it->world_pos);
        SDL_FRect rect{screen.x - 6, screen.y - 6, 12, 12};
        SDL_SetRenderDrawColor(renderer_, it->color.r * 255, it->color.g * 255,
                               it->color.b * 255,
                               static_cast<uint8_t>(alpha * 255));
        SDL_RenderFillRect(renderer_, &rect);
        ++it;
    }
}

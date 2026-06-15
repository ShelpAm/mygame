#include "systems/render-system.hpp"
#include "core/resource-manager.hpp"
#include "net/client.hpp"
#include "systems/camera-system.hpp"
#include "systems/navigation-system.hpp"
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
    render_tile_map(client.world_state(), nav);
    render_entities(client, client.player_position(), client.local_player());
    render_health_bars(client);
    render_damage_numbers(client, client.combat_events());
}

void RenderSystem::render_tile_map(WorldState const &world_state,
                                   NavigationSystem const &nav)
{
    auto vp = camera_.viewport();
    int startX = static_cast<int>(vp.x / 64.f) - 1;
    int startY = static_cast<int>(vp.y / 64.f) - 1;
    int endX = startX + static_cast<int>(vp.w / 64.f) + 2;
    int endY = startY + static_cast<int>(vp.h / 64.f) + 2;

    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            Vec2i tile{x, y};
            bool seen = world_state.is_tile_seen(tile);
            Vec2f screen = camera_.world_to_screen({x * 64.f, y * 64.f});
            SDL_FRect rect{screen.x, screen.y, 66.f, 66.f};

            bool blocked = !nav.is_walkable(tile);
            if (!seen)
                SDL_SetRenderDrawColor(renderer_, 5, 5, 10, 255);
            else if (blocked)
                SDL_SetRenderDrawColor(renderer_, 60, 50, 40, 255);
            else
                SDL_SetRenderDrawColor(renderer_, 40, 40, 50, 255);
            SDL_RenderFillRect(renderer_, &rect);

            if (seen) {
                SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
                SDL_RenderRect(renderer_, &rect);
            }
        }
    }
}

void RenderSystem::render_entities(Client &client, Vec2f player_pos,
                                   EntityId player_id)
{
    for (auto &re : client.remote_entities()) {
        if (!re.alive)
            continue;

        Vec2f screen = camera_.world_to_screen(re.position);
        float size = 24.f * re.scale;

        SDL_FRect rect{screen.x - size, screen.y - size, size * 2, size * 2};
        // Main rect
        // SDL_FColor color =
        //     re.hit_flash ? SDL_FColor{1.f, 1.f, 1.f, 1.f} : re.color;
        // SDL_SetRenderDrawColor(renderer_, color.r * 255, color.g * 255,
        //                        color.b * 255, color.a * 255);
        // SDL_RenderFillRect(renderer_, &rect);

        /* Display the image */
        SDL_FRect dst = rect;
        auto texture = resources_.texture("entity");
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART);
        SDL_RenderTexture(renderer_, texture, NULL, &dst);

        // resources_.camera().update(renderer_);
        // resources_.camera().render(renderer_, &dst);

        // '!' mark
        if (re.id != player_id && re.interactable) {
            float dist = std::hypot(re.position.x - player_pos.x,
                                    re.position.y - player_pos.y);
            if (dist < 64.f) {
                SDL_FRect hint{screen.x - 4, screen.y - size - 14, 10, 14};
                SDL_SetRenderDrawColor(renderer_, 255, 255, 100, 220);
                SDL_RenderFillRect(renderer_, &hint);
            }
        }
    }
}

void RenderSystem::render_health_bars(Client &client)
{
    for (auto &re : client.remote_entities()) {
        if (!re.alive)
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

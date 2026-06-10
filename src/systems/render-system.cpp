#include "systems/render-system.hpp"
#include "core/resource-manager.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/position.hpp"
#include "entities/components/sprite.hpp"
#include "entities/entity-manager.hpp"
#include "systems/camera-system.hpp"
#include "systems/combat-system.hpp"
#include "systems/navigation-system.hpp"
#include "world/world-state.hpp"
#include <cstdio>

RenderSystem::RenderSystem(SDL_Renderer *renderer, ResourceManager &resources,
                           CameraSystem &camera)
    : renderer_(renderer), resources_(resources), camera_(camera)
{
}

void RenderSystem::render(EntityManager &entities,
                          WorldState const &world_state,
                          NavigationSystem const &nav,
                          std::vector<CombatEvent> const &combat_events)
{
    render_tile_map(world_state, nav);
    render_entities(entities);
    render_health_bars(entities);
    render_damage_numbers(combat_events);
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
            if (!seen) {
                SDL_SetRenderDrawColor(renderer_, 5, 5, 10, 255);
            }
            else if (blocked) {
                SDL_SetRenderDrawColor(renderer_, 60, 50, 40, 255);
            }
            else {
                SDL_SetRenderDrawColor(renderer_, 40, 40, 50, 255);
            }
            SDL_RenderFillRect(renderer_, &rect);

            if (seen) {
                SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
                SDL_RenderRect(renderer_, &rect);
            }
        }
    }
}

void RenderSystem::render_entities(EntityManager &entities)
{
    for (auto id : entities.all_entities()) {
        auto *pos = entities.get_component<Position>(id);
        auto *spr = entities.get_component<Sprite>(id);
        auto *cs = entities.get_component<CombatStats>(id);
        if (!pos || !spr || !spr->visible)
            continue;
        if (cs && !cs->alive)
            continue; // Don't render dead

        Vec2f screen = camera_.world_to_screen(pos->world_pos);
        float size = 14.f * spr->scale;

        // Hit flash
        auto *flash = entities.get_component<HitFlash>(id);
        SDL_FColor col = spr->color;
        if (flash && flash->remaining > 0.f) {
            col = {1.f, 1.f, 1.f, 1.f}; // White flash on hit
        }

        SDL_FRect rect{screen.x - size, screen.y - size, size * 2, size * 2};
        SDL_SetRenderDrawColor(renderer_, col.r * 255, col.g * 255, col.b * 255,
                               col.a * 255);
        SDL_RenderFillRect(renderer_, &rect);

        // Outline for enemies
        if (cs && cs->team == Team::enemy) {
            SDL_SetRenderDrawColor(renderer_, 180, 40, 40, 200);
            SDL_RenderRect(renderer_, &rect);
        }
    }
}

void RenderSystem::render_health_bars(EntityManager &entities)
{
    for (auto id : entities.all_entities()) {
        auto *pos = entities.get_component<Position>(id);
        auto *cs = entities.get_component<CombatStats>(id);
        if (!pos || !cs || !cs->alive)
            continue;

        Vec2f screen = camera_.world_to_screen(pos->world_pos);
        float barW = 30.f;
        float barH = 4.f;
        float barY = screen.y - 22.f;
        float barX = screen.x - barW / 2.f;

        // Background (dark red)
        SDL_FRect bg{barX, barY, barW, barH};
        SDL_SetRenderDrawColor(renderer_, 40, 10, 10, 255);
        SDL_RenderFillRect(renderer_, &bg);

        // Health fill
        float ratio = (float)cs->hp / (float)cs->max_hp;
        SDL_FRect fill{barX, barY, barW * ratio, barH};
        SDL_FColor col = cs->team == Team::player
                             ? SDL_FColor{0.2f, 0.8f, 0.3f, 1.f}
                             : SDL_FColor{0.9f, 0.2f, 0.1f, 1.f};
        SDL_SetRenderDrawColor(renderer_, col.r * 255, col.g * 255, col.b * 255,
                               255);
        SDL_RenderFillRect(renderer_, &fill);
    }
}

void RenderSystem::render_damage_numbers(std::vector<CombatEvent> const &events)
{
    char buf[32];
    for (auto &ev : events) {
        if (ev.damage <= 0)
            continue;
        std::snprintf(buf, sizeof(buf), "-%d", ev.damage);
        FloatingText ft;
        ft.text = buf;
        ft.world_pos = {0, 0}; // Will be positioned per defender
        ft.color = {1.f, 0.3f, 0.2f, 1.f};
        ft.lifetime = 0.8f;
        floating_texts_.push_back(ft);
    }

    // Update and render existing texts
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
        ++it;
    }
}

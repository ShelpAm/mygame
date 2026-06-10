#include "systems/RenderSystem.hpp"
#include "systems/CombatSystem.hpp"
#include "net/NetworkManager.hpp"
#include "core/ResourceManager.hpp"
#include "systems/CameraSystem.hpp"
#include "systems/NavigationSystem.hpp"
#include "entities/EntityManager.hpp"
#include "entities/components/Position.hpp"
#include "entities/components/Sprite.hpp"
#include "entities/components/CombatStats.hpp"
#include "world/WorldState.hpp"
#include <cstdio>

RenderSystem::RenderSystem(SDL_Renderer* renderer, ResourceManager& resources, CameraSystem& camera)
    : m_renderer(renderer), m_resources(resources), m_camera(camera)
{}

void RenderSystem::render(EntityManager& entities, const WorldState& worldState,
                           const NavigationSystem& nav,
                           const std::vector<CombatEvent>& combatEvents,
                           const std::vector<RemoteEntity>& remotePlayers) {
    renderTileMap(worldState, nav);
    renderEntities(entities);
    renderHealthBars(entities);
    renderDamageNumbers(combatEvents);
    // Render remote entities
    for (const auto& rp : remotePlayers) {
        if (!rp.alive) continue;
        Vec2f screen = m_camera.worldToScreen(rp.position);
        // Color by team
        uint8_t r, g, b;
        if (rp.team == 1) { r = 220; g = 40; b = 40; }       // Enemy - red
        else if (rp.team == 2) { r = 150; g = 150; b = 150; } // Neutral - gray
        else { r = 50; g = 150; b = 255; }                     // Player/friendly - blue
        SDL_FRect rect{screen.x - 12.f, screen.y - 12.f, 24.f, 24.f};
        SDL_SetRenderDrawColor(m_renderer, r, g, b, 255);
        SDL_RenderFillRect(m_renderer, &rect);
        // HP bar
        float barW = 24.f;
        float ratio = (float)rp.hp / (float)rp.maxHp;
        SDL_FRect bg{screen.x - barW/2, screen.y - 20.f, barW, 3.f};
        SDL_SetRenderDrawColor(m_renderer, 40, 10, 10, 255);
        SDL_RenderFillRect(m_renderer, &bg);
        SDL_FRect fill{screen.x - barW/2, screen.y - 20.f, barW * ratio, 3.f};
        SDL_SetRenderDrawColor(m_renderer, 50, 200, 50, 255);
        SDL_RenderFillRect(m_renderer, &fill);
    }
}

void RenderSystem::renderTileMap(const WorldState& worldState, const NavigationSystem& nav) {
    auto vp = m_camera.viewport();
    int startX = static_cast<int>(vp.x / 64.f) - 1;
    int startY = static_cast<int>(vp.y / 64.f) - 1;
    int endX = startX + static_cast<int>(vp.w / 64.f) + 2;
    int endY = startY + static_cast<int>(vp.h / 64.f) + 2;

    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            Vec2i tile{x, y};
            bool seen = worldState.isTileSeen(tile);
            Vec2f screen = m_camera.worldToScreen({x * 64.f, y * 64.f});
            SDL_FRect rect{screen.x, screen.y, 66.f, 66.f};

            bool blocked = !nav.isWalkable(tile);
            if (!seen) {
                SDL_SetRenderDrawColor(m_renderer, 5, 5, 10, 255);
            } else if (blocked) {
                SDL_SetRenderDrawColor(m_renderer, 60, 50, 40, 255);
            } else {
                SDL_SetRenderDrawColor(m_renderer, 40, 40, 50, 255);
            }
            SDL_RenderFillRect(m_renderer, &rect);

            if (seen) {
                SDL_SetRenderDrawColor(m_renderer, 50, 50, 60, 255);
                SDL_RenderRect(m_renderer, &rect);
            }
        }
    }
}

void RenderSystem::renderEntities(EntityManager& entities) {
    for (auto id : entities.allEntities()) {
        auto* pos = entities.getComponent<Position>(id);
        auto* spr = entities.getComponent<Sprite>(id);
        auto* cs = entities.getComponent<CombatStats>(id);
        if (!pos || !spr || !spr->visible) continue;
        if (cs && !cs->alive) continue;  // Don't render dead

        Vec2f screen = m_camera.worldToScreen(pos->worldPos);
        float size = 14.f * spr->scale;

        // Hit flash
        auto* flash = entities.getComponent<HitFlash>(id);
        SDL_FColor col = spr->color;
        if (flash && flash->remaining > 0.f) {
            col = {1.f, 1.f, 1.f, 1.f};  // White flash on hit
        }

        SDL_FRect rect{screen.x - size, screen.y - size, size * 2, size * 2};
        SDL_SetRenderDrawColor(m_renderer, col.r * 255, col.g * 255, col.b * 255, col.a * 255);
        SDL_RenderFillRect(m_renderer, &rect);

        // Outline for enemies
        if (cs && cs->team == Team::Enemy) {
            SDL_SetRenderDrawColor(m_renderer, 180, 40, 40, 200);
            SDL_RenderRect(m_renderer, &rect);
        }
    }
}

void RenderSystem::renderHealthBars(EntityManager& entities) {
    for (auto id : entities.allEntities()) {
        auto* pos = entities.getComponent<Position>(id);
        auto* cs = entities.getComponent<CombatStats>(id);
        if (!pos || !cs || !cs->alive) continue;

        Vec2f screen = m_camera.worldToScreen(pos->worldPos);
        float barW = 30.f;
        float barH = 4.f;
        float barY = screen.y - 22.f;
        float barX = screen.x - barW / 2.f;

        // Background (dark red)
        SDL_FRect bg{barX, barY, barW, barH};
        SDL_SetRenderDrawColor(m_renderer, 40, 10, 10, 255);
        SDL_RenderFillRect(m_renderer, &bg);

        // Health fill
        float ratio = (float)cs->hp / (float)cs->maxHp;
        SDL_FRect fill{barX, barY, barW * ratio, barH};
        SDL_FColor col = cs->team == Team::Player
            ? SDL_FColor{0.2f, 0.8f, 0.3f, 1.f}
            : SDL_FColor{0.9f, 0.2f, 0.1f, 1.f};
        SDL_SetRenderDrawColor(m_renderer, col.r * 255, col.g * 255, col.b * 255, 255);
        SDL_RenderFillRect(m_renderer, &fill);
    }
}

void RenderSystem::renderDamageNumbers(const std::vector<CombatEvent>& events) {
    char buf[32];
    for (auto& ev : events) {
        if (ev.damage <= 0) continue;
        std::snprintf(buf, sizeof(buf), "-%d", ev.damage);
        FloatingText ft;
        ft.text = buf;
        ft.worldPos = {0, 0};  // Will be positioned per defender
        ft.color = {1.f, 0.3f, 0.2f, 1.f};
        ft.lifetime = 0.8f;
        m_floatingTexts.push_back(ft);
    }

    // Update and render existing texts
    float dt = 1.f / 60.f;
    for (auto it = m_floatingTexts.begin(); it != m_floatingTexts.end();) {
        it->elapsed += dt;
        if (it->elapsed >= it->lifetime) {
            it = m_floatingTexts.erase(it);
            continue;
        }
        it->worldPos = it->worldPos + it->velocity * dt;
        float alpha = 1.f - (it->elapsed / it->lifetime);
        it->color.a = alpha;
        ++it;
    }
}

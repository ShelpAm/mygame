#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include "entities/components/VisualFX.hpp"

class ResourceManager;
class CameraSystem;
class EntityManager;
class WorldState;
class NavigationSystem;

struct CombatEvent;
struct RemoteEntity;

class RenderSystem {
public:
    RenderSystem(SDL_Renderer* renderer, ResourceManager& resources, CameraSystem& camera);

    void render(EntityManager& entities, const WorldState& worldState,
                const NavigationSystem& nav,
                const std::vector<CombatEvent>& combatEvents,
                const std::vector<RemoteEntity>& remotePlayers);

private:
    SDL_Renderer* m_renderer;
    ResourceManager& m_resources;
    CameraSystem& m_camera;

    void renderTileMap(const WorldState& worldState, const NavigationSystem& nav);
    void renderEntities(EntityManager& entities);
    void renderHealthBars(EntityManager& entities);
    void renderDamageNumbers(const std::vector<CombatEvent>& events);

    std::vector<FloatingText> m_floatingTexts;
};

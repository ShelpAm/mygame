#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include "entities/components/visual-fx.hpp"
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

    void render(EntityManager& entities, const WorldState& world_state,
                const NavigationSystem& nav,
                const std::vector<CombatEvent>& combat_events,
                const std::vector<RemoteEntity>& remote_players);

private:
    SDL_Renderer* renderer_;
    ResourceManager& resources_;
    CameraSystem& camera_;

    void render_tile_map(const WorldState& world_state, const NavigationSystem& nav);
    void render_entities(EntityManager& entities);
    void render_health_bars(EntityManager& entities);
    void render_damage_numbers(const std::vector<CombatEvent>& events);

    std::vector<FloatingText> floating_texts_;
};

#include "entities/entity-manager.hpp"
#include <algorithm>

EntityId EntityManager::create_entity()
{
    EntityId id = next_id_++;

    if (id == invalid_entity) {
        throw std::runtime_error("Entity ID overflow");
    }

    alive_.push_back(id);
    return id;
}

void EntityManager::destroy_entity(EntityId id)
{
    std::erase(alive_, id);
}

bool EntityManager::alive(EntityId id) const
{
    return std::ranges::find(alive_, id) != alive_.end();
}

std::vector<EntityId> EntityManager::all_entities() const
{
    return alive_;
}

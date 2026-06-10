#include "entities/EntityManager.hpp"
#include <algorithm>

EntityId EntityManager::createEntity() {
    EntityId id = m_nextId++;
    m_alive.push_back(id);
    return id;
}

void EntityManager::destroyEntity(EntityId id) {
    std::erase(m_alive, id);
}

bool EntityManager::alive(EntityId id) const {
    return std::ranges::find(m_alive, id) != m_alive.end();
}

std::vector<EntityId> EntityManager::allEntities() const {
    return m_alive;
}

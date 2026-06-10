#pragma once

#include "entities/components/Position.hpp"
#include "entities/components/Sprite.hpp"
#include "entities/components/Movement.hpp"
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <memory>

class EntityManager {
public:
    EntityId createEntity();
    void destroyEntity(EntityId id);
    bool alive(EntityId id) const;

    template <typename T>
    T& addComponent(EntityId id, T component);

    template <typename T>
    void removeComponent(EntityId id);

    template <typename T>
    T* getComponent(EntityId id);

    template <typename T>
    bool hasComponent(EntityId id) const;

    std::vector<EntityId> allEntities() const;

private:
    EntityId m_nextId = 1;
    std::vector<EntityId> m_alive;

    template <typename T>
    struct ComponentPool {
        std::unordered_map<EntityId, T> data;
    };

    struct PoolDeleter {
        template<typename T>
        static void deletePool(void* p) { delete static_cast<ComponentPool<T>*>(p); }

        void (*fn)(void*) = nullptr;
        void operator()(void* p) const { if (fn) fn(p); }
    };
    std::unordered_map<std::type_index, std::unique_ptr<void, PoolDeleter>> m_pools;

    template <typename T>
    ComponentPool<T>& pool();
};

template <typename T>
EntityManager::ComponentPool<T>& EntityManager::pool() {
    auto ti = std::type_index(typeid(T));
    auto it = m_pools.find(ti);
    if (it == m_pools.end()) {
        auto* raw = new ComponentPool<T>();
        m_pools[ti] = std::unique_ptr<void, PoolDeleter>(raw, PoolDeleter{PoolDeleter::deletePool<T>});
        return *raw;
    }
    return *static_cast<ComponentPool<T>*>(it->second.get());
}

template <typename T>
T& EntityManager::addComponent(EntityId id, T component) {
    auto& p = pool<T>();
    p.data[id] = std::move(component);
    return p.data[id];
}

template <typename T>
void EntityManager::removeComponent(EntityId id) {
    auto& p = pool<T>();
    p.data.erase(id);
}

template <typename T>
T* EntityManager::getComponent(EntityId id) {
    auto& p = pool<T>();
    auto it = p.data.find(id);
    return it != p.data.end() ? &it->second : nullptr;
}

template <typename T>
bool EntityManager::hasComponent(EntityId id) const {
    auto ti = std::type_index(typeid(T));
    auto it = m_pools.find(ti);
    if (it == m_pools.end()) return false;
    auto& p = *static_cast<const ComponentPool<T>*>(it->second.get());
    return p.data.contains(id);
}

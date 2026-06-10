#pragma once

#include "entities/components/movement.hpp"
#include "entities/components/position.hpp"
#include "entities/components/sprite.hpp"
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

class EntityManager {
  public:
    EntityId create_entity();
    void destroy_entity(EntityId id);
    bool alive(EntityId id) const;

    template <typename T> T &add_component(EntityId id, T component);

    template <typename T> void remove_component(EntityId id);

    template <typename T> T *get_component(EntityId id);

    template <typename T> bool has_component(EntityId id) const;

    std::vector<EntityId> all_entities() const;

  private:
    EntityId next_id_ = 1;
    std::vector<EntityId> alive_;

    template <typename T> struct ComponentPool {
        std::unordered_map<EntityId, T> data;
    };

    struct PoolDeleter {
        template <typename T> static void delete_pool(void *p)
        {
            delete static_cast<ComponentPool<T> *>(p);
        }

        void (*fn)(void *) = nullptr;
        void operator()(void *p) const
        {
            if (fn)
                fn(p);
        }
    };
    std::unordered_map<std::type_index, std::unique_ptr<void, PoolDeleter>>
        pools_;

    template <typename T> ComponentPool<T> &pool();
};

template <typename T> EntityManager::ComponentPool<T> &EntityManager::pool()
{
    auto ti = std::type_index(typeid(T));
    auto it = pools_.find(ti);
    if (it == pools_.end()) {
        auto *raw = new ComponentPool<T>();
        pools_[ti] = std::unique_ptr<void, PoolDeleter>(
            raw, PoolDeleter{PoolDeleter::delete_pool<T>});
        return *raw;
    }
    return *static_cast<ComponentPool<T> *>(it->second.get());
}

template <typename T> T &EntityManager::add_component(EntityId id, T component)
{
    auto &p = pool<T>();
    p.data[id] = std::move(component);
    return p.data[id];
}

template <typename T> void EntityManager::remove_component(EntityId id)
{
    auto &p = pool<T>();
    p.data.erase(id);
}

template <typename T> T *EntityManager::get_component(EntityId id)
{
    auto &p = pool<T>();
    auto it = p.data.find(id);
    return it != p.data.end() ? &it->second : nullptr;
}

template <typename T> bool EntityManager::has_component(EntityId id) const
{
    auto ti = std::type_index(typeid(T));
    auto it = pools_.find(ti);
    if (it == pools_.end())
        return false;
    auto &p = *static_cast<ComponentPool<T> const *>(it->second.get());
    return p.data.contains(id);
}

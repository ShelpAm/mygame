#pragma once

#include "core/game-types.hpp"
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

class EntityManager {
  public:
    EntityId create_entity();
    EntityId create_entity(EntityId id); // register existing Flecs ID
    void destroy_entity(EntityId id);
    bool alive(EntityId id) const;

    template <typename T> T &add_component(EntityId id, T component);

    template <typename T> void remove_component(EntityId id);

    template <typename T> T *get_component(EntityId id);

    template <typename T> bool has_component(EntityId id) const;

    std::vector<EntityId> all_entities() const;

    template <typename... Ts> std::vector<EntityId> view() const;

  private:
    EntityId next_id_ = invalid_entity + 1;
    std::vector<EntityId> alive_;

    struct IComponentPool {
        virtual ~IComponentPool() = default;
    };

    template <typename T> struct ComponentPool : IComponentPool {
        std::unordered_map<EntityId, T> data;
    };

    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;

    template <typename T> ComponentPool<T> &pool();
};

template <typename T> EntityManager::ComponentPool<T> &EntityManager::pool()
{
    auto [it, ok] = pools_.try_emplace(std::type_index(typeid(T)),
                                       std::make_unique<ComponentPool<T>>());
    return static_cast<ComponentPool<T> &>(*it->second);
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

template <typename... Ts>
std::vector<EntityId> EntityManager::view() const
{
    std::vector<EntityId> result;
    for (EntityId id : alive_) {
        if ((has_component<Ts>(id) && ...)) {
            result.push_back(id);
        }
    }
    return result;
}

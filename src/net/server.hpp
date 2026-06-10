#pragma once

#include "entities/entity-manager.hpp"
#include "entities/components/position.hpp"
#include "entities/components/combat-stats.hpp"
#include "core/game-mode.hpp"
#include <memory>
#include <vector>
#include <unordered_map>

class CombatSystem;
class NetworkManager;
class WorldState;
class QuestManager;

class Server {
public:
    Server();
    ~Server();

    void set_managers(CombatSystem* cs, WorldState* ws, QuestManager* qm);
    EntityManager& entities() { return em_; }
    EntityId spawn_player(int id);

    void update(float dt, NetworkManager& net);
    void handle_combat_event(int attacker_id, int defender_id, int damage, bool killed);

    EntityId add_entity(Vec2f pos, Team team, int hp, int max_hp);
    void remove_entity(EntityId id);

    // Remote player management
    bool remote_player(int player_id);
    void add_remote_player(int player_id, Vec2f pos);
    void update_remote_player(int player_id, Vec2f pos, int hp, int max_hp, bool alive);
    void send_full_state(NetworkManager& net);
    void mark_needs_full_sync() { needs_full_sync_ = true; }
    bool check_needs_full_sync() { bool v = needs_full_sync_; needs_full_sync_ = false; return v; }

private:
    EntityManager em_;
    std::unordered_map<int, EntityId> remote_player_map_;
    std::unordered_map<int, bool> sent_initial_sync_;
    bool needs_full_sync_ = false;
    CombatSystem* cs_ = nullptr;
    WorldState* ws_ = nullptr;
    QuestManager* qm_ = nullptr;
    std::vector<EntityId> local_players_;
    float sync_timer_ = 0.f;

    std::vector<uint8_t> build_sync_payload();
};

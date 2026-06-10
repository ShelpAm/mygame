#pragma once

#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <memory>

using boost::asio::ip::tcp;

struct RemoteEntity {
    int id = 0;
    Vec2f position, target_pos;
    int hp = 20, max_hp = 20;
    bool alive = true;
    int team = 0;
};

struct NetMessage {
    enum Type { join = 0, state_full = 1, entity_update = 2, chat = 3, disconnect = 4, combat_event = 5, recruit_soldier = 6, spawn_enemy_wave = 7 };
    Type type;
    std::vector<uint8_t> data;
};

class NetworkManager {
public:
    using MsgCallback = std::function<void(const NetMessage&)>;

    NetworkManager();
    ~NetworkManager();

    bool host(int port = 27015);
    bool connect(const std::string& ip, int port = 27015);
    void disconnect();
    bool is_connected() const { return connected_ && !connecting_; }
    bool is_hosting() const { return hosting_; }

    void update();
    void send_entity_update(int player_id, Vec2f pos, int hp, int max_hp, bool alive);
    void send_combat_event(int attacker_id, int defender_id, int damage, bool killed);
    void send_recruit_request(Vec2f player_pos);
    void send_enemy_wave(Vec2f center, int count, Team team);
    void send_chat(const std::string& msg);
    void send_full_sync(const std::vector<uint8_t>& data);
    void interpolate_entities(float dt);
    void set_callback(MsgCallback cb) { callback_ = std::move(cb); }
    const std::vector<RemoteEntity>& remote_entities() const { return remote_entities_; }
    const std::vector<std::string>& chat_history() const { return chat_history_; }

private:
    boost::asio::io_context io_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::unique_ptr<tcp::socket> socket_;
    std::thread thread_;
    std::mutex mutex_;
    std::queue<std::vector<uint8_t>> outgoing_;
    std::vector<uint8_t> read_buf_;

    bool hosting_ = false;
    bool connected_ = false;
    bool connecting_ = false;
    std::vector<RemoteEntity> remote_entities_;
    std::vector<std::string> chat_history_;
    MsgCallback callback_;

    void io_thread();
    void start_read();
    void handle_message(const NetMessage& msg);
    void queue_send(std::vector<uint8_t> data);

    static constexpr int max_players = 4;
};

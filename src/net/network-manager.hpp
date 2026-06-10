#pragma once

#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include "net/net-packet.hpp"

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;

struct RemoteEntity {
    int id = 0;
    Vec2f position, target_pos;
    int hp = 20, max_hp = 20;
    bool alive = true;
    int team = 0;
};

enum class ConnectionState {
    disconnected,
    connecting,
    connected,
};

class NetworkManager {
  public:
    using MsgCallback = std::function<void(NetPacket const &)>;

    NetworkManager();
    ~NetworkManager();

    bool host(int port = 27015);
    bool connect(std::string const &ip, int port = 27015);
    void disconnect();
    bool is_connected() const
    {
        return state_ == ConnectionState::connected;
    }
    bool is_hosting() const
    {
        return hosting_;
    }
    ConnectionState connection_state() const
    {
        return state_;
    }

    void update();
    void queue_send(std::vector<uint8_t> data);
    void send_entity_update(int player_id, Vec2f pos, int hp, int max_hp,
                            bool alive);
    void send_combat_event(int attacker_id, int defender_id, int damage,
                           bool killed);
    void send_recruit_request(std::uint16_t player_id);
    void send_enemy_wave(Vec2f center, int count, Team team);
    void send_chat(std::string const &msg);
    void send_full_sync(std::vector<uint8_t> const &data);
    void interpolate_entities(float dt);
    void set_callback(MsgCallback cb)
    {
        callback_ = std::move(cb);
    }
    std::vector<RemoteEntity> const &remote_entities() const
    {
        return remote_entities_;
    }
    std::vector<std::string> const &chat_history() const
    {
        return chat_history_;
    }

  private:
    boost::asio::io_context io_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::unique_ptr<tcp::socket> socket_;
    std::thread thread_;
    std::mutex mutex_;
    std::queue<std::vector<uint8_t>> outgoing_;
    std::vector<uint8_t> read_buf_;

    bool hosting_ = false;
    ConnectionState state_ = ConnectionState::disconnected;
    std::vector<RemoteEntity> remote_entities_;
    std::vector<std::string> chat_history_;
    MsgCallback callback_;

    void io_thread();
    void start_read();
    void handle_message(NetPacket const &msg);

    static constexpr int max_players = 4;
};

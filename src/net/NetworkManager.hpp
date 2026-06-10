#pragma once

#include "core/Math.hpp"
#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <memory>

using boost::asio::ip::tcp;

struct RemotePlayer {
    int id = 0;
    Vec2f position;
    int hp = 20, maxHp = 20;
    bool alive = true;
};

struct NetMessage {
    enum Type { Join = 0, StateFull = 1, EntityUpdate = 2, Chat = 3, Disconnect = 4 };
    Type type = StateFull;
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
    bool isConnected() const { return m_connected; }
    bool isHosting() const { return m_hosting; }

    void update();
    void sendEntityUpdate(int playerId, Vec2f pos, int hp, int maxHp, bool alive);
    void sendChat(const std::string& msg);
    void setCallback(MsgCallback cb) { m_callback = std::move(cb); }

    const std::vector<RemotePlayer>& remotePlayers() const { return m_remotePlayers; }

private:
    boost::asio::io_context m_io;
    std::unique_ptr<tcp::acceptor> m_acceptor;
    std::unique_ptr<tcp::socket> m_socket;
    std::thread m_thread;
    std::mutex m_mutex;

    bool m_hosting = false;
    bool m_connected = false;
    std::vector<RemotePlayer> m_remotePlayers;
    MsgCallback m_callback;
    std::vector<uint8_t> m_readBuf;

    void acceptLoop();
    void readLoop();
    void handleMessage(const NetMessage& msg);
    void sendRaw(const std::vector<uint8_t>& data);

    static constexpr int MAX_PLAYERS = 4;
};

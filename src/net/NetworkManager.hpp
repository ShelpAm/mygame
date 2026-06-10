#pragma once

#include "core/Math.hpp"
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
    Vec2f position, targetPos;
    int hp = 20, maxHp = 20;
    bool alive = true;
    int team = 0;
};

struct NetMessage {
    enum Type { Join = 0, StateFull = 1, EntityUpdate = 2, Chat = 3, Disconnect = 4, CombatEvent = 5 };
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
    bool isConnected() const { return m_connected && !m_connecting; }
    bool isHosting() const { return m_hosting; }

    void update();
    void sendEntityUpdate(int playerId, Vec2f pos, int hp, int maxHp, bool alive);
    void sendCombatEvent(int attackerId, int defenderId, int damage, bool killed);
    void sendChat(const std::string& msg);
    void sendFullSync(const std::vector<uint8_t>& data);
    void interpolateEntities(float dt);
    void setCallback(MsgCallback cb) { m_callback = std::move(cb); }
    const std::vector<RemoteEntity>& remoteEntities() const { return m_remoteEntities; }
    const std::vector<std::string>& chatHistory() const { return m_chatHistory; }

private:
    boost::asio::io_context m_io;
    std::unique_ptr<tcp::acceptor> m_acceptor;
    std::unique_ptr<tcp::socket> m_socket;
    std::thread m_thread;
    std::mutex m_mutex;
    std::queue<std::vector<uint8_t>> m_outgoing;
    std::vector<uint8_t> m_readBuf;

    bool m_hosting = false;
    bool m_connected = false;
    bool m_connecting = false;
    std::vector<RemoteEntity> m_remoteEntities;
    std::vector<std::string> m_chatHistory;
    MsgCallback m_callback;

    void ioThread();
    void startRead();
    void handleMessage(const NetMessage& msg);
    void queueSend(std::vector<uint8_t> data);

    static constexpr int MAX_PLAYERS = 4;
};

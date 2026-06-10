#include "net/NetworkManager.hpp"
#include "net/NetPacket.hpp"
#include <iostream>
#include <cstring>

NetworkManager::NetworkManager() {}
NetworkManager::~NetworkManager() { disconnect(); }

bool NetworkManager::host(int port) {
    try {
        m_acceptor = std::make_unique<tcp::acceptor>(m_io, tcp::endpoint(tcp::v4(), port));
        m_hosting = true;
        m_connected = true;
        m_thread = std::thread(&NetworkManager::ioThread, this);
        std::cout << "Hosting on port " << port << '\n';
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Host failed: " << e.what() << '\n';
        return false;
    }
}

bool NetworkManager::connect(const std::string& ip, int port) {
    try {
        m_socket = std::make_unique<tcp::socket>(m_io);
        m_connecting = true;
        m_thread = std::thread(&NetworkManager::ioThread, this);
        boost::asio::post(m_io, [this, ip, port]() {
            m_socket->async_connect(
                tcp::endpoint(boost::asio::ip::make_address(ip), port),
                [this](boost::system::error_code ec) {
                    m_connecting = false;
                    if (ec) {
                        std::cerr << "Connect failed: " << ec.message() << '\n';
                        return;
                    }
                    m_connected = true;
                    std::cout << "Connected!\n";
                    startRead();
                });
        });
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Connect failed: " << e.what() << '\n';
        return false;
    }
}

void NetworkManager::ioThread() {
    try {
        if (m_hosting) {
            m_socket = std::make_unique<tcp::socket>(m_io);
            m_acceptor->async_accept(*m_socket, [this](boost::system::error_code ec) {
                if (!ec) { std::cout << "Client connected\n"; startRead(); }
            });
        }
        m_io.run();
    } catch (const std::exception& e) {
        std::cerr << "IO error: " << e.what() << '\n';
        m_connected = false;
    }
}

void NetworkManager::disconnect() {
    if (!m_connected && !m_hosting && !m_connecting) return;
    m_connected = false;
    m_connecting = false;
    m_hosting = false;
    try { m_io.stop(); } catch (...) {}
    if (m_thread.joinable()) m_thread.join();
    m_socket.reset();
    m_acceptor.reset();
    m_io.restart();
}

void NetworkManager::startRead() {
    auto buf = std::make_shared<std::array<uint8_t, 8>>();
    boost::asio::async_read(*m_socket, boost::asio::buffer(*buf),
        [this, buf](boost::system::error_code ec, size_t) {
            if (ec || !m_connected) return;
            uint32_t msgType, size;
            memcpy(&msgType, buf->data(), 4);
            memcpy(&size, buf->data() + 4, 4);

            auto data = std::make_shared<std::vector<uint8_t>>(size);
            if (size > 0) {
                boost::asio::async_read(*m_socket, boost::asio::buffer(*data),
                    [this, msgType, data](boost::system::error_code ec2, size_t) {
                        if (ec2) { m_connected = false; return; }
                        NetMessage msg{static_cast<NetMessage::Type>(msgType), *data};
                        std::lock_guard<std::mutex> lock(m_mutex);
                        handleMessage(msg);
                        if (m_callback) m_callback(msg);
                    });
            } else {
                NetMessage msg{static_cast<NetMessage::Type>(msgType), {}};
                handleMessage(msg);
                if (m_callback) m_callback(msg);
            }
            startRead();  // Continue reading
        });
}

void NetworkManager::handleMessage(const NetMessage& msg) {
    if (msg.type == NetMessage::EntityUpdate && msg.data.size() >= 21) {
        int id; float x, y; int hp, maxHp; uint8_t alive;
        memcpy(&id, msg.data.data(), 4);
        memcpy(&x, msg.data.data() + 4, 4);
        memcpy(&y, msg.data.data() + 8, 4);
        memcpy(&hp, msg.data.data() + 12, 4);
        memcpy(&maxHp, msg.data.data() + 16, 4);
        alive = msg.data[20];
        for (auto& rp : m_remoteEntities) {
            if (rp.id == id) { rp.targetPos = {x,y}; rp.hp = hp; rp.maxHp = maxHp; rp.alive = alive; return; }
        }
        m_remoteEntities.push_back({id, {x,y}, {x,y}, hp, maxHp, (bool)alive});
    } else if (msg.type == NetMessage::Chat) {
        std::string text(msg.data.begin(), msg.data.end());
        m_chatHistory.push_back(text);
    } else if (msg.type == NetMessage::CombatEvent) {
        if (m_callback) m_callback(msg);
    } else if (msg.type == NetMessage::StateFull) {
        // Parse multi-entity sync
        auto& d = msg.data;
        for (size_t i = 0; i + 22 <= d.size(); i += 22) {
            int eid; float x, y; int hp, maxHp; uint8_t alive, team;
            memcpy(&eid, d.data() + i, 4);
            memcpy(&x, d.data() + i + 4, 4);
            memcpy(&y, d.data() + i + 8, 4);
            memcpy(&hp, d.data() + i + 12, 4);
            memcpy(&maxHp, d.data() + i + 16, 4);
            alive = d[i + 20];
            team = d[i + 21];
            bool found = false;
            for (auto& re : m_remoteEntities) {
                if (re.id == eid) { re.targetPos = {x,y}; re.hp = hp; re.maxHp = maxHp; re.alive = alive; re.team = team; found = true; break; }
            }
            if (!found) m_remoteEntities.push_back({eid, {x,y}, {x,y}, hp, maxHp, (bool)alive, (int)team});
        }
    }
}

void NetworkManager::queueSend(std::vector<uint8_t> data) {
    if (!m_connected || m_connecting || !m_socket || !m_socket->is_open()) return;
    auto buf = std::make_shared<std::vector<uint8_t>>(std::move(data));
    boost::asio::async_write(*m_socket, boost::asio::buffer(*buf),
        [buf](boost::system::error_code, size_t) {});
}


void NetworkManager::sendEntityUpdate(int playerId, Vec2f pos, int hp, int maxHp, bool alive) {
    if (!m_connected || m_connecting) return;
    queueSend(makeEntityUpdate(playerId, pos.x, pos.y, hp, maxHp, alive));
}

void NetworkManager::sendFullSync(const std::vector<uint8_t>& payload) {
    if (!m_connected || m_connecting) return;
    queueSend(makeFullSync(payload));
}

void NetworkManager::sendChat(const std::string& msg) {
    if (!m_connected || m_connecting) return;
    m_chatHistory.push_back("You: " + msg);
    queueSend(makeChat(msg));
}

void NetworkManager::sendCombatEvent(int attackerId, int defenderId, int damage, bool killed) {
    if (!m_connected || m_connecting) return;
    queueSend(makeCombatEvent(attackerId, defenderId, damage, killed));
}

void NetworkManager::sendRecruitRequest(Vec2f playerPos) {
    if (!m_connected || m_connecting) return;
    queueSend(makeRecruitRequest(playerPos));
}

void NetworkManager::sendEnemyWave(Vec2f center, int count, Team team) {
    if (!m_connected || m_connecting) return;
    queueSend(makeEnemyWave(center, count, team));
}

void NetworkManager::interpolateEntities(float dt) {
    for (auto& e : m_remoteEntities) {
        float t = std::min(1.f, dt * 15.f);
        e.position.x += (e.targetPos.x - e.position.x) * std::min(1.f, dt * 30.f);
        e.position.y += (e.targetPos.y - e.position.y) * std::min(1.f, dt * 30.f);
    }
}

void NetworkManager::update() {}

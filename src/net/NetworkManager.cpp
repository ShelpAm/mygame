#include "net/NetworkManager.hpp"
#include <iostream>
#include <cstring>

NetworkManager::NetworkManager() : m_readBuf(4096) {}
NetworkManager::~NetworkManager() { disconnect(); }

bool NetworkManager::host(int port) {
    try {
        m_acceptor = std::make_unique<tcp::acceptor>(m_io, tcp::endpoint(tcp::v4(), port));
        m_socket = std::make_unique<tcp::socket>(m_io);
        m_hosting = true;
        m_connected = true;

        m_thread = std::thread([this]() {
            m_acceptor->accept(*m_socket);
            std::cout << "Client connected\n";
            readLoop();
        });
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
        m_socket->connect(tcp::endpoint(boost::asio::ip::make_address(ip), port));
        m_connected = true;

        m_thread = std::thread([this]() { readLoop(); });
        std::cout << "Connected to " << ip << ':' << port << '\n';
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Connect failed: " << e.what() << '\n';
        return false;
    }
}

void NetworkManager::disconnect() {
    if (!m_connected && !m_hosting) return;
    m_connected = false;
    m_hosting = false;
    try {
        if (m_socket && m_socket->is_open()) m_socket->close();
        if (m_acceptor && m_acceptor->is_open()) m_acceptor->close();
    } catch (...) {}
    m_io.stop();
    if (m_thread.joinable()) m_thread.join();
    m_socket.reset();
    m_acceptor.reset();
    m_io.restart();
}

void NetworkManager::readLoop() {
    try {
        while (m_connected) {
            uint32_t msgType = 0, size = 0;
            boost::asio::read(*m_socket, boost::asio::buffer(&msgType, 4));
            boost::asio::read(*m_socket, boost::asio::buffer(&size, 4));

            std::vector<uint8_t> data(size);
            if (size > 0)
                boost::asio::read(*m_socket, boost::asio::buffer(data.data(), size));

            NetMessage msg;
            msg.type = static_cast<NetMessage::Type>(msgType);
            msg.data = std::move(data);

            std::lock_guard<std::mutex> lock(m_mutex);
            handleMessage(msg);
            if (m_callback) m_callback(msg);
        }
    } catch (const std::exception& e) {
        std::cerr << "Connection lost: " << e.what() << '\n';
        m_connected = false;
    }
}

void NetworkManager::handleMessage(const NetMessage& msg) {
    if (msg.type == NetMessage::EntityUpdate && msg.data.size() >= 21) {
        int id; float x, y; int hp, maxHp; uint8_t alive;
        memcpy(&id, msg.data.data(), 4);
        memcpy(&x, msg.data.data() + 4, 4);
        memcpy(&y, msg.data.data() + 8, 4);
        memcpy(&hp, msg.data.data() + 12, 4);
        memcpy(&maxHp, msg.data.data() + 16, 4);
        memcpy(&alive, msg.data.data() + 20, 1);

        for (auto& rp : m_remotePlayers) {
            if (rp.id == id) {
                rp.position = {x, y}; rp.hp = hp;
                rp.maxHp = maxHp; rp.alive = alive; return;
            }
        }
        m_remotePlayers.push_back({id, {x, y}, hp, maxHp, (bool)alive});
    }
}

void NetworkManager::sendEntityUpdate(int playerId, Vec2f pos, int hp, int maxHp, bool alive) {
    if (!m_connected || !m_socket || !m_socket->is_open()) return;
    std::vector<uint8_t> data(21);
    memcpy(data.data(), &playerId, 4);
    memcpy(data.data() + 4, &pos.x, 4);
    memcpy(data.data() + 8, &pos.y, 4);
    memcpy(data.data() + 12, &hp, 4);
    memcpy(data.data() + 16, &maxHp, 4);
    data[20] = alive ? 1 : 0;

    uint32_t type = NetMessage::EntityUpdate;
    uint32_t size = data.size();
    try {
        boost::asio::write(*m_socket, boost::asio::buffer(&type, 4));
        boost::asio::write(*m_socket, boost::asio::buffer(&size, 4));
        boost::asio::write(*m_socket, boost::asio::buffer(data.data(), size));
    } catch (...) {}
}

void NetworkManager::sendChat(const std::string& msg) {
    uint32_t type = NetMessage::Chat;
    uint32_t size = msg.size();
    try {
        boost::asio::write(*m_socket, boost::asio::buffer(&type, 4));
        boost::asio::write(*m_socket, boost::asio::buffer(&size, 4));
        boost::asio::write(*m_socket, boost::asio::buffer(msg.data(), size));
    } catch (...) {}
}

void NetworkManager::update() {}

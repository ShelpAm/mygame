#include "net/network-manager.hpp"
#include "net/net-packet.hpp"
#include <iostream>
#include <cstring>

NetworkManager::NetworkManager() {}
NetworkManager::~NetworkManager() { disconnect(); }

bool NetworkManager::host(int port) {
    try {
        acceptor_ = std::make_unique<tcp::acceptor>(io_, tcp::endpoint(tcp::v4(), port));
        hosting_ = true;
        connected_ = true;
        thread_ = std::thread(&NetworkManager::io_thread, this);
        std::cout << "Hosting on port " << port << '\n';
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Host failed: " << e.what() << '\n';
        return false;
    }
}

bool NetworkManager::connect(const std::string& ip, int port) {
    try {
        socket_ = std::make_unique<tcp::socket>(io_);
        connecting_ = true;
        thread_ = std::thread(&NetworkManager::io_thread, this);
        boost::asio::post(io_, [this, ip, port]() {
            socket_->async_connect(
                tcp::endpoint(boost::asio::ip::make_address(ip), port),
                [this](boost::system::error_code ec) {
                    connecting_ = false;
                    if (ec) {
                        std::cerr << "Connect failed: " << ec.message() << '\n';
                        return;
                    }
                    connected_ = true;
                    std::cout << "Connected!\n";
                    start_read();
                });
        });
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Connect failed: " << e.what() << '\n';
        return false;
    }
}

void NetworkManager::io_thread() {
    try {
        if (hosting_) {
            socket_ = std::make_unique<tcp::socket>(io_);
            acceptor_->async_accept(*socket_, [this](boost::system::error_code ec) {
                if (!ec) { std::cout << "Client connected\n"; start_read(); }
            });
        }
        io_.run();
    } catch (const std::exception& e) {
        std::cerr << "IO error: " << e.what() << '\n';
        connected_ = false;
    }
}

void NetworkManager::disconnect() {
    if (!connected_ && !hosting_ && !connecting_) return;
    connected_ = false;
    connecting_ = false;
    hosting_ = false;
    try { io_.stop(); } catch (...) {}
    if (thread_.joinable()) thread_.join();
    socket_.reset();
    acceptor_.reset();
    io_.restart();
}

void NetworkManager::start_read() {
    auto buf = std::make_shared<std::array<uint8_t, 8>>();
    boost::asio::async_read(*socket_, boost::asio::buffer(*buf),
        [this, buf](boost::system::error_code ec, size_t) {
            if (ec || !connected_) return;
            uint32_t msgType, size;
            memcpy(&msgType, buf->data(), 4);
            memcpy(&size, buf->data() + 4, 4);

            auto data = std::make_shared<std::vector<uint8_t>>(size);
            if (size > 0) {
                boost::asio::async_read(*socket_, boost::asio::buffer(*data),
                    [this, msgType, data](boost::system::error_code ec2, size_t) {
                        if (ec2) { connected_ = false; return; }
                        NetMessage msg{static_cast<NetMessage::Type>(msgType), *data};
                        std::lock_guard<std::mutex> lock(mutex_);
                        handle_message(msg);
                        if (callback_) callback_(msg);
                    });
            } else {
                NetMessage msg{static_cast<NetMessage::Type>(msgType), {}};
                handle_message(msg);
                if (callback_) callback_(msg);
            }
            start_read();  // Continue reading
        });
}

void NetworkManager::handle_message(const NetMessage& msg) {
    if (msg.type == NetMessage::entity_update && msg.data.size() >= 21) {
        int id; float x, y; int hp, max_hp; uint8_t alive;
        memcpy(&id, msg.data.data(), 4);
        memcpy(&x, msg.data.data() + 4, 4);
        memcpy(&y, msg.data.data() + 8, 4);
        memcpy(&hp, msg.data.data() + 12, 4);
        memcpy(&max_hp, msg.data.data() + 16, 4);
        alive = msg.data[20];
        for (auto& rp : remote_entities_) {
            if (rp.id == id) { rp.target_pos = {x,y}; rp.hp = hp; rp.max_hp = max_hp; rp.alive = alive; return; }
        }
        remote_entities_.push_back({id, {x,y}, {x,y}, hp, max_hp, (bool)alive});
    } else if (msg.type == NetMessage::chat) {
        std::string text(msg.data.begin(), msg.data.end());
        chat_history_.push_back(text);
    } else if (msg.type == NetMessage::combat_event) {
        if (callback_) callback_(msg);
    } else if (msg.type == NetMessage::state_full) {
        // Parse multi-entity sync
        auto& d = msg.data;
        for (size_t i = 0; i + 22 <= d.size(); i += 22) {
            int eid; float x, y; int hp, max_hp; uint8_t alive, team;
            memcpy(&eid, d.data() + i, 4);
            memcpy(&x, d.data() + i + 4, 4);
            memcpy(&y, d.data() + i + 8, 4);
            memcpy(&hp, d.data() + i + 12, 4);
            memcpy(&max_hp, d.data() + i + 16, 4);
            alive = d[i + 20];
            team = d[i + 21];
            bool found = false;
            for (auto& re : remote_entities_) {
                if (re.id == eid) { re.target_pos = {x,y}; re.hp = hp; re.max_hp = max_hp; re.alive = alive; re.team = team; found = true; break; }
            }
            if (!found) remote_entities_.push_back({eid, {x,y}, {x,y}, hp, max_hp, (bool)alive, (int)team});
        }
    }
}

void NetworkManager::queue_send(std::vector<uint8_t> data) {
    if (!connected_ || connecting_ || !socket_ || !socket_->is_open()) return;
    auto buf = std::make_shared<std::vector<uint8_t>>(std::move(data));
    boost::asio::async_write(*socket_, boost::asio::buffer(*buf),
        [buf](boost::system::error_code, size_t) {});
}


void NetworkManager::send_entity_update(int player_id, Vec2f pos, int hp, int max_hp, bool alive) {
    if (!connected_ || connecting_) return;
    queue_send(make_entity_update(player_id, pos.x, pos.y, hp, max_hp, alive));
}

void NetworkManager::send_full_sync(const std::vector<uint8_t>& payload) {
    if (!connected_ || connecting_) return;
    queue_send(make_full_sync(payload));
}

void NetworkManager::send_chat(const std::string& msg) {
    if (!connected_ || connecting_) return;
    chat_history_.push_back("You: " + msg);
    queue_send(make_chat(msg));
}

void NetworkManager::send_combat_event(int attacker_id, int defender_id, int damage, bool killed) {
    if (!connected_ || connecting_) return;
    queue_send(make_combat_event(attacker_id, defender_id, damage, killed));
}

void NetworkManager::send_recruit_request(Vec2f player_pos) {
    if (!connected_ || connecting_) return;
    queue_send(make_recruit_request(player_pos));
}

void NetworkManager::send_enemy_wave(Vec2f center, int count, Team team) {
    if (!connected_ || connecting_) return;
    queue_send(make_enemy_wave(center, count, team));
}

void NetworkManager::interpolate_entities(float dt) {
    for (auto& e : remote_entities_) {
        float t = std::min(1.f, dt * 15.f);
        e.position.x += (e.target_pos.x - e.position.x) * std::min(1.f, dt * 30.f);
        e.position.y += (e.target_pos.y - e.position.y) * std::min(1.f, dt * 30.f);
    }
}

void NetworkManager::update() {}

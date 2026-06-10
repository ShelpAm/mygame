#include "net/network-manager.hpp"
#include "net/net-packet.hpp"
#include <cstring>
#include <iostream>

NetworkManager::NetworkManager() {}
NetworkManager::~NetworkManager()
{
    disconnect();
}

bool NetworkManager::host(int port)
{
    try {
        acceptor_ = std::make_unique<tcp::acceptor>(
            io_, tcp::endpoint(tcp::v4(), port));
        hosting_ = true;
        state_ = ConnectionState::connected;
        thread_ = std::thread(&NetworkManager::io_thread, this);
        std::cout << "Hosting on port " << port << '\n';
        return true;
    }
    catch (std::exception const &e) {
        std::cerr << "Host failed: " << e.what() << '\n';
        return false;
    }
}

bool NetworkManager::connect(std::string const &ip, int port)
{
    try {
        socket_ = std::make_unique<tcp::socket>(io_);
        state_ = ConnectionState::connecting;
        thread_ = std::thread(&NetworkManager::io_thread, this);
        boost::asio::post(io_, [this, ip, port]() {
            socket_->async_connect(
                tcp::endpoint(boost::asio::ip::make_address(ip), port),
                [this](boost::system::error_code ec) {
                    if (ec) {
                        state_ = ConnectionState::disconnected;
                        std::cerr << "Connect failed: " << ec.message() << '\n';
                        return;
                    }
                    state_ = ConnectionState::connected;
                    std::cout << "Connected!\n";
                    start_read();
                });
        });
        return true;
    }
    catch (std::exception const &e) {
        std::cerr << "Connect failed: " << e.what() << '\n';
        return false;
    }
}

void NetworkManager::io_thread()
{
    try {
        if (hosting_) {
            socket_ = std::make_unique<tcp::socket>(io_);
            acceptor_->async_accept(*socket_,
                                    [this](boost::system::error_code ec) {
                                        if (!ec) {
                                            std::cout << "Client connected\n";
                                            start_read();
                                        }
                                    });
        }
        io_.run();
    }
    catch (std::exception const &e) {
        std::cerr << "IO error: " << e.what() << '\n';
        state_ = ConnectionState::disconnected;
    }
}

void NetworkManager::disconnect()
{
    if (state_ == ConnectionState::disconnected && !hosting_)
        return;
    state_ = ConnectionState::disconnected;
    hosting_ = false;
    try {
        io_.stop();
    }
    catch (...) {
    }
    if (thread_.joinable())
        thread_.join();
    socket_.reset();
    acceptor_.reset();
    io_.restart();
}

void NetworkManager::start_read()
{
    auto buf = std::make_shared<std::array<uint8_t, 8>>();
    boost::asio::async_read(
        *socket_, boost::asio::buffer(*buf),
        [this, buf](boost::system::error_code ec, size_t) {
            if (ec || state_ != ConnectionState::connected)
                return;
            uint32_t msgType, size;
            memcpy(&msgType, buf->data(), 4);
            memcpy(&size, buf->data() + 4, 4);

            auto data = std::make_shared<std::vector<uint8_t>>(size);
            if (size > 0) {
                boost::asio::async_read(
                    *socket_, boost::asio::buffer(*data),
                    [this, msgType, data](boost::system::error_code ec2,
                                          size_t) {
                        if (ec2) {
                            state_ = ConnectionState::disconnected;
                            return;
                        }
                        NetPacket pkt{static_cast<NetPacket::Type>(msgType),
                                      *data};
                        std::lock_guard<std::mutex> lock(mutex_);
                        handle_message(pkt);
                        if (callback_)
                            callback_(pkt);
                    });
            }
            else {
                NetPacket pkt{static_cast<NetPacket::Type>(msgType), {}};
                handle_message(pkt);
                if (callback_)
                    callback_(pkt);
            }
            start_read(); // Continue reading
        });
}

void NetworkManager::handle_message(NetPacket const &msg)
{
    if (msg.type == NetPacket::entity_update && msg.payload.size() >= 21) {
        int id;
        float x, y;
        int hp, max_hp;
        uint8_t alive;
        memcpy(&id, msg.payload.data(), 4);
        memcpy(&x, msg.payload.data() + 4, 4);
        memcpy(&y, msg.payload.data() + 8, 4);
        memcpy(&hp, msg.payload.data() + 12, 4);
        memcpy(&max_hp, msg.payload.data() + 16, 4);
        alive = msg.payload[20];
        for (auto &rp : remote_entities_) {
            if (rp.id == id) {
                rp.target_pos = {x, y};
                rp.hp = hp;
                rp.max_hp = max_hp;
                rp.alive = alive;
                return;
            }
        }
        remote_entities_.push_back(
            {id, {x, y}, {x, y}, hp, max_hp, (bool)alive});
    }
    else if (msg.type == NetPacket::chat) {
        std::string text(msg.payload.begin(), msg.payload.end());
        chat_history_.push_back(text);
    }
    else if (msg.type == NetPacket::combat_event) {
        if (callback_)
            callback_(msg);
    }
    else if (msg.type == NetPacket::state_full) {
        // Parse multi-entity sync
        auto &d = msg.payload;
        for (size_t i = 0; i + 22 <= d.size(); i += 22) {
            int eid;
            float x, y;
            int hp, max_hp;
            uint8_t alive, team;
            memcpy(&eid, d.data() + i, 4);
            memcpy(&x, d.data() + i + 4, 4);
            memcpy(&y, d.data() + i + 8, 4);
            memcpy(&hp, d.data() + i + 12, 4);
            memcpy(&max_hp, d.data() + i + 16, 4);
            alive = d[i + 20];
            team = d[i + 21];
            bool found = false;
            for (auto &re : remote_entities_) {
                if (re.id == eid) {
                    re.target_pos = {x, y};
                    re.hp = hp;
                    re.max_hp = max_hp;
                    re.alive = alive;
                    re.team = team;
                    found = true;
                    break;
                }
            }
            if (!found)
                remote_entities_.push_back(
                    {eid, {x, y}, {x, y}, hp, max_hp, (bool)alive, (int)team});
        }
    }
}

void NetworkManager::queue_send(std::vector<uint8_t> data)
{
    if (state_ != ConnectionState::connected || !socket_ || !socket_->is_open())
        return;
    auto buf = std::make_shared<std::vector<uint8_t>>(std::move(data));
    boost::asio::async_write(*socket_, boost::asio::buffer(*buf),
                             [buf](boost::system::error_code, size_t) {});
}

void NetworkManager::send_entity_update(int player_id, Vec2f pos, int hp,
                                        int max_hp, bool alive)
{
    if (state_ != ConnectionState::connected)
        return;
    queue_send(make_entity_update(player_id, pos.x, pos.y, hp, max_hp, alive));
}

void NetworkManager::send_full_sync(std::vector<uint8_t> const &payload)
{
    if (state_ != ConnectionState::connected)
        return;
    queue_send(make_full_sync(payload));
}

void NetworkManager::send_chat(std::string const &msg)
{
    if (state_ != ConnectionState::connected)
        return;
    chat_history_.push_back("You: " + msg);
    queue_send(make_chat(msg));
}

void NetworkManager::send_combat_event(int attacker_id, int defender_id,
                                       int damage, bool killed)
{
    if (state_ != ConnectionState::connected)
        return;
    queue_send(make_combat_event(attacker_id, defender_id, damage, killed));
}

void NetworkManager::send_recruit_request(std::uint16_t player_id)
{
    if (state_ != ConnectionState::connected)
        return;
    queue_send(make_recruit_request(player_id));
}

void NetworkManager::send_enemy_wave(Vec2f center, int count, Team team)
{
    if (state_ != ConnectionState::connected)
        return;
    queue_send(make_enemy_wave(center, count, team));
}

void NetworkManager::interpolate_entities(float dt)
{
    for (auto &e : remote_entities_) {
        float t = std::min(1.f, dt * 15.f);
        e.position.x +=
            (e.target_pos.x - e.position.x) * std::min(1.f, dt * 30.f);
        e.position.y +=
            (e.target_pos.y - e.position.y) * std::min(1.f, dt * 30.f);
    }
}

void NetworkManager::update() {}

#include "net/local-transport.hpp"
#include "net/net-packet.hpp"
#include "net/network-transport.hpp"
#include <boost/test/unit_test.hpp>
#include <thread>

struct NetworkFixture {
    NetworkFixture() = default;
    ~NetworkFixture()
    {
        NetworkTransport::shutdown();
    }
};

BOOST_GLOBAL_FIXTURE(NetworkFixture);

BOOST_AUTO_TEST_SUITE(network_tests)

// -- Packet serialization --
BOOST_AUTO_TEST_CASE(write_read_int_roundtrip)
{
    std::vector<uint8_t> buf;
    write_bytes(buf, uint32_t{42});
    BOOST_TEST(buf.size() == 4);
    BOOST_TEST(read_bytes<uint32_t>(buf, 0) == 42);
}

BOOST_AUTO_TEST_CASE(write_read_mixed_types)
{
    std::vector<uint8_t> buf;
    write_bytes(buf, uint16_t{100});
    write_bytes(buf, uint32_t{99999});
    BOOST_TEST(read_bytes<uint16_t>(buf, 0) == 100);
    BOOST_TEST(read_bytes<uint32_t>(buf, 2) == 99999);
}

BOOST_AUTO_TEST_CASE(serialize_packet_roundtrip)
{
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    auto data = serialize_packet({NetPacket::chat, payload});
    // 4 bytes type + 4 bytes size + payload
    BOOST_TEST(data.size() == 8 + payload.size());
    // Verify header
    auto type = read_bytes<uint32_t>(data, 0);
    auto size = read_bytes<uint32_t>(data, 4);
    BOOST_TEST(type == static_cast<uint32_t>(NetPacket::chat));
    BOOST_TEST(size == payload.size());
    // Verify payload
    BOOST_TEST(data[8] == 0x01);
    BOOST_TEST(data[9] == 0x02);
    BOOST_TEST(data[10] == 0x03);
}

BOOST_AUTO_TEST_CASE(make_and_parse_entity_update)
{
    auto data = make_entity_update(7, 1.5f, -2.0f, 10, 20, true);
    // Strip header
    auto header_type = read_bytes<uint32_t>(data, 0);
    auto header_size = read_bytes<uint32_t>(data, 4);
    BOOST_TEST(header_type == static_cast<uint32_t>(NetPacket::entity_update));

    // Parse payload (skip 8-byte header)
    std::vector<uint8_t> payload(data.begin() + 8, data.end());
    auto u = parse_entity_update(payload);
    BOOST_TEST(u.id == 7);
    BOOST_TEST(u.x == 1.5f);
    BOOST_TEST(u.y == -2.0f);
    BOOST_TEST(u.hp == 10);
    BOOST_TEST(u.max_hp == 20);
    BOOST_TEST(u.alive);
}

BOOST_AUTO_TEST_CASE(make_and_parse_combat_event)
{
    auto data = make_combat_event(1, 3, 25, true);
    std::vector<uint8_t> payload(data.begin() + 8, data.end());
    auto ev = parse_combat_event(payload);
    BOOST_TEST(ev.attacker_id == 1);
    BOOST_TEST(ev.defender_id == 3);
    BOOST_TEST(ev.damage == 25);
    BOOST_TEST(ev.killed);
}

BOOST_AUTO_TEST_CASE(make_chat_preserves_text)
{
    auto data = make_chat("hello world");
    std::vector<uint8_t> payload(data.begin() + 8, data.end());
    std::string text(payload.begin(), payload.end());
    BOOST_TEST(text == "hello world");
}

// -- Local transport --
BOOST_AUTO_TEST_CASE(local_transport_send_receive)
{
    auto [a, b] = create_transport_pair();

    int received = 0;
    a->set_callback([&](TransportExMessage const &msg) {
        BOOST_TEST(msg.from == a.get());
        BOOST_TEST(msg.type == NetPacket::chat);
        received++;
    });

    std::vector<uint8_t> pl = {'h', 'i'};
    b->send({NetPacket::chat, pl});
    a->consume();
    BOOST_TEST(received == 1);
}

BOOST_AUTO_TEST_CASE(local_transport_bidirectional)
{
    auto [a, b] = create_transport_pair();

    int a_count = 0, b_count = 0;
    a->set_callback([&](TransportExMessage const &) { a_count++; });
    b->set_callback([&](TransportExMessage const &) { b_count++; });

    b->send({NetPacket::chat, {}});
    a->consume();
    BOOST_TEST(a_count == 1);

    a->send({NetPacket::join, {}});
    b->consume();
    BOOST_TEST(b_count == 1);
}

BOOST_AUTO_TEST_CASE(local_transport_multiple_messages)
{
    auto [a, b] = create_transport_pair();

    int count = 0;
    a->set_callback([&](TransportExMessage const &) { count++; });

    for (int i = 0; i < 5; ++i)
        b->send({NetPacket::chat, {}});

    a->consume();
    BOOST_TEST(count == 5);
}

BOOST_AUTO_TEST_CASE(local_transport_is_connected)
{
    auto [a, b] = create_transport_pair();
    BOOST_TEST(a->is_connected());
    BOOST_TEST(b->is_connected());
}

// -- NetworkTransport lifecycle --
BOOST_AUTO_TEST_CASE(network_transport_not_connected_initially)
{
    auto peer = std::make_unique<NetworkTransport>();
    BOOST_TEST(!peer->is_connected());
}

BOOST_AUTO_TEST_CASE(network_transport_do_receive_empty)
{
    auto peer = std::make_unique<NetworkTransport>();
    int called = 0;
    peer->set_callback([&](TransportExMessage const &) { called++; });
    peer->consume(); // no data, should not call callback
    BOOST_TEST(called == 0);
}

BOOST_AUTO_TEST_CASE(network_transport_on_connected)
{
    auto peer = std::make_unique<NetworkTransport>();
    BOOST_TEST(!peer->is_connected());
    peer->on_connected();
    BOOST_TEST(peer->is_connected());
}

BOOST_AUTO_TEST_CASE(network_transport_has_socket)
{
    auto peer = std::make_unique<NetworkTransport>();
    // Socket exists but is not open (no connect/accept yet)
    auto &sock = peer->socket();
    BOOST_TEST(!sock.is_open());
}

// -- Listener lifecycle --
BOOST_AUTO_TEST_CASE(listener_default_constructed)
{
    NetworkTransport::Acceptor l;
    // Default-constructed; not listening
    l.stop(); // no-op, safe to call
}

BOOST_AUTO_TEST_CASE(listener_move)
{
    NetworkTransport::Acceptor a;
    NetworkTransport::Acceptor b = std::move(a);
    b.stop(); // moved-from a is in valid-but-unspecified state
}

BOOST_AUTO_TEST_CASE(listener_stop_before_listen_is_safe)
{
    NetworkTransport::Acceptor l;
    l.stop(); // should not crash
}

BOOST_AUTO_TEST_CASE(listener_listen_twice_rejected)
{
    NetworkTransport::Acceptor l;
    bool ok = l.listen(0, [](std::unique_ptr<NetworkTransport>) {});
    // Port 0 means OS picks an ephemeral port
    BOOST_TEST(ok);
    // Second listen should be rejected
    bool ok2 = l.listen(0, [](std::unique_ptr<NetworkTransport>) {});
    BOOST_TEST(!ok2);
    l.stop();
}

// -- Thread-safety smoke test for local transport --
BOOST_AUTO_TEST_CASE(local_transport_threaded_send)
{
    auto [a, b] = create_transport_pair();
    std::atomic<int> count{0};
    a->set_callback([&](TransportExMessage const &) { count++; });

    std::thread t([&]() {
        for (int i = 0; i < 100; ++i)
            b->send({NetPacket::chat, {}});
    });
    t.join();

    a->consume();
    BOOST_TEST(count == 100);
}

BOOST_AUTO_TEST_SUITE_END()

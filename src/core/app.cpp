#include "core/app.hpp"

#include "core/game-mode.hpp"
#include "core/resource-manager.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "dialogue/relationship-table.hpp"
#include "entities/components/combat-stats.hpp"
#include "factions/event-simulator.hpp"
#include "knowledge/rumor-propagator.hpp"
#include "net/network-manager.hpp"
#include "save/save-manager.hpp"
#include "systems/render-system.hpp"
#include "ui/ui-manager.hpp"
#include <boost/asio.hpp>
#include <fstream>
#include <imgui.h>
#include <print>

App::App() = default;
App::~App()
{
    shutdown();
}

bool App::init()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        return false;
    window_ = SDL_CreateWindow(window_title, window_width, window_height,
                               SDL_WINDOW_RESIZABLE);
    if (!window_)
        return false;
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
        return false;
    SDL_SetRenderVSync(renderer_, 1);

    resources_ = std::make_unique<ResourceManager>();
    render_system_ =
        std::make_unique<RenderSystem>(renderer_, *resources_, camera_system_);
    // Ensure fresh state (no-op, keeps pattern)
    navigation_system_ = NavigationSystem{};
    ui_manager_ = std::make_unique<UIManager>(window_, renderer_);
    network_ = std::make_unique<NetworkManager>();

    locale_.discover_languages("assets/locale");
    locale_.set_language(0);
    dialogue_engine_.discover_languages("assets/dialogue");
    quests_.load_from_json("assets/data/quests.json");

    events_ =
        std::make_unique<EventSimulator>(factions_, knowledge_, world_state_);
    rumors_ = std::make_unique<RumorPropagator>(knowledge_);

    server_ = std::make_unique<Server>();
    server_->set_managers(&combat_, &world_state_, &quests_);
    server_->set_survival(&survival_);
    server_->set_event_simulator(events_.get());

    game_mode_ = std::make_unique<GameMode>(server_->entities());
    game_mode_->init_world(world_state_, knowledge_, dialogue_engine_,
                           topic_registry_, relationships_, factions_, *events_,
                           *rumors_, combat_, quests_, *network_);

    server_->set_game_mode(game_mode_.get());

    start_local_session();

    game_clock_.restart();
    running_ = true;
    return true;
}

void App::start_local_session()
{
    session_mode_ = SessionMode::local;
    if (!server_) {
        server_ = std::make_unique<Server>();
        server_->set_managers(&combat_, &world_state_, &quests_);
        server_->set_survival(&survival_);
        server_->set_event_simulator(events_.get());
        server_->set_game_mode(game_mode_.get());
    }
    server_->clear_transports();
    server_->attach_local_pair(client_);

    client_.send_join_request();
}

void App::start_host_session(int port)
{
    stop_session();
    session_mode_ = SessionMode::host;
    if (!server_) {
        server_ = std::make_unique<Server>();
        server_->set_managers(&combat_, &world_state_, &quests_);
        server_->set_survival(&survival_);
        server_->set_event_simulator(events_.get());
        server_->set_game_mode(game_mode_.get());
    }
    network_->host(port);
    server_->attach_local_pair(client_);
    server_->attach_network(*network_);

    client_.send_join_request();
}

void App::start_client_session(std::string const &host, int port)
{
    stop_session();
    session_mode_ = SessionMode::client;
    server_.reset();

    std::string resolved_ip = host;
    try {
        boost::asio::ip::make_address(host);
    }
    catch (...) {
        try {
            boost::asio::io_context io;
            boost::asio::ip::tcp::resolver resolver(io);
            auto results = resolver.resolve(host, std::to_string(port));
            for (auto const &ep : results) {
                resolved_ip = ep.endpoint().address().to_string();
                break;
            }
        }
        catch (...) {
        }
    }

    network_->connect(resolved_ip, port);
    client_.attach_network(*network_);

    client_.send_join_request();
}

void App::stop_session()
{
    if (network_)
        network_->disconnect();
    if (server_)
        server_->clear_transports();
    client_.detach_transport();
}

void App::run()
{
    while (running_) {
        float dt = game_clock_.tick();
        process_events();
        if (!running_)
            break;
        update(dt);
        render();
    }
}

void App::shutdown()
{
    running_ = false;
    stop_session();
    network_.reset();
    server_.reset();
    game_mode_.reset();
    events_.reset();
    rumors_.reset();
    ui_manager_.reset();
    render_system_.reset();
    resources_.reset();
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void App::process_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ui_manager_->process_event(event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            camera_system_.resize(event.window.data1, event.window.data2);
            break;
        }
    }
    input_.update();
}

void App::update(float dt)
{
    survival_.update(dt, false, false);
    world_state_.update(dt);
    events_->update(world_state_.day());

    client_.update(dt);

    // Handles move
    float mx = 0, my = 0;
    bool in_dialogue = game_mode_->dialogue().active;
    if (!ImGui::IsAnyItemActive() && !client_.is_player_dead() &&
        !in_dialogue) {
        if (input_.is_pressed(InputManager::Action::move_up))
            my -= 1;
        if (input_.is_pressed(InputManager::Action::move_down))
            my += 1;
        if (input_.is_pressed(InputManager::Action::move_left))
            mx -= 1;
        if (input_.is_pressed(InputManager::Action::move_right))
            mx += 1;
    }
    if (mx != 0 || my != 0) {
        float len = std::hypot(mx, my);
        client_.send_player_direction({mx / len, my / len});
    }

    // Discrete actions — just_pressed fires once per key press
    if (input_.just_pressed(InputManager::Action::interact))
        client_.send_interact();
    if (input_.just_pressed(InputManager::Action::rest))
        client_.send_rest();
    if (input_.just_pressed(InputManager::Action::recruit))
        client_.send_recruit();
    if (input_.just_pressed(InputManager::Action::quick_save))
        quick_save();
    if (input_.just_pressed(InputManager::Action::load_menu))
        show_load_menu_ = !show_load_menu_;
    if (input_.just_pressed(InputManager::Action::help))
        show_help_ = !show_help_;
    if (input_.just_pressed(InputManager::Action::multiplayer))
        show_multiplayer_ = !show_multiplayer_;

    if (session_mode_ != SessionMode::client)
        server_->update(dt);

    // TODO: healthy state
    // if (survival_.state().food <= 0 || survival_.state().water <= 0) {
    //     auto *pcs = server_.entities().get_component<CombatStats>(
    //         server_.host_player_id());
    //     if (pcs && pcs->alive) {
    //         pcs->hp = std::max(0, pcs->hp - 1);
    //         server_.mark_needs_full_sync();
    //     }
    // }

    camera_system_.set_target(client_.player_position());
    camera_system_.update(dt);
    // TODO: What is this?
    // game_mode_->update(server_.host_player_id(), dt);
    ui_manager_->update(dt);
}

void App::render()
{
    SDL_SetRenderDrawColor(renderer_, 10, 10, 15, 255);
    SDL_RenderClear(renderer_);

    render_system_->render(client_.entities(), world_state_, navigation_system_,
                           combat_.events());
    combat_.clear_events();
    ui_manager_->render(world_state_, *this);
    SDL_RenderPresent(renderer_);
}

void App::set_ui_language(int lang_index)
{
    locale_.set_language(lang_index);
    dialogue_engine_.set_language(lang_index);
}

bool App::is_player_dead() const
{
    return const_cast<Client &>(client_).is_player_dead();
}

CombatStats const *App::player_combat_stats() const
{
    return const_cast<Client &>(client_).player_stats();
}

void App::quick_save()
{
    std::println("Unimplemented: quick save");
    return;
    save_to_slot(next_save_slot_++);
}

void App::save_to_slot(int slot)
{
    // auto *pos =
    //     server_.entities().get_component<Position>(server_.host_player_id());
    // auto *cs =
    //     server_.entities().get_component<CombatStats>(server_.host_player_id());
    // Vec2f pp = pos ? pos->world_pos : Vec2f{};
    //
    // std::vector<SaveManager::NPCData> npcData;
    // for (auto eid : game_mode_->npc_entities()) {
    //     auto *np = server_.entities().get_component<Position>(eid);
    //     auto *ns = server_.entities().get_component<NPCState>(eid);
    //     auto *ncs = server_.entities().get_component<CombatStats>(eid);
    //     if (!np || !ns)
    //         continue;
    //     SaveManager::NPCData nd;
    //     nd.id = ns->npc_id;
    //     nd.name = ns->display_name;
    //     nd.personality = ns->personality;
    //     nd.position = np->world_pos;
    //     nd.hp = ncs ? ncs->hp : 10;
    //     nd.max_hp = ncs ? ncs->max_hp : 10;
    //     nd.alive = ncs ? ncs->alive : true;
    //     for (auto const &[fid, kf] : ns->knowledge)
    //         nd.knowledge[fid] = kf.npc_version;
    //     npcData.push_back(std::move(nd));
    // }
    // SaveManager::save("saves/save_" + std::to_string(slot) + ".json",
    //                   world_state_, knowledge_, relationships_, pp,
    //                   cs ? cs->hp : 20, cs ? cs->max_hp : 20, npcData);
}

std::vector<int> App::available_save_slots() const
{
    std::vector<int> slots;
    for (int i = 1; i <= 10; ++i) {
        std::ifstream f("saves/save_" + std::to_string(i) + ".json");
        if (f.good())
            slots.push_back(i);
    }
    return slots;
}

void App::load_from_slot(int slot)
{
    std::string path = "saves/save_" + std::to_string(slot) + ".json";
    SaveManager::SaveData data;
    if (!SaveManager::load(path, data))
        return;

    while (!server_->entities().all_entities().empty())
        server_->entities().destroy_entity(
            server_->entities().all_entities().back());
    game_mode_ = std::make_unique<GameMode>(server_->entities());

    auto pid = game_mode_->spawn_player(data.player_pos.x, data.player_pos.y);
    auto *cs = server_->entities().get_component<CombatStats>(pid);
    if (cs) {
        cs->hp = data.player_hp;
        cs->max_hp = data.player_max_hp;
    }

    world_state_.set_day(data.day);
    world_state_.set_season(data.season);
    for (auto const &t : data.seen_tiles)
        world_state_.reveal_tile(t);
    knowledge_.mark_topic_known("ugarit_sack");
    knowledge_.mark_topic_known("sea_peoples");
    knowledge_.mark_topic_known("byblos_king");
    for (auto const &t : data.known_topics)
        knowledge_.mark_topic_known(t);
    relationships_ = {};
    for (auto const &[nid, rel] : data.relations)
        relationships_.set_relation(nid, {rel[0], rel[1], rel[2]});

    for (auto const &nd : data.npcs) {
        std::vector<NPCKnowledgeEntry> facts;
        for (auto const &[fid, ver] : nd.knowledge)
            facts.push_back({fid, ver, 70, false, ""});
        game_mode_->spawn_npc(nd.id, nd.name, nd.position.x, nd.position.y,
                              nd.personality, facts);
        auto &eid = game_mode_->npc_entities().back();
        auto *ncs = server_->entities().get_component<CombatStats>(eid);
        if (ncs) {
            ncs->hp = nd.hp;
            ncs->max_hp = nd.max_hp;
            ncs->alive = nd.alive;
        }
    }

    server_->set_game_mode(game_mode_.get());

    stop_session();
    client_.reset();
    start_local_session();

    auto *pos = server_->entities().get_component<Position>(pid);
    if (pos)
        camera_system_.center_on(pos->world_pos);
}

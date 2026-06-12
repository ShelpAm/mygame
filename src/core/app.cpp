#include "core/app.hpp"

#include "core/game-mode.hpp"
#include "core/resource-manager.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/position.hpp"
#include "factions/event-simulator.hpp"
#include "knowledge/rumor-propagator.hpp"
#include "net/local-transport.hpp"
#include "net/network-transport.hpp"
#include "save/save-manager.hpp"
#include "systems/render-system.hpp"
#include "ui/ui-manager.hpp"
#include <boost/asio.hpp>
#include <fstream>
#include <imgui.h>
#include <spdlog/spdlog.h>

App::App() = default;
App::~App()
{
    shutdown();
}

void App::init()
{
    SDL_SetAppMetadata("The Sunset Straits App name", "1.0",
                       "com.example.app-identifier");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        throw std::runtime_error("Failed to initialize SDL");

    if (!SDL_CreateWindowAndRenderer(window_title, window_width, window_height,
                                     SDL_WINDOW_RESIZABLE, &window_,
                                     &renderer_))
        throw std::runtime_error("Failed to create window and renderer");

    SDL_SetRenderLogicalPresentation(renderer_, window_width, window_height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer_, 1);

    resources_ = std::make_unique<ResourceManager>();
    render_system_ =
        std::make_unique<RenderSystem>(renderer_, *resources_, camera_system_);
    // Ensure fresh state (no-op, keeps pattern)
    navigation_system_ = NavigationSystem{};
    ui_manager_ = std::make_unique<UIManager>(window_, renderer_);

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

    game_mode_ = std::make_unique<GameMode>();
    game_mode_->init_world(world_state_, knowledge_, dialogue_engine_,
                           factions_, *events_, *rumors_, combat_, quests_);

    server_->set_game_mode(game_mode_.get());

    start_local_session();

    game_clock_.restart();
    running_ = true;
}

void App::start_local_session()
{
    server_ = std::make_unique<Server>();
    server_->set_managers(&combat_, &world_state_, &quests_);
    server_->set_survival(&survival_);
    server_->set_event_simulator(events_.get());
    server_->set_game_mode(game_mode_.get());

    auto [srv, cli] = create_transport_pair();
    server_->attach_transport(std::move(srv));
    client_.attach_transport(std::move(cli));

    client_.send_join_request();
    session_mode_ = SessionMode::local;
}

awaitable<void> App::start_host_session(int port)
{
    session_mode_ = SessionMode::host;
    co_await server_->listen(port);
}

awaitable<void> App::start_client_session(std::string const &host, int port)
{
    // Resolve domain name if needed.
    std::string resolved_ip = host;
    try {
        boost::asio::ip::make_address(host);
    }
    catch (std::exception const &e) {
        spdlog::debug("Not an IP address ({}), resolving as hostname",
                      e.what());
        try {
            boost::asio::io_context io;
            boost::asio::ip::tcp::resolver resolver(io);
            auto results = resolver.resolve(host, std::to_string(port));
            for (auto const &ep : results) {
                resolved_ip = ep.endpoint().address().to_string();
                break;
            }
        }
        catch (std::exception const &e2) {
            spdlog::error("DNS resolution failed for {}: {}", host, e2.what());
        }
    }

    try {
        auto peer = co_await NetworkTransport::connect(resolved_ip, port);
        client_.attach_transport(std::move(peer));

        client_.send_join_request();
        session_mode_ = SessionMode::client;
    }
    catch (std::exception &e) {
        spdlog::error("Connect failed: {}, aborting", e.what());
    }
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
    spdlog::set_level(spdlog::level::debug);
    client_.reset();
    server_.reset();
    game_mode_.reset();
    ITransport::shutdown();
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
    bool in_dialogue = dialogue().active;
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
                           combat_.events(), client_.player_position(),
                           client_.local_player());
    combat_.clear_events();
    ui_manager_->render(world_state_, *this);
    SDL_RenderPresent(renderer_);
}

void App::set_ui_language(int lang_index)
{
    locale_.set_language(lang_index);
    dialogue_engine_.set_language(lang_index);
}

CombatStats const *App::player_combat_stats() const
{
    return const_cast<Client &>(client_).player_stats();
}

void App::quick_save()
{
    spdlog::warn("Unimplemented: quick save");
    return;
    save_to_slot(next_save_slot_++);
}

void App::save_to_slot(int slot)
{
    spdlog::warn("Save/load is currently disabled to prevent exploits and "
                 "bugs. It will be re-enabled in a future update.");
    (void)slot;
    // auto *pos =
    //     server_.entities().get_component<Position>(server_.host_player_id());
    // auto *cs =
    //     server_.entities().get_component<CombatStats>(server_.host_player_id());
    // Vec2f pp = pos ? pos->world_pos : Vec2f{};
    //
    // auto npcData = game_mode_->collect_npc_save_data();
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
    spdlog::warn("Save/load is currently disabled to prevent exploits and "
                 "bugs. It will be re-enabled in a future update.");
    (void)slot;
    return;

    std::string path = "saves/save_" + std::to_string(slot) + ".json";
    SaveManager::SaveData data;
    if (!SaveManager::load(path, data))
        return;

    auto pid = game_mode_->load_world(data);

    client_.reset();
    start_local_session();

    auto *pos = game_mode_->entities().get_component<Position>(pid);
    if (pos)
        camera_system_.center_on(pos->world_pos);
}

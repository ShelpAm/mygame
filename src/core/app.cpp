#include "core/app.hpp"

#include "core/game-mode.hpp"
#include "core/resource-manager.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "net/local-session.hpp"
#include "net/network-session.hpp"
#include "net/server.hpp"
#include "net/session.hpp"
#include "systems/render-system.hpp"
#include "ui/ui-manager.hpp"
#include <boost/asio.hpp>
#include <format>
#include <fstream>
#include <imgui.h>
#include <ranges>
#include <spdlog/spdlog.h>

App::App() : camera_system_(window_width_, window_height_)
{
}
App::~App()
{
    shutdown();
}

void App::init()
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%-8l%$] %v");
    // spdlog::flush_on(spdlog::level::trace);

    SDL_SetAppMetadata("The Sunset Straits App name", "1.0", "com.example.app-identifier");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_CAMERA))
        throw std::runtime_error("Failed to initialize SDL");

    if (!SDL_CreateWindowAndRenderer(window_title, window_width_, window_height_,
                                     SDL_WINDOW_RESIZABLE, &window_, &renderer_))
        throw std::runtime_error("Failed to create window and renderer");

    camera_system_.resize(window_width_, window_height_);
    SDL_SetRenderVSync(renderer_, 1);

    resources_ = std::make_unique<ResourceManager>();

    FontManager::init();
    fonts_ = std::make_unique<FontManager>(renderer_);

    // Load all knight animation frames
    constexpr std::string_view base = "assets/textures/CHIBI KNIGHT-PNG/CHIBI KNIGHT-PNG";
    struct Anim {
        std::string_view dir;
        std::string_view anim;
        std::string_view prefix;
        int count;
    };
    constexpr std::array<Anim, 5> anims{
        Anim{.dir = "01-Idle_", .anim = "Idle", .prefix = "knight_idle", .count = 8},
        Anim{.dir = "02-Run_", .anim = "Run", .prefix = "knight_run", .count = 8},
        Anim{.dir = "03-Attack_", .anim = "Attack", .prefix = "knight_attack", .count = 8},
        Anim{.dir = "05-Hurt_", .anim = "Hurt", .prefix = "knight_hurt", .count = 8},
        Anim{.dir = "06-Die_", .anim = "Die", .prefix = "knight_die", .count = 8},
    };
    for (auto const &a : anims) {
        for (int i = 0; i < a.count; ++i) {
            auto name = std::format("{}_{}", a.prefix, i);
            auto path = std::format("{}/{}/2D_KNIGHT__{}_{:03d}.png", base, a.dir, a.anim, i);
            resources_->load_texture(renderer_, name, path);
        }
    }

    for (auto i : std::views::iota(0, 16)) {
        auto s = std::to_string(i);
        resources_->load_texture(renderer_, "tile_" + s, "./assets/textures/tilemap/" + s + ".png");
    }

    resources_->load_texture(renderer_, "entity_dead", "./assets/textures/entity-dead.png");

    auto *default_font = fonts_->load_font("./assets/fonts/Monaspace Neon Var.ttf", 12.F);
    render_system_ =
        std::make_unique<RenderSystem>(renderer_, resources_.get(), &camera_system_, default_font);

    navigation_system_ = NavigationSystem{};
    std::vector<Vec2i> const no = {
        {-2, 1},   {-1, 0},   {-1, 1},   {-1, 2},   {0, -1},   {0, 0},     {0, 1},    {0, 2},
        {0, 3},    {1, -1},   {1, 0},    {1, 1},    {1, 2},    {2, -2},    {2, -1},   {2, 0},
        {2, 1},    {2, 2},    {3, -1},   {3, 0},    {3, 1},    {4, 0},     {4, 1},    {8, 6},
        {9, 5},    {9, 6},    {9, 7},    {10, 4},   {10, 5},   {10, 6},    {10, 7},   {10, 8},
        {11, 4},   {11, 5},   {11, 6},   {11, 7},   {12, 5},   {12, 6},    {12, 7},   {13, 5},
        {13, 6},   {-12, -8}, {-11, -9}, {-11, -8}, {-11, -7}, {-10, -10}, {-10, -9}, {-10, -8},
        {-10, -7}, {-10, -6}, {-9, -10}, {-9, -9},  {-9, -8},  {-9, -7},   {-8, -10}, {-8, -9},
        {-8, -8},  {-8, -7},  {-7, -9},  {-7, -8},  {-6, -8},  {15, -10},  {16, -11}, {16, -10},
        {16, -9},  {17, -12}, {17, -11}, {17, -10}, {17, -9},  {17, -8},   {18, -12}, {18, -11},
        {18, -10}, {18, -9},  {19, -11}, {19, -10}, {19, -9},  {20, -10},  {-5, -2},  {-4, -3},
        {-3, -4},  {4, 3},    {5, 4},    {5, 5},    {6, 4},    {6, 5},     {7, 5},    {13, 0},
        {14, -1},  {14, 0},   {14, 1},   {15, 0},   {15, 1},   {-5, 4},    {-6, 5},   {-7, 5},
        {-8, 6},   {-9, 5},   {-9, 6}};
    for (auto e : no)
        navigation_system_.set_walkable(e, false);

    ui_manager_ = std::make_unique<UIManager>(window_, renderer_, default_font);

    locale_.discover_languages("assets/locale");
    locale_.set_language(0);

    // Start IO thread
    io_thread_ = std::jthread([this]() {
        try {
            spdlog::info("IO thread started");
            io_.run();
            spdlog::info("IO thread stopped");
        }
        catch (std::exception const &e) {
            spdlog::error("IO thread error: {}", e.what());
        }
    });
    Session::set_io(&io_);

    server_ = std::make_unique<Server>();

    game_mode_ = std::make_unique<GameMode>();
    game_mode_->init_world();
    game_mode_->set_navigation(&navigation_system_);
    game_mode_->set_server(server_.get());
    server_->set_game_mode(game_mode_.get());

    game_mode_thread_ = std::jthread([this](std::stop_token const &st) {
        Stopwatch sw;
        while (!st.stop_requested()) {
            auto dt = sw.tick();
            // 只有在客户端模式时才update，节省计算资源
            if (session_mode_ != SessionMode::client)
                game_mode_->update(dt);
        }
    });

    client_ = std::make_unique<Client>(this);

    start_local_session();

    stopwatch_.restart();
    running_ = true;

    spdlog::info("App: initialization complete");
}

void App::start_host_session(int port)
{
    session_mode_ = SessionMode::host;
    start_listen(port);
}

void App::start_local_session()
{
    client_->close_current_session();

    auto [srv, cli] = create_local_transport_pair();
    spdlog::info("App: spawned two transports: srv = {}, cli = {}", srv->remote_info(),
                 cli->remote_info());

    auto do_attach = [](App *app, auto srv, auto cli) -> awaitable<void> {
        try {
            co_await (app->server_->attach_transport(std::move(srv)) &&
                      app->client_->attach_transport(std::move(cli)));
            app->client_->send_join_request();
            app->session_mode_ = SessionMode::local;
        }
        catch (std::exception const &e) {
            spdlog::error("App: failed to attach local transports: {}", e.what());
        }
    };
    Session::spawn(do_attach(this, srv, cli));
}

void App::start_client_session(std::string const &host, int port)
{
    client_->close_current_session();

    // Resolve domain name if needed.
    std::string resolved_ip = host;
    try {
        boost::asio::ip::make_address(host);
    }
    catch (std::exception const &e) {
        spdlog::debug("Not an IP address ({}), resolving as hostname", e.what());
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

    auto attach = [](App *app, std::string resolved_ip, auto port) -> awaitable<void> {
        try {
            auto t = co_await NetworkSession::connect(resolved_ip, port);
            co_await app->client_->attach_transport(std::move(t));
            app->client_->send_join_request();
            app->session_mode_ = SessionMode::client;
        }
        catch (std::exception &e) {
            spdlog::error("{}, falling back to local session", e.what());
            app->start_local_session();
        }
    };
    Session::spawn(attach(this, resolved_ip, port));
}

void App::run()
{
    while (running_) {
        spdlog::trace("App: main loop tick");
        auto dt = stopwatch_.tick();
        process_events();
        if (!running_)
            break;
        update(dt);
        render();
    }
}

void App::shutdown()
{
    // Shutdown already called or init failed, nothing to do
    if (window_ == nullptr)
        return;

    spdlog::info("App: shutting down");

    // 似乎不需要了，因为生命周期都是对的了，只要保证 io 比 client，game_mode
    // 先停。（因为 client，game_mode 没有用 shared_ptr...)
    // if (client_)
    //     client_->detach_transport();
    // if (game_mode_)
    //     game_mode_->server()->clear_transports();

    spdlog::info("App: stopping IO...");
    io_workguard_.reset();
    io_.stop();
    if (io_thread_.joinable())
        io_thread_.join();
    spdlog::info("App: IO stopped");

    // Reset after io thread stopped, otherwise coro in attach_transport will
    // use this after freed.
    // client_.reset();
    game_mode_thread_.request_stop();
    if (game_mode_thread_.joinable())
        game_mode_thread_.join();
    // game_mode_.reset();

    ui_manager_.reset();
    render_system_.reset();
    fonts_.reset();
    resources_.reset();

    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
    spdlog::info("App: SDL cleaned up");

    spdlog::info("App: shutdown complete");
}

void App::process_events()
{
    spdlog::trace("App: processing events");
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (ui_manager_->process_event(event))
            continue;
        switch (event.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            handle_resize(event.window.data1, event.window.data2);
            break;
        default:
            spdlog::warn("Unhandled SDL event type: {}", event.type);
        }
    }
    input_.update();
    spdlog::trace("App: finished processing events");
}

void App::update(float dt)
{
    spdlog::trace("App::update dt={} mode={}", dt, static_cast<int>(session_mode_));

    if (input_.just_pressed(InputManager::Action::help))
        ui_manager_->toggle_help();
    if (input_.just_pressed(InputManager::Action::multiplayer))
        ui_manager_->toggle_multiplayer();

    client_->update(dt);

    // Logged in to a server
    if (client_->player_id() != invalid_entity) {
        // Handles move
        float mx = 0;
        float my = 0;
        bool in_dialogue = dialogue().active;
        if (!ImGui::IsAnyItemActive() && !client_->is_player_dead() && !in_dialogue) {
            if (input_.is_pressed(InputManager::Action::move_up))
                my -= 1;
            if (input_.is_pressed(InputManager::Action::move_down))
                my += 1;
            if (input_.is_pressed(InputManager::Action::move_left))
                mx -= 1;
            if (input_.is_pressed(InputManager::Action::move_right))
                mx += 1;
        }
        float len = std::hypot(mx, my);
        static Vec2f last_sent_dir{0, 0};
        Vec2f dir = len > 0 ? Vec2f{mx / len, my / len} : Vec2f{0, 0};
        if (dir.x != last_sent_dir.x || dir.y != last_sent_dir.y) {
            client_->send_player_direction(dir);
            last_sent_dir = dir;
        }

        // Discrete actions — just_pressed fires once per key press
        if (input_.just_pressed(InputManager::Action::interact))
            client_->send_interact();
        if (input_.just_pressed(InputManager::Action::rest))
            client_->send_rest();
        if (input_.just_pressed(InputManager::Action::recruit))
            client_->send_recruit();
        if (input_.just_pressed(InputManager::Action::recruit_ranged))
            client_->send_recruit_ranged();
        if (input_.just_pressed(InputManager::Action::guard))
            client_->send_soldier_command();
        if (input_.just_pressed(InputManager::Action::respawn))
            client_->send_respawn();
        if (input_.just_pressed(InputManager::Action::cycle_formation))
            client_->send_cycle_formation();
        if (input_.just_pressed(InputManager::Action::select_melee))
            client_->toggle_selected_role(0);
        if (input_.just_pressed(InputManager::Action::select_ranged))
            client_->toggle_selected_role(1);
        if (input_.just_pressed(InputManager::Action::debug_toggle)) {
            static bool trace_on = false;
            trace_on = !trace_on;
            spdlog::set_level(trace_on ? spdlog::level::trace : spdlog::level::debug);
            spdlog::info("Log level: {}", trace_on ? "trace" : "debug");
            render_system_->toggle_debug_mode();
        }
        if (input_.just_pressed(InputManager::Action::quick_save))
            quick_save();
        if (input_.just_pressed(InputManager::Action::load_menu))
            ui_manager_->toggle_load_menu();

        camera_system_.set_target(client_->player_position());
    }

    camera_system_.update(dt);
    ui_manager_->update(dt);
}

void App::render()
{
    SDL_SetRenderDrawColor(renderer_, 10, 10, 15, 255);
    SDL_RenderClear(renderer_);

    // Connected to server and have a player entity
    if (client_->player_id() != invalid_entity) {
        render_system_->render(*client_, navigation_system_);
        client_->combat_events().clear();
    }
    ui_manager_->render(client_->player_id() == invalid_entity ? nullptr : &client_->world_state(),
                        *this);

    SDL_RenderPresent(renderer_);
}

void App::set_ui_language(int lang_index)
{
    locale_.set_language(lang_index);
    server_->set_language(lang_index);
}

void App::start_listen(int port)
{
    Session::spawn([](Server *s, auto port) -> awaitable<void> {
        co_await s->listen(port);
    }(server_.get(), port));
}

void App::stop_listen()
{
    server_->stop_listen();
    // Keeps current local client
    // Don't server_->clear_transports();
    for (auto const &s : server_->sessions()) {
        if (typeid(s.get()) == typeid(LocalSession *)) {
            server_->kick(s, "Host stopped the session");
        }
    }
}

std::vector<std::shared_ptr<Session>> const &App::server_sessions() const
{
    return server_->sessions();
}

void App::kick_session(std::shared_ptr<Session> s, std::string const &reason)
{
    server_->kick(std::move(s), reason);
}

void App::quick_save()
{
    spdlog::warn("Unimplemented: quick save");
    return;
    save_to_slot(next_save_slot_++);
}

void App::save_to_slot([[maybe_unused]] int slot)
{
    spdlog::warn("Save/load is currently disabled to prevent exploits and "
                 "bugs. It will be re-enabled in a future update.");
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

void App::load_from_slot([[maybe_unused]] int slot)
{
    spdlog::warn("Save/load is currently disabled to prevent exploits and "
                 "bugs. It will be re-enabled in a future update.");

    // std::string path = "saves/save_" + std::to_string(slot) + ".json";
    // SaveManager::SaveData data;
    // if (!SaveManager::load(path, data))
    //     return;
    //
    // auto pid = game_mode_->load_world(data);
    //
    // client_->detach_transport();
    // start_local_session();
    //
    // auto const *pos = game_mode_->get_position(pid);
    // if (pos)
    //     camera_system_.center_on(pos->world_pos);
}

void App::handle_resize(int new_width, int new_height)
{
    spdlog::info("Window resized to {}x{}", new_width, new_height);
    window_width_ = new_width;
    window_height_ = new_height;
    // TODO: Camera should keep unchanged, it's logical
    camera_system_.resize(window_width_, window_height_);
}

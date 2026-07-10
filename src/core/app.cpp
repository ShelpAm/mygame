#include "core/app.hpp"

#include "animation/animation-data.hpp"
#include "core/game-mode.hpp"
#include "core/resource-manager.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "net/local-session.hpp"
#include "net/network-session.hpp"
#include "net/server.hpp"
#include "net/session.hpp"
#include "systems/render-system.hpp"
#include "ui/ui-manager.hpp"
#include "world/terrain-generator.hpp"
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <rfl.hpp>
#include <rfl/yaml.hpp>
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

    init_sdl();
    init_graphics();
    init_io();
    init_world_data();
    init_ui();
    init_server();

    stopwatch_.restart();
    running_ = true;

    spdlog::info("App: initialization complete");
}

void App::init_sdl()
{
    if (!SDL_SetAppMetadata("The Sunset Straits App name", "1.0", "com.example.app-identifier"))
        throw std::runtime_error("Failed to set SDL app metadata");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_CAMERA))
        throw std::runtime_error("Failed to initialize SDL");

    if (!SDL_CreateWindowAndRenderer(window_title, window_width_, window_height_,
                                     SDL_WINDOW_RESIZABLE, &window_, &renderer_))
        throw std::runtime_error("Failed to create window and renderer");

    camera_system_.resize(window_width_, window_height_);
    SDL_SetRenderVSync(renderer_, 1);
}

void App::init_graphics()
{
    resources_ = std::make_unique<ResourceManager>();

    FontManager::init();
    fonts_ = std::make_unique<FontManager>(renderer_);

    load_textures();

    register_default_clips(*resources_);

    hud_font_ = fonts_->load_font("./assets/fonts/Monaspace Neon Var.ttf", 12.F);
    auto *cjk_font = fonts_->load_font("./assets/fonts/SourceHanSansCN-Regular.otf", 12.F);
    hud_font_->patch_fallback(*cjk_font);

    render_system_ = std::make_unique<RenderSystem>(window_, renderer_, resources_.get(),
                                                    &camera_system_, hud_font_);
}

void App::init_io()
{
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

    client_ = std::make_unique<Client>();
    client_->on_kicked_ = [this] { start_local_session(); };
    client_->set_resources(resources_.get());
}

void App::init_world_data()
{
    navigation_system_ = NavigationSystem{};

    load_terrain_from_json("assets/data/terrain.json", map_data_, navigation_system_);

    auto loc_data = load_locations_from_json("assets/data/locations.json");
    location_defs_ = std::move(loc_data.locations);
    generate_town_footprints(map_data_, navigation_system_, location_defs_, loc_data.routes);
    render_system_->set_map_data(&map_data_);
    render_system_->set_location_defs(&location_defs_);

    locale_.discover_languages("assets/locale");
    locale_.set_language("en");
    load_dialogue_templates();

    client_->set_location_defs(&location_defs_);
}

void App::init_ui()
{
    UIControl ui_ctrl;
    ui_ctrl.get_session_mode = [this] { return session_mode_; };
    ui_ctrl.get_fps = [this] { return stopwatch_.fps(); };
    ui_ctrl.do_dialogue_action = [this](auto const &a) { do_dialogue_action(a); };
    ui_ctrl.end_dialogue = [this] { end_dialogue(); };
    ui_ctrl.resolve_dialogue_text = [this](auto const &t, int i, auto const &s) {
        return resolve_dialogue_text(t, i, s);
    };
    ui_ctrl.start_local_session = [this] { start_local_session(); };
    ui_ctrl.start_host_session = [this](int p) { start_host_session(p); };
    ui_ctrl.start_client_session = [this](auto const &h, int p) { start_client_session(h, p); };
    ui_ctrl.stop_listen = [this] { stop_listen(); };
    ui_ctrl.set_session_mode = [this](SessionMode m) { set_session_mode(m); };
    ui_ctrl.server_sessions = [this]() -> std::vector<std::shared_ptr<Session>> const & {
        return server_sessions();
    };
    ui_ctrl.kick_session = [this](auto s, auto const &r) { kick_session(s, r); };
    ui_ctrl.available_save_slots = [this] { return available_save_slots(); };
    ui_ctrl.load_from_slot = [this](int s) { load_from_slot(s); };

    ui_manager_ = std::make_unique<UIManager>(render_system_.get(), hud_font_, *client_, locale_,
                                              std::move(ui_ctrl));
    ui_manager_->set_location_defs(&location_defs_);
}

void App::init_server()
{
    server_ = std::make_unique<Server>();

    game_mode_ = std::make_unique<GameMode>();
    game_mode_->set_map_data(&map_data_);
    game_mode_->set_location_defs(location_defs_);
    game_mode_->init_world();
    game_mode_->set_navigation(&navigation_system_);
    game_mode_->set_server(server_.get());
    server_->set_game_mode(game_mode_.get());

    game_mode_thread_ = std::jthread([this](std::stop_token const &st) {
        Stopwatch sw;
        while (!st.stop_requested()) {
            auto dt = sw.tick();
            if (session_mode_ != SessionMode::client)
                game_mode_->update(dt);
        }
    });

    start_local_session();
}

void App::load_textures()
{
    using Generic = rfl::Generic;
    using Obj = Generic::Object;

    auto read_file = [](std::string const &p) {
        std::ifstream f(p);
        return std::string{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    };

    auto parse = [&](std::string const &path) -> std::optional<Obj> {
        auto yaml_str = read_file(path);
        if (yaml_str.empty()) return std::nullopt;
        auto result = rfl::yaml::read<Generic>(yaml_str);
        if (!result) {
            spdlog::error("load_textures: failed to parse {}: {}", path, result.error().what());
            return std::nullopt;
        }
        return std::get<Obj>(result.value().get());
    };

    auto get_str = [](Obj const &o, std::string const &key) -> std::optional<std::string> {
        for (auto const &[k, v] : o)
            if (k == key)
                if (auto *s = std::get_if<std::string>(&v.get()))
                    return *s;
        return std::nullopt;
    };
    auto get_int = [](Obj const &o, std::string const &key) -> std::optional<int> {
        for (auto const &[k, v] : o)
            if (k == key)
                if (auto *i = std::get_if<int64_t>(&v.get()))
                    return static_cast<int>(*i);
        return std::nullopt;
    };

    auto root = parse("assets/data/textures.yaml");
    if (!root) return;

    // ── groups: individual frame PNGs ─────────────────────────────────────
    // Each {prefix}_{i}.png is loaded from the group's path.
    // Frame counts come from clips.yaml.
    auto clips = parse("assets/data/clips.yaml");
    if (!clips) return;

    for (auto const &[gk, gv] : *root) {
        if (gk != "groups") continue;
        for (auto const &[grp_name, grp_val] : std::get<Obj>(gv.get())) {
            auto const &g = std::get<Obj>(grp_val.get());
            auto path = get_str(g, "path");
            if (!path) continue;

            // Match group name in clips.yaml to get frame counts
            for (auto const &[ck, cv] : *clips) {
                if (ck != "groups") continue;
                for (auto const &[cn, cval] : std::get<Obj>(cv.get())) {
                    if (cn != grp_name) continue;
                    auto const &grp = std::get<Obj>(cval.get());
                    for (auto const &[ak, av] : grp) {
                        if (ak != "anims") continue;
                        for (auto const &a_val : std::get<std::vector<Generic>>(av.get())) {
                            auto const &a = std::get<Obj>(a_val.get());
                            auto prefix = get_str(a, "prefix");
                            auto frames = get_int(a, "frames");
                            if (!prefix || !frames) continue;
                            for (int i = 0; i < *frames; ++i) {
                                auto sprite = *prefix + "_" + std::to_string(i);
                                resources_->load_texture(renderer_, sprite,
                                    *path + "/" + sprite + ".png");
                            }
                        }
                    }
                }
            }
        }
    }

    // ── spritesheets: one PNG per anim, split horizontally ───────────────
    for (auto const &[gk, gv] : *root) {
        if (gk != "spritesheets") continue;
        for (auto const &[name, val] : std::get<Obj>(gv.get())) {
            auto const &g = std::get<Obj>(val.get());
            auto path = get_str(g, "path");
            auto fw = get_int(g, "frame_width");
            if (!path || !fw) continue;

            // Find the "sheets" sub-object
            for (auto const &[sk, sv] : g) {
                if (sk != "sheets") continue;
                for (auto const &[filename, spec] : std::get<Obj>(sv.get())) {
                    auto const &arr = std::get<std::vector<Generic>>(spec.get());
                    if (arr.size() < 2) continue;
                    auto prefix = std::get_if<std::string>(&arr[0].get());
                    auto frames = std::get_if<int64_t>(&arr[1].get());
                    if (!prefix || !frames) continue;
                    resources_->load_spritesheet(renderer_, *prefix,
                        *path + "/" + filename, *fw);
                }
            }
        }
    }

    // ── tiles: numbered textures from a directory ─────────────────────────
    for (auto const &[tk, tv] : *root) {
        if (tk != "tiles") continue;
        auto const &t = std::get<Obj>(tv.get());
        auto path = get_str(t, "path");
        auto count = get_int(t, "count").value_or(1);
        auto prefix = get_str(t, "prefix").value_or("tile_");
        if (!path) continue;
        for (int i = 0; i < count; ++i) {
            resources_->load_texture(renderer_, prefix + std::to_string(i),
                *path + "/" + std::to_string(i) + ".png");
        }
    }

    // ── singles: one-off texture files ────────────────────────────────────
    for (auto const &[sk, sv] : *root) {
        if (sk != "singles") continue;
        for (auto const &[name, val] : std::get<Obj>(sv.get())) {
            if (auto *path = std::get_if<std::string>(&val.get()))
                resources_->load_texture(renderer_, name, *path);
        }
    }

    // Load sprite definitions (sprite layer: maps sprite name → texture + clip)
    resources_->load_sprites("assets/data/sprites.yaml");
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
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
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

    client_->update(dt);

    if (client_->connected()) {
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
        if (dir != last_sent_dir) {
            client_->send_player_direction(dir);
            last_sent_dir = dir;
        }

        using Action = InputManager::Action;
        // 映射表：动作 → 对应的处理函数
        static std::unordered_map<Action, std::function<void()>> const handlers = {
            {Action::interact, [this]() { client_->send_interact(); }},
            {Action::rest, [this]() { client_->send_rest(); }},
            {Action::recruit, [this]() { client_->send_recruit(); }},
            {Action::recruit_ranged, [this]() { client_->send_recruit_ranged(); }},
            {Action::guard, [this]() { client_->send_soldier_command(); }},
            {Action::respawn, [this]() { client_->send_respawn(); }},
            {Action::cycle_formation, [this]() { client_->send_cycle_formation(); }},
            {Action::select_melee, [this]() { client_->toggle_selected_role(0); }},
            {Action::select_ranged, [this]() { client_->toggle_selected_role(1); }},
            {Action::toggle_log_level,
             []() {
                 static bool trace_on = false;
                 trace_on = !trace_on;
                 spdlog::set_level(trace_on ? spdlog::level::trace : spdlog::level::debug);
                 spdlog::info("Log level toggled, now is {}", trace_on ? "trace" : "debug");
             }},
            {Action::toggle_debug, [this]() { render_system_->toggle_debug_mode(); }},
            {Action::quick_save, [this]() { quick_save(); }},
            {Action::load_menu, [this]() { ui_manager_->toggle_load_menu(); }},
            {Action::help, [this]() { ui_manager_->toggle_help(); }},
            {Action::multiplayer, [this]() { ui_manager_->toggle_multiplayer(); }},
        };

        for (auto const &[action, handler] : handlers) {
            if (input_.just_pressed(action))
                handler();
        }

        camera_system_.set_target(client_->player_position());
    }

    camera_system_.update(dt);
    ui_manager_->update(dt);
}

void App::render()
{
    SDL_SetRenderDrawColor(renderer_, 10, 10, 15, 255);
    SDL_RenderClear(renderer_);

    if (client_->connected()) {
        render_system_->render(*client_);
        client_->combat_events().clear();
    }
    ui_manager_->render(client_->connected() ? &client_->world_state() : nullptr);

    SDL_RenderPresent(renderer_);
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

void App::load_dialogue_templates()
{
    try {
        for (auto const &entry : std::filesystem::directory_iterator("assets/dialogue")) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;

            std::ifstream file(entry.path().string());
            if (!file)
                continue;

            std::string content{std::istreambuf_iterator<char>(file), {}};
            auto parsed = boost::json::parse(content);
            auto &arr = parsed.as_object().at("templates").as_array();

            DialogueTemplateSet dts;
            for (auto const &item : arr) {
                auto const &obj = item.as_object();
                std::string type = std::string(obj.at("type").as_string());
                std::vector<std::string> texts;
                for (auto const &txt : obj.at("texts").as_array())
                    texts.push_back(std::string(txt.as_string()));
                dts.by_type.emplace(std::move(type), std::move(texts));
            }
            dialogue_template_sets_.push_back(std::move(dts));
        }
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to load dialogue templates: {}", e.what());
    }
}

std::string
App::resolve_dialogue_text(std::string const &template_type, int variant_index,
                           std::unordered_map<std::string, std::string> const &slots) const
{
    // Get the current template set
    int lang = dialogue_template_lang_;
    if (lang < 0 || lang >= static_cast<int>(dialogue_template_sets_.size()))
        lang = 0;

    auto const &dts = dialogue_template_sets_[lang];
    auto it = dts.by_type.find(template_type);
    if (it == dts.by_type.end()) {
        spdlog::warn("Dialogue template type '{}' not found for language {}", template_type, lang);
        return "[missing template: " + template_type + "]";
    }

    auto const &texts = it->second;
    if (variant_index < 0 || variant_index >= static_cast<int>(texts.size()))
        variant_index = 0;

    // Resolve slot values — locale-key prefixed values are looked up
    std::unordered_map<std::string, std::string> resolved;
    for (auto const &[key, val] : slots) {
        if (val.empty()) {
            resolved[key] = "";
        }
        else if (val.starts_with("topic.") || val.starts_with("fact.")) {
            // Locale key: look up the localized string
            resolved[key] = locale_.get(val);
        }
        else {
            // Raw string (e.g., person name)
            resolved[key] = val;
        }
    }

    std::string pattern = texts[variant_index];
    return fill_template(pattern, resolved);
}

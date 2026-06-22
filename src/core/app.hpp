#pragma once

#include "core/game-clock.hpp"
#include "core/game-mode.hpp"
#include "core/input-manager.hpp"
#include "core/locale-manager.hpp"
#include "net/client.hpp"
#include "systems/camera-system.hpp"
#include "systems/navigation-system.hpp"

#include <memory>
#include <SDL3/SDL.h>
#include <string>
#include <thread>
#include <vector>

class ResourceManager;
class RenderSystem;
class UIManager;
class Server;
struct CombatStats;

enum class SessionMode : std::uint8_t { local, host, client };

class App {
  public:
    App();
    ~App();
    void init();
    void run();
    void shutdown();

    LocaleManager const &locale() const { return locale_; }
    Client &client()
    {
        if (!client_)
            throw std::runtime_error("Client not initialized");
        return *client_;
    }
    Client const &client() const
    {
        if (!client_)
            throw std::runtime_error("Client not initialized");
        return *client_;
    }
    Stopwatch const &stopwatch() const { return stopwatch_; }
    SessionMode session_mode() const { return session_mode_; }
    void set_session_mode(SessionMode mode) { session_mode_ = mode; }

    void start_local_session();
    void start_host_session(int port);
    void start_client_session(std::string const &host, int port);

    void start_listen(int port);
    void stop_listen();
    std::vector<std::shared_ptr<Session>> const &server_sessions() const;
    void kick_session(std::shared_ptr<Session> s, std::string const &reason);

    void set_ui_language(int lang_index);
    DialogueState const &dialogue() const { return client_->dialogue(); }
    void end_dialogue() { client_->send_dialogue_action("__end__"); }
    void ask_topic(std::string const &t) { do_dialogue_action(t); }
    void do_dialogue_action(std::string const &a)
    {
        client_->send_dialogue_action(a);
    }

    void quick_save();
    void save_to_slot(int slot);
    void load_from_slot(int slot);
    std::vector<int> available_save_slots() const;

  private:
    void process_events();
    void update(
        float dt); // Receives messages from server, and trigger related updates
    void render();

    void handle_resize(int new_width, int new_height);

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;

    int window_width_ = 800;
    int window_height_ = 450;
    static constexpr const char *window_title = "The Sunset Straits";

    Stopwatch stopwatch_;

    InputManager input_;
    CameraSystem camera_system_;
    NavigationSystem navigation_system_;
    LocaleManager locale_;

    std::unique_ptr<ResourceManager> resources_;
    std::unique_ptr<RenderSystem> render_system_;
    std::unique_ptr<UIManager> ui_manager_;
    asio::io_context io_;
    decltype(asio::make_work_guard(io_)) io_workguard_ =
        asio::make_work_guard(io_);
    std::jthread io_thread_;

    GameMode &game_mode()
    {
        if (!game_mode_)
            throw std::runtime_error("Game mode not initialized");
        return *game_mode_;
    }

    // GameMode 本应运行在另一进程，和client互不影响的，现在只分离线程
    std::unique_ptr<GameMode> game_mode_;
    std::jthread game_mode_thread_;

    std::unique_ptr<Server> server_;

    std::unique_ptr<Client> client_;

    SessionMode session_mode_ = SessionMode::local;

    bool running_ = false;
    int next_save_slot_ = 1;
};

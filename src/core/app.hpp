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
struct CombatStats;

enum class SessionMode { local, host, client };

class App {
  public:
    App();
    ~App();
    void init();
    void run();
    void shutdown();

    auto const &game_clock() const
    {
        return game_clock_;
    }
    LocaleManager const &locale() const
    {
        return locale_;
    }
    bool show_load_menu() const
    {
        return show_load_menu_;
    }
    void set_show_load_menu(bool v)
    {
        show_load_menu_ = v;
    }
    bool show_help() const
    {
        return show_help_;
    }
    void set_show_help(bool v)
    {
        show_help_ = v;
    }
    bool show_multiplayer() const
    {
        return show_multiplayer_;
    }
    void set_show_multiplayer(bool v)
    {
        show_multiplayer_ = v;
    }
    asio::io_context &io()
    {
        return io_;
    }
    GameMode &game_mode()
    {
        if (!game_mode_)
            throw std::runtime_error("Game mode not initialized");
        return *game_mode_;
    }
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

    void start_local_session();

    SessionMode session_mode() const
    {
        return session_mode_;
    }

    void start_host_session(int port);
    void start_client_session(std::string const &ip, int port);

    void set_ui_language(int lang_index);
    DialogueState const &dialogue() const
    {
        return session_mode_ == SessionMode::client
                   ? client_->dialogue()
                   : game_mode_->dialogue(client_->player_id());
    }
    void end_dialogue()
    {
        if (session_mode_ == SessionMode::client)
            client_->send_dialogue_action("__end__");
        else
            game_mode_->end_dialogue(client_->player_id());
    }
    void ask_topic(std::string const &t)
    {
        do_dialogue_action(t);
    }
    void do_dialogue_action(std::string const &a)
    {
        if (session_mode_ == SessionMode::client)
            client_->send_dialogue_action(a);
        else
            game_mode_->do_dialogue_action(client_->player_id(), a);
    }

    void quick_save();
    void save_to_slot(int slot);
    void load_from_slot(int slot);
    std::vector<int> available_save_slots() const;

  private:
    void process_events();
    void update(float dt);
    void render();

    void handle_resize(int new_width, int new_height);

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;

    int window_width_ = 800;
    int window_height_ = 450;
    static constexpr char const *window_title = "The Sunset Straits";

    InputManager input_;
    GameClock game_clock_;
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

    std::unique_ptr<GameMode> game_mode_;
    std::unique_ptr<Client> client_;

    SessionMode session_mode_ = SessionMode::local;

    bool show_load_menu_ = false;
    bool show_help_ = false;
    bool show_multiplayer_ = false;
    bool running_ = false;
    int next_save_slot_ = 1;
};

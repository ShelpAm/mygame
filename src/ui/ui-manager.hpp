#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>

class WorldState;
class App;
class LocaleManager;

class UIManager {
  public:
    UIManager(SDL_Window *window, SDL_Renderer *renderer);
    ~UIManager();

    /// @return If recognized and handled
    bool process_event(SDL_Event const &event);
    void update(float dt);
    void render(WorldState *world_state, App &app);

    bool show_help() const { return show_help_; }
    void toggle_help() { show_help_ = !show_help_; }
    bool show_load_menu() const { return show_load_menu_; }
    void toggle_load_menu() { show_load_menu_ = !show_load_menu_; }
    bool show_multiplayer() const { return show_multiplayer_; }
    void toggle_multiplayer() { show_multiplayer_ = !show_multiplayer_; }

  private:
    [[maybe_unused]] SDL_Window *window_;
    SDL_Renderer *renderer_;
    bool show_journal_ = false;
    bool show_inventory_ = false;
    bool show_map_ = false;
    bool show_load_menu_ = false;
    bool show_help_ = false;
    bool show_multiplayer_ = false;

    void render_hud(WorldState const &world_state, App &app);
    void render_journal(App const &app);
    void render_inventory(App const &app);
    void render_map(App const &app);
    void render_dialogue(App const &app);
    void render_help_panel(App const &app);
    void render_load_menu(App const &app);
    void render_multiplayer_menu(App const &app);

    void render_hosting(App &app);
    void render_local(App &app);
    void render_client(App &app);
    void render_client_list(App &app);
    void render_chat(App &app);

    std::string host_ip_ = "127.0.0.1";
    int host_port_ = 27015;
    std::string chat_buf_;
    bool chat_active_ = false;
    std::vector<std::string> server_list_;
    void load_server_list();
    void save_server_list();
};

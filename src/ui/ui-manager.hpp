#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>

class WorldState;
class App;
class LocaleManager;
class GameMode;

class UIManager {
  public:
    UIManager(SDL_Window *window, SDL_Renderer *renderer);
    ~UIManager();

    void process_event(SDL_Event const &event);
    void update(float dt);
    void render(WorldState const &world_state, App const &app);

  private:
    SDL_Window *window_;
    SDL_Renderer *renderer_;
    bool show_journal_ = false;
    bool show_inventory_ = false;
    bool show_map_ = false;

    void render_hud(WorldState const &world_state, App const &app);
    void render_journal(App const &app);
    void render_inventory(App const &app);
    void render_map(App const &app);
    void render_dialogue(App const &app);
    void render_help_panel(App const &app);
    void render_load_menu(App const &app);
    void render_multiplayer_menu(App const &app);
    char host_ip_[64] = "127.0.0.1";
    int host_port_ = 27015;
    char chat_buf_[256] = {};
    bool chat_active_ = false;
    std::vector<std::string> server_list_;
    void load_server_list();
    void save_server_list();
};

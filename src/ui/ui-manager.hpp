#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <string>

class WorldState;
class App;
class LocaleManager;
class GameMode;

class UIManager {
public:
    UIManager(SDL_Window* window, SDL_Renderer* renderer);
    ~UIManager();

    void process_event(const SDL_Event& event);
    void update(float dt);
    void render(const WorldState& world_state, const App& app);

private:
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    bool show_journal_ = false;
    bool show_inventory_ = false;
    bool show_map_ = false;

    void render_hud(const WorldState& world_state, const App& app);
    void render_journal(const App& app);
    void render_inventory(const App& app);
    void render_map(const App& app);
    void render_dialogue(const App& app);
    void render_help_panel(const App& app);
    void render_load_menu(const App& app);
    void render_multiplayer_menu(const App& app);
    char host_ip_[32] = "127.0.0.1";
    char chat_buf_[256] = {};
    bool chat_active_ = false;
    std::vector<std::string> server_list_;
    void load_server_list();
    void save_server_list();
};

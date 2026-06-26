#pragma once

#include "core/font.hpp"
#include "world/location-store.hpp"
#include <SDL3/SDL.h>
#include <string>
#include <unordered_set>
#include <vector>

class App;
class WorldState;
class LocaleManager;
class RenderSystem;

class UIManager {
  public:
    UIManager(UIManager const &) = default;
    UIManager(UIManager &&) = delete;
    UIManager &operator=(UIManager const &) = default;
    UIManager &operator=(UIManager &&) = delete;
    UIManager(RenderSystem *rs, Font *hud_font);
    ~UIManager();

    /// @return If recognized and handled
    bool process_event(SDL_Event const &event);
    void update(float dt);
    void render(WorldState *world_state, App &app);

    void toggle_help() { show_help_ = !show_help_; }
    void toggle_load_menu() { show_load_menu_ = !show_load_menu_; }
    void toggle_multiplayer() { show_multiplayer_ = !show_multiplayer_; }

    void set_location_defs(std::vector<LocationDefinition> const *defs) { location_defs_ = defs; }

  private:
    RenderSystem *render_system_;
    Font *hud_font_;
    bool show_journal_ = false;
    bool show_inventory_ = false;
    bool show_map_ = false;
    bool show_load_menu_ = false;
    bool show_help_ = false;
    bool show_multiplayer_ = false;

    void render_hud_window(WorldState const &world_state, App &app);
    void render_hud_text(WorldState const &world_state, App &app);
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

    struct DiscoveryNotification {
        std::string locale_key;
        float timer = 0.F;
    };
    std::vector<DiscoveryNotification> discovery_queue_;
    std::vector<LocationDefinition> const *location_defs_ = nullptr;
    std::unordered_set<std::string> notified_town_ids_;

    void load_server_list();
    void save_server_list();
};

#pragma once

#include "core/game-clock.hpp"
#include "core/game-mode.hpp"
#include "core/input-manager.hpp"
#include "core/locale-manager.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "factions/faction-network.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "net/client.hpp"
#include "net/network-transport.hpp"
#include "net/server.hpp"
#include "survival/condition-tracker.hpp"
#include "systems/camera-system.hpp"
#include "systems/combat-system.hpp"
#include "systems/navigation-system.hpp"
#include "systems/quest-manager.hpp"
#include "world/world-state.hpp"

#include <memory>
#include <SDL3/SDL.h>
#include <string>
#include <vector>

class ResourceManager;
class RenderSystem;
class UIManager;
class EventSimulator;
class RumorPropagator;
struct CombatStats;

enum class SessionMode { local, host, client };

class App {
  public:
    App();
    ~App();
    void init();
    void run();
    void shutdown();

    LocaleManager const &locale() const
    {
        return locale_;
    }
    ConditionTracker const &survival() const
    {
        return survival_;
    }
    CombatStats const *player_combat_stats() const;
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
    Client &client()
    {
        return client_;
    }
    Client const &client() const
    {
        return client_;
    }
    QuestManager &quests_mut()
    {
        return quests_;
    }
    QuestManager const &quests() const
    {
        return quests_;
    }
    GameMode &game_mode()
    {
        return *game_mode_;
    }
    GameMode const &game_mode() const
    {
        return *game_mode_;
    }

    SessionMode session_mode() const
    {
        return session_mode_;
    }

    void start_local_session();
    awaitable<void> start_host_session(int port);
    awaitable<void> start_client_session(std::string const &ip, int port);

    void set_ui_language(int lang_index);
    DialogueState const &dialogue() const
    {
        return session_mode_ == SessionMode::client ? client_.dialogue()
                                                     : game_mode_->dialogue();
    }
    void end_dialogue()
    {
        if (session_mode_ == SessionMode::client)
            client_.send_dialogue_action("__end__");
        game_mode_->end_dialogue();
    }
    void ask_topic(std::string const &t)
    {
        do_dialogue_action(t);
    }
    void do_dialogue_action(std::string const &a)
    {
        if (session_mode_ == SessionMode::client)
            client_.send_dialogue_action(a);
        game_mode_->do_dialogue_action(a);
    }

    void quick_save();
    void save_to_slot(int slot);
    void load_from_slot(int slot);
    std::vector<int> available_save_slots() const;

  private:
    void process_events();
    void update(float dt);
    void render();

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;

    // Plain members
    InputManager input_;
    GameClock game_clock_;
    CameraSystem camera_system_{window_width, window_height};
    NavigationSystem navigation_system_;
    WorldState world_state_;
    KnowledgeGraph knowledge_;
    DialogueEngine dialogue_engine_;
    LocaleManager locale_;
    ConditionTracker survival_;
    FactionNetwork factions_;
    CombatSystem combat_;
    QuestManager quests_;
    Client client_;

    // Must stay unique_ptr — runtime deps or recreated
    std::unique_ptr<Server> server_;
    std::unique_ptr<ResourceManager> resources_;
    std::unique_ptr<RenderSystem> render_system_;
    std::unique_ptr<UIManager> ui_manager_;
    std::unique_ptr<EventSimulator> events_;
    std::unique_ptr<RumorPropagator> rumors_;
    std::unique_ptr<GameMode> game_mode_;

    SessionMode session_mode_ = SessionMode::local;

    bool show_load_menu_ = false;
    bool show_help_ = false;
    bool show_multiplayer_ = false;
    bool running_ = false;
    int next_save_slot_ = 1;

    static constexpr int window_width = 960;
    static constexpr int window_height = 540;
    static constexpr char const *window_title = "The Sunset Straits";
};

#pragma once

#include <SDL3/SDL.h>
#include "core/game-types.hpp"
#include "core/locale-manager.hpp"
#include <memory>
#include <string>
#include <vector>

class ResourceManager;
class InputManager;
class GameClock;
class RenderSystem;
class CameraSystem;
class NavigationSystem;
class EntityManager;
class UIManager;
class WorldState;
class KnowledgeGraph;
class DialogueEngine;
class TopicRegistry;
class RelationshipTable;
class ConditionTracker;
class FactionNetwork;
class EventSimulator;
class RumorPropagator;
class CombatSystem;
class QuestManager;
class NetworkManager;
class Server;
class Client;
#include "core/game-mode.hpp"
struct CombatStats;

class App {
public:
    App();
    ~App();
    bool init();
    void run();
    void shutdown();

    SDL_Window* window() { return window_; }
    SDL_Renderer* renderer() { return renderer_; }
    InputManager& input() { return *input_; }
    LocaleManager& locale() { return *locale_; }
    const LocaleManager& locale() const { return *locale_; }
    const ConditionTracker& survival() const { return *survival_; }
    void do_rest();
    void recruit_soldier();
    const CombatStats* player_combat_stats() const;
    bool is_player_dead() const;
    bool show_load_menu() const { return show_load_menu_; }
    void set_show_load_menu(bool v) { show_load_menu_ = v; }
    bool show_help() const { return show_help_; }
    void set_show_help(bool v) { show_help_ = v; }
    bool show_multiplayer() const { return show_multiplayer_; }
    void set_show_multiplayer(bool v) { show_multiplayer_ = v; }
    NetworkManager* network_mut() { return network_.get(); }
    const NetworkManager* network() const { return network_.get(); }
    QuestManager& quests_mut() { return *quests_; }
    const QuestManager& quests() const { return *quests_; }
    GameMode& game_mode() { return *game_mode_; }
    const GameMode& game_mode() const { return *game_mode_; }

    void set_ui_language(int lang_index);
    void end_dialogue() { game_mode_->end_dialogue(); }
    void ask_topic(const std::string& t) { game_mode_->do_dialogue_action(t, *locale_); }
    void do_dialogue_action(const std::string& a) { game_mode_->do_dialogue_action(a, *locale_); }

    void quick_save();
    void save_to_slot(int slot);
    void load_from_slot(int slot);
    std::vector<int> available_save_slots() const;

private:
    void process_events();
    void update(float dt);
    void render();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::unique_ptr<ResourceManager> resources_;
    std::unique_ptr<InputManager> input_;
    std::unique_ptr<GameClock> game_clock_;
    std::unique_ptr<RenderSystem> render_system_;
    std::unique_ptr<CameraSystem> camera_system_;
    std::unique_ptr<NavigationSystem> navigation_system_;
    std::unique_ptr<EntityManager> entity_manager_;
    std::unique_ptr<UIManager> ui_manager_;
    std::unique_ptr<WorldState> world_state_;
    std::unique_ptr<KnowledgeGraph> knowledge_;
    std::unique_ptr<DialogueEngine> dialogue_engine_;
    std::unique_ptr<TopicRegistry> topic_registry_;
    std::unique_ptr<RelationshipTable> relationships_;
    std::unique_ptr<LocaleManager> locale_;
    std::unique_ptr<ConditionTracker> survival_;
    std::unique_ptr<FactionNetwork> factions_;
    std::unique_ptr<EventSimulator> events_;
    std::unique_ptr<RumorPropagator> rumors_;
    std::unique_ptr<CombatSystem> combat_;
    std::unique_ptr<QuestManager> quests_;
    std::unique_ptr<NetworkManager> network_;
    std::unique_ptr<GameMode> game_mode_;
    std::unique_ptr<Server> server_;
    std::unique_ptr<Client> client_;

    bool show_load_menu_ = false;
    bool show_help_ = false;
    bool show_multiplayer_ = false;
    bool running_ = false;
    int next_save_slot_ = 1;

    static constexpr int window_width = 1280;
    static constexpr int window_height = 720;
    static constexpr const char* window_title = "The Sunset Straits";
};

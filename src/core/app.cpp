#include "core/app.hpp"
#include "core/game-mode.hpp"
#include "core/resource-manager.hpp"
#include "core/input-manager.hpp"
#include "core/game-clock.hpp"
#include "systems/render-system.hpp"
#include "systems/camera-system.hpp"
#include "systems/combat-system.hpp"
#include "systems/navigation-system.hpp"
#include "entities/entity-manager.hpp"
#include "entities/components/combat-stats.hpp"
#include "dialogue/dialogue-engine.hpp"
#include "dialogue/topic-registry.hpp"
#include "dialogue/relationship-table.hpp"
#include "factions/faction-network.hpp"
#include "factions/event-simulator.hpp"
#include "knowledge/rumor-propagator.hpp"
#include "survival/condition-tracker.hpp"
#include "save/save-manager.hpp"
#include "systems/quest-manager.hpp"
#include "net/network-manager.hpp"
#include "net/server.hpp"
#include "net/client.hpp"
#include "ui/ui-manager.hpp"
#include "world/world-state.hpp"
#include <imgui.h>
#include <fstream>

App::App() = default;
App::~App() { shutdown(); }

bool App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) return false;
    window_ = SDL_CreateWindow(window_title, window_width, window_height, SDL_WINDOW_RESIZABLE);
    if (!window_) return false;
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) return false;
    SDL_SetRenderVSync(renderer_, 1);

    resources_ = std::make_unique<ResourceManager>();
    input_ = std::make_unique<InputManager>();
    game_clock_ = std::make_unique<GameClock>();
    entity_manager_ = std::make_unique<EntityManager>();
    world_state_ = std::make_unique<WorldState>();
    knowledge_ = std::make_unique<KnowledgeGraph>();
    dialogue_engine_ = std::make_unique<DialogueEngine>();
    topic_registry_ = std::make_unique<TopicRegistry>();
    relationships_ = std::make_unique<RelationshipTable>();
    locale_ = std::make_unique<LocaleManager>();
    survival_ = std::make_unique<ConditionTracker>();
    factions_ = std::make_unique<FactionNetwork>();
    camera_system_ = std::make_unique<CameraSystem>(window_width, window_height);
    render_system_ = std::make_unique<RenderSystem>(renderer_, *resources_, *camera_system_);
    navigation_system_ = std::make_unique<NavigationSystem>();
    ui_manager_ = std::make_unique<UIManager>(window_, renderer_);
    combat_ = std::make_unique<CombatSystem>();
    quests_ = std::make_unique<QuestManager>();
    network_ = std::make_unique<NetworkManager>();
    server_ = std::make_unique<Server>();
    client_ = std::make_unique<Client>();
    server_->set_managers(combat_.get(), world_state_.get(), quests_.get());
    client_->set_managers(combat_.get(), world_state_.get(), quests_.get());

    locale_->discover_languages("assets/locale");
    locale_->set_language(0);
    dialogue_engine_->discover_languages("assets/dialogue");
    quests_->load_from_json("assets/data/quests.json");

    // Network callback
    network_->set_callback([this](const NetMessage& msg) {
        if (msg.type == NetMessage::recruit_soldier) {
            float px, py; memcpy(&px, msg.data.data(), 4); memcpy(&py, msg.data.data()+4, 4);
            auto eid = game_mode_->spawn_soldier(game_mode_->player_entity(), rand(), {0,-1});
            auto* pos = server_->entities().get_component<Position>(eid);
            if (pos) pos->world_pos = {px + 32.f, py + 32.f};  // Near requesting player
        } else if (msg.type == NetMessage::spawn_enemy_wave) {
            float cx, cy; int cnt; uint8_t tm;
            memcpy(&cx, msg.data.data(), 4); memcpy(&cy, msg.data.data()+4, 4);
            memcpy(&cnt, msg.data.data()+8, 4); tm = msg.data[12];
            combat_->spawn_enemy_wave(server_->entities(), cnt, {cx, cy}, 400.f, (Team)tm);
        } else if (msg.type == NetMessage::entity_update && msg.data.size() >= 21) {
            // Remote player position update
            int id; float x, y; int hp, max_hp; uint8_t alive;
            memcpy(&id, msg.data.data(), 4);
            memcpy(&x, msg.data.data()+4, 4); memcpy(&y, msg.data.data()+8, 4);
            memcpy(&hp, msg.data.data()+12, 4); memcpy(&max_hp, msg.data.data()+16, 4);
            alive = msg.data[20];
            // Create/update remote player entity on server
            if (!server_->remote_player(id)) {
                server_->add_remote_player(id, {x, y});
            } else {
                server_->update_remote_player(id, {x, y}, hp, max_hp, (bool)alive);
            }
        } else if (msg.type == NetMessage::combat_event && msg.data.size() >= 13) {
            auto read = [&](int o) { int v; memcpy(&v, msg.data.data()+o, 4); return v; };
            int att = read(0), def = read(4), dmg = read(8); bool k = msg.data[12];
            if (server_) server_->handle_combat_event(att, def, dmg, k);
            if (client_) client_->handle_combat_event(att, def, dmg, k);
        }
    });

    events_ = std::make_unique<EventSimulator>(*factions_, *knowledge_, *world_state_);
    rumors_ = std::make_unique<RumorPropagator>(*knowledge_);
    game_mode_ = std::make_unique<GameMode>(server_->entities());
    game_mode_->init_world(*world_state_, *knowledge_, *dialogue_engine_,
                          *topic_registry_, *relationships_, *factions_,
                          *events_, *rumors_, *combat_, *quests_, *network_);

    game_clock_->restart();
    running_ = true;
    return true;
}

void App::run() {
    while (running_) {
        float dt = game_clock_->tick();
        process_events();
        if (!running_) break;
        update(dt);
        render();
    }
}

void App::shutdown() {
    running_ = false;
    // Stop networking first, then destroy game logic, then render/IO
    if (network_) { network_->disconnect(); network_.reset(); }
    combat_.reset();
    events_.reset();
    game_mode_.reset();
    quests_.reset();
    game_mode_.reset();
    combat_.reset();
    quests_.reset();
    events_.reset();
    factions_.reset();
    rumors_.reset();
    knowledge_.reset();
    dialogue_engine_.reset();
    topic_registry_.reset();
    relationships_.reset();
    locale_.reset();
    survival_.reset();
    ui_manager_.reset();
    render_system_.reset();
    camera_system_.reset();
    navigation_system_.reset();
    entity_manager_.reset();
    world_state_.reset();
    input_.reset();
    resources_.reset();
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    SDL_Quit();
}

void App::process_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ui_manager_->process_event(event);
        switch (event.type) {
            case SDL_EVENT_QUIT: running_ = false; break;
            case SDL_EVENT_WINDOW_RESIZED:
                camera_system_->resize(event.window.data1, event.window.data2); break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat) break;
                switch (event.key.scancode) {
                    case SDL_SCANCODE_E: if (!is_player_dead()) game_mode_->handle_interaction(*locale_); break;
                    case SDL_SCANCODE_R: do_rest(); break;
                    case SDL_SCANCODE_F1: show_help_ = !show_help_; break;
                    case SDL_SCANCODE_F2: recruit_soldier(); break;
                    case SDL_SCANCODE_F5: quick_save(); break;
                    case SDL_SCANCODE_F9: show_load_menu_ = !show_load_menu_; break;
                    case SDL_SCANCODE_F10: show_multiplayer_ = !show_multiplayer_; break;
                    default: break;
                }
                break;
        }
    }
    input_->update();
}

void App::update(float dt) {
    survival_->update(dt, false, false);
    world_state_->update(dt);
    events_->update(world_state_->day());

    // Network handling
    if (network_->is_connected()) {
        network_->update();
        if (network_->is_hosting()) {
            server_->update(dt, *network_);
            if (server_->check_needs_full_sync()) {
                server_->send_full_state(*network_);
            }
        } else {
            // Client: receive sync
            client_->update(dt, *network_);
        }
        network_->interpolate_entities(dt);
    }

    // Single-player or host: spawn enemies from events
    if (!network_->is_connected() || network_->is_hosting()) {
        static size_t s_lastCount = 0;
        auto cnt = events_->triggered_events().size();
        if (cnt > s_lastCount) { s_lastCount = cnt;
            auto* pos = server_->entities().get_component<Position>(game_mode_->player_entity());
            Vec2f center = pos ? pos->world_pos : Vec2f{};
            auto& latest = events_->triggered_events().back();
            if (latest.type == GameEvent::Type::battle || latest.type == GameEvent::Type::refugee_wave)
                combat_->spawn_enemy_wave(server_->entities(), 2 + rand() % 4, center, 400.f, Team::enemy);
        }
    }

    if (survival_->state().food <= 0 || survival_->state().water <= 0) {
        auto* pcs = server_->entities().get_component<CombatStats>(game_mode_->player_entity());
        if (pcs && pcs->alive) pcs->hp = std::max(0, pcs->hp - 1);
    }

    game_mode_->is_client = (network_->is_connected() && !network_->is_hosting());
    game_mode_->update(dt, *input_, *locale_, *world_state_);

    // Client: move local player and send to server
    if (network_->is_connected() && !network_->is_hosting()) {
        Vec2f p = client_->local_player_pos();
        float mx = 0, my = 0;
        if (!ImGui::IsAnyItemActive()) {
            if (input_->is_pressed(InputManager::Action::move_up)) my -= 1;
            if (input_->is_pressed(InputManager::Action::move_down)) my += 1;
            if (input_->is_pressed(InputManager::Action::move_left)) mx -= 1;
            if (input_->is_pressed(InputManager::Action::move_right)) mx += 1;
        }
        if (mx != 0 || my != 0) {
            float len = std::hypot(mx, my);
            client_->set_local_player_pos({p.x + mx/len * 200.f * dt, p.y + my/len * 200.f * dt});
        }
        camera_system_->set_target(client_->local_player_pos());
        auto* cs = client_->entities().get_component<CombatStats>(client_->local_player());
        network_->send_entity_update(0, client_->local_player_pos(), cs ? cs->hp : 20, cs ? cs->max_hp : 20, cs ? cs->alive : true);
    } else {
        auto* ppos = server_->entities().get_component<Position>(game_mode_->player_entity());
        if (ppos) camera_system_->set_target(ppos->world_pos);
    }
    camera_system_->update(dt);

    ui_manager_->update(dt);
}

void App::render() {
    SDL_SetRenderDrawColor(renderer_, 10, 10, 15, 255);
    SDL_RenderClear(renderer_);
    // Render from server (single/host) or client (join)
    auto& renderEm = (network_->is_connected() && !network_->is_hosting())
        ? client_->entities() : server_->entities();
    render_system_->render(renderEm, *world_state_, *navigation_system_,
                            combat_->events(), network_->remote_entities());
    combat_->clear_events();
    ui_manager_->render(*world_state_, *this);
    SDL_RenderPresent(renderer_);
}

void App::set_ui_language(int lang_index) {
    locale_->set_language(lang_index);
    dialogue_engine_->set_language(lang_index);
}

bool App::is_player_dead() const {
    auto* cs = server_->entities().get_component<CombatStats>(game_mode_->player_entity());
    return cs && !cs->alive;
}

const CombatStats* App::player_combat_stats() const {
    return server_->entities().get_component<CombatStats>(game_mode_->player_entity());
}

void App::do_rest() {
    auto* pcs = server_->entities().get_component<CombatStats>(game_mode_->player_entity());
    if (pcs && pcs->alive) {
        pcs->hp = std::min(pcs->max_hp, pcs->hp + 5);
        survival_->heal(5.f);
    }
}

void App::quick_save() { save_to_slot(next_save_slot_++); }

void App::save_to_slot(int slot) {
    auto* pos = server_->entities().get_component<Position>(game_mode_->player_entity());
    auto* cs = server_->entities().get_component<CombatStats>(game_mode_->player_entity());
    Vec2f pp = pos ? pos->world_pos : Vec2f{};

    std::vector<SaveManager::NPCData> npcData;
    for (auto eid : game_mode_->npc_entities()) {
        auto* np = server_->entities().get_component<Position>(eid);
        auto* ns = server_->entities().get_component<NPCState>(eid);
        auto* ncs = server_->entities().get_component<CombatStats>(eid);
        if (!np || !ns) continue;
        SaveManager::NPCData nd;
        nd.id = ns->npc_id; nd.name = ns->display_name; nd.personality = ns->personality;
        nd.position = np->world_pos; nd.hp = ncs ? ncs->hp : 10; nd.max_hp = ncs ? ncs->max_hp : 10;
        nd.alive = ncs ? ncs->alive : true;
        for (const auto& [fid, kf] : ns->knowledge) nd.knowledge[fid] = kf.npc_version;
        npcData.push_back(std::move(nd));
    }
    SaveManager::save("saves/save_" + std::to_string(slot) + ".json", *world_state_,
                      *knowledge_, *relationships_, pp, cs ? cs->hp : 20, cs ? cs->max_hp : 20, npcData);
}

std::vector<int> App::available_save_slots() const {
    std::vector<int> slots;
    for (int i = 1; i <= 10; ++i) {
        std::ifstream f("saves/save_" + std::to_string(i) + ".json");
        if (f.good()) slots.push_back(i);
    }
    return slots;
}

void App::load_from_slot(int slot) {
    std::string path = "saves/save_" + std::to_string(slot) + ".json";
    SaveManager::SaveData data;
    if (!SaveManager::load(path, data)) return;

    // Reset
    while (!server_->entities().all_entities().empty())
        server_->entities().destroy_entity(server_->entities().all_entities().back());
    game_mode_ = std::make_unique<GameMode>(*entity_manager_);

    auto pid = game_mode_->spawn_player(data.player_pos.x, data.player_pos.y);
    auto* cs = server_->entities().get_component<CombatStats>(pid);
    if (cs) { cs->hp = data.player_hp; cs->max_hp = data.player_max_hp; }

    world_state_->set_day(data.day); world_state_->set_season(data.season);
    for (const auto& t : data.seen_tiles) world_state_->reveal_tile(t);
    knowledge_->mark_topic_known("ugarit_sack");
    knowledge_->mark_topic_known("sea_peoples");
    knowledge_->mark_topic_known("byblos_king");
    for (const auto& t : data.known_topics) knowledge_->mark_topic_known(t);
    relationships_ = std::make_unique<RelationshipTable>();
    for (const auto& [nid, rel] : data.relations)
        relationships_->set_relation(nid, {rel[0], rel[1], rel[2]});

    for (const auto& nd : data.npcs) {
        std::vector<NPCKnowledgeEntry> facts;
        for (const auto& [fid, ver] : nd.knowledge) facts.push_back({fid, ver, 70, false, ""});
        game_mode_->spawn_npc(nd.id, nd.name, nd.position.x, nd.position.y, nd.personality, facts);
        auto& eid = game_mode_->npc_entities().back();
        auto* ncs = server_->entities().get_component<CombatStats>(eid);
        if (ncs) { ncs->hp = nd.hp; ncs->max_hp = nd.max_hp; ncs->alive = nd.alive; }
    }

    auto* pos = server_->entities().get_component<Position>(pid);
    if (pos) camera_system_->center_on(pos->world_pos);
}

void App::recruit_soldier() {
    static int idx = 0;
    if (network_->is_connected() && !network_->is_hosting()) {
        network_->send_recruit_request(client_->local_player_pos());
    } else {
        Vec2f facing{0, -1};
        game_mode_->spawn_soldier(game_mode_->player_entity(), idx++, facing);
    }
}

#include "ui/ui-manager.hpp"
#include "core/locale-manager.hpp"
#include "entities/components/combat-stats.hpp"
#include "entities/components/soldier-ai.hpp"
#include "net/client.hpp"
#include "net/session.hpp"
#include "systems/formation.hpp"
#include "systems/render-system.hpp"
#include "world/world-state.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <spdlog/spdlog.h>
#include <unordered_set>

UIManager::UIManager(RenderSystem *rs, Font *hud_font, Client &client,
                     LocaleManager &locale, UIControl control)
    : render_system_(rs), hud_font_(hud_font), client_(client), locale_(locale),
      ctrl_(std::move(control))
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    auto *window = render_system_->window();
    auto *renderer = render_system_->renderer();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    SDL_StartTextInput(window);

    // Load CJK font for Chinese support
    auto &fonts = *io.Fonts;
    fonts.Clear();
    // Default font first, then merge CJK
    fonts.AddFontDefault();
    // Try Noto Sans Mono CJK as a regular font (includes CJK)
    ImFontConfig cfg;
    cfg.MergeMode = true;
    static std::vector<ImWchar> const cjkRanges = {
        0x0020, 0x00FF, 0x2000, 0x206F, 0x3000, 0x30FF, 0x31F0,
        0x31FF, 0xFF00, 0xFFEF, 0x4E00, 0x9FFF, 0,
    };
    // Try CJK fonts across platforms
    fonts.AddFontFromFileTTF("./assets/fonts/SourceHanSansCN-Regular.otf", 16, &cfg,
                             cjkRanges.data());
    fonts.Build();

    load_server_list();
}

UIManager::~UIManager()
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

bool UIManager::process_event(SDL_Event const &event)
{
    return ImGui_ImplSDL3_ProcessEvent(&event);
}

void UIManager::update(float dt)
{
    // Update discovery notification timers
    for (auto &n : discovery_queue_)
        n.timer -= dt;
    std::erase_if(discovery_queue_, [](auto const &n) { return n.timer <= 0.f; });
}

void UIManager::render(WorldState *world_state)
{
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();

    // Check for newly discovered towns (add to notification queue)
    if (world_state) {
        auto const &discovered = client_.discovered_towns();
        for (auto const &td : discovered) {
            if (!notified_town_ids_.contains(td.loc_id)) {
                notified_town_ids_.insert(td.loc_id);
                discovery_queue_.push_back({td.locale_key, 5.f}); // 5-second display
            }
        }
    }

    if (world_state)
        render_hud_window(*world_state);

    // Render discovery toast notifications
    if (!discovery_queue_.empty()) {
        float y_offset = 120.f;
        for (auto const &n : discovery_queue_) {
            std::string town_name = locale_.get(n.locale_key);
            if (town_name.empty())
                town_name = n.locale_key;
            float alpha = std::min(1.f, n.timer);
            ImVec4 color(0.95f, 0.85f, 0.6f, alpha);

            ImGui::SetNextWindowPos(ImVec2(400, y_offset), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::Begin(("##toast_" + n.locale_key).c_str(), nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
            ImGui::TextColored(color, "🏛 %s",
                               std::format("{} {}", locale_.get("town.discovered_prefix"),
                                           town_name).c_str());
            // Description
            std::string desc_key = n.locale_key + ".desc";
            std::string desc = locale_.get(desc_key);
            if (!desc.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, alpha * 0.8f), "%s", desc.c_str());
            }
            ImGui::End();

            y_offset += 60.f;
        }
    }

    // clang-format off
    if (client_.dialogue().active)        render_dialogue();
    if (show_journal_)                render_journal();
    if (show_inventory_)              render_inventory();
    if (show_map_)                    render_map();
    if (show_help_)                   render_help_panel();
    if (show_load_menu_)              render_load_menu();

    if (show_multiplayer_) {
        if (!chat_active_) {
            chat_buf_[0] = '\0';
            chat_active_ = true;
        }
        render_multiplayer_menu();
    }
    else {
        if (chat_active_) {
            chat_active_ = false;
            chat_buf_[0] = '\0';
        }
    }
    // clang-format on

    if (render_system_->debug_mode())
        render_debug_legend();

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), render_system_->renderer());

    // HUD text rendered with game font on top of everything
    if (client_.player_id() != invalid_entity && world_state)
        render_hud_text(*world_state);
}

void UIManager::render_hud_window([[maybe_unused]] WorldState const &world_state)
{
    // Language switcher (top-right, needs buttons, stays in ImGui)
    ImGui::SetNextWindowPos(ImVec2(600, 10), ImGuiCond_Always);
    ImGui::Begin("Lang", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
    for (auto const &lang : locale_.available_languages()) {
        if (ImGui::Button(lang.c_str())) {
            locale_.set_language(lang);
        }
    }
    ImGui::End();

    // Death overlay (stays ImGui for simplicity)
    auto const &surv = client_.survival();
    auto const *stat = client_.player_stats();
    if ((stat && !stat->alive) || surv.health <= 0.F) {
        ImGui::SetNextWindowPos(ImVec2(440, 300), ImGuiCond_Always);
        ImGui::Begin("DeathOverlay", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(1.f, 0.1f, 0.1f, 1.f), "%s",
                           locale_.get("resp.dead").c_str());
        ImGui::TextDisabled("%s", locale_.get("resp.dead_hint").c_str());
        ImGui::End();
    }
}

void UIManager::render_hud_text(WorldState const &world_state)
{
    constexpr float x = 10.F, line_h = 14.F;
    float y = 10.F;

    // Dark semi-transparent background for HUD readability
    constexpr float bg_w = 420.F;
    float bg_h = 200.F;
    SDL_FRect hud_bg{.x = 4.F, .y = 4.F, .w = bg_w, .h = bg_h};
    SDL_SetRenderDrawBlendMode(render_system_->renderer(), SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(render_system_->renderer(), 8, 8, 12, 160);
    SDL_RenderFillRect(render_system_->renderer(), &hud_bg);

    // Line 1: Title | Language | FPS
    hud_font_->draw({x, y}, SDL_Color{.r = 204, .g = 178, .b = 102, .a = 255},
                    std::format("{} | {}: {} | {}: {:.0f}", locale_.get("game.title"),
                                locale_.get("menu.language"), locale_.current_language_name(),
                                locale_.get("hud.fps"), ctrl_.get_fps()));

    // Current town name (if any)
    auto const &town_id = client_.current_town_id();
    if (!town_id.empty()) {
        y += line_h;
        std::string town_name = locale_.get("location." + town_id);
        hud_font_->draw({x, y}, SDL_Color{.r = 255, .g = 220, .b = 140, .a = 255},
                        town_name);
    }

    // Line 2: Day | Season | Time
    y += line_h;
    static constexpr char const *seasonKeys[] = {"season.spring", "season.summer", "season.autumn",
                                                 "season.winter"};
    hud_font_->draw({x, y}, SDL_Color{.r = 200, .g = 200, .b = 200, .a = 230},
                    std::format("{}: {} | {}: {} | {}: {:.0f}", locale_.get("hud.day"),
                                world_state.day(), locale_.get("hud.season"),
                                locale_.get(seasonKeys[world_state.season()]), locale_.get("hud.time"),
                                world_state.time_of_day()));

    // Line 3: Keys hint
    y += line_h;
    hud_font_->draw({x, y}, SDL_Color{.r = 140, .g = 140, .b = 140, .a = 200},
                    std::string(locale_.get("hud.keys")));

    // Line 4: HP | Food | Water | Energy
    y += line_h;
    auto const &sv = client_.survival();
    auto *cs = client_.player_stats();
    assert(cs != nullptr);
    hud_font_->draw({x, y}, SDL_Color{.r = 200, .g = 200, .b = 200, .a = 230},
                    std::format("{}: {}/{} | {}: {:.0f} | {}: {:.0f} | {}: {:.0f}",
                                locale_.get("hud.hp"), cs->hp, cs->max_hp, locale_.get("hud.food"), sv.food,
                                locale_.get("hud.water"), sv.water, locale_.get("hud.energy"), sv.energy));

    // Soldier info (lines 5+)
    auto my_team = client_.player_team();
    int soldier_count = 0;
    int follow_count = 0;
    int guard_count = 0;
    int patrol_count = 0;
    int melee_count = 0;
    int ranged_count = 0;
    for (auto &re : client_.remote_entities()) {
        if (re.kind == EntityKind::soldier && re.cs.alive && re.cs.team == my_team) {
            ++soldier_count;
            if (re.soldier_stance == SoldierStance::defensive)
                ++follow_count;
            else if (re.soldier_stance == SoldierStance::offensive)
                ++guard_count;
            else if (re.soldier_stance == SoldierStance::passive)
                ++patrol_count;
            if (re.soldier_role == SoldierRole::melee)
                ++melee_count;
            else if (re.soldier_role == SoldierRole::ranged)
                ++ranged_count;
        }
    }
    if (soldier_count > 0) {
        y += line_h;
        auto sel = client_.selected_roles();
        auto &fm_reg = formation_registry();
        hud_font_->draw({x, y}, SDL_Color{200, 200, 200, 230},
                        std::format("Soldiers: {} | [G] {}F/{}G/{}P", soldier_count, follow_count,
                                    guard_count, patrol_count));
        y += line_h;
        hud_font_->draw({x, y}, SDL_Color{200, 200, 200, 230},
                        std::format("[1] Melee:{} {} [2] Ranged:{} {} | [F4] Formation: {}",
                                    melee_count, (sel & 1) ? "*" : " ", ranged_count,
                                    (sel & 2) ? "*" : " ",
                                    fm_reg[client_.formation_idx() % fm_reg.size()].first));
    }
    else {
        y += line_h;
        hud_font_->draw({x, y}, SDL_Color{140, 140, 140, 200}, locale_.get("hud.recruit_hint"));
    }

    // Network stats (bottom of HUD block)
    auto rtt = client_.rtt_ms();
    auto age = client_.last_sync_age();
    auto color = SDL_Color{100, 255, 100, 200};
    if (age > 100)
        color = SDL_Color{255, 255, 100, 200};
    if (age > 300)
        color = SDL_Color{255, 200, 50, 200};
    if (age > 1000)
        color = SDL_Color{255, 80, 80, 200};
    y += line_h;
    hud_font_->draw({x, y}, color, std::format("RTT: {}ms  (last sync: {}ms ago)", rtt, age));
}

void UIManager::render_dialogue()
{
    auto const &ds = client_.dialogue();

    ImGui::SetNextWindowSize(ImVec2(500, 480), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_Appearing);
    ImGui::Begin(locale_.get("dialogue.title").c_str(), nullptr, ImGuiWindowFlags_NoResize);

    // Title + trust indicator
    ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.4f, 1.0f), "%s", ds.npc_name.c_str());
    if (ds.npc_id != "__town__" && ds.npc_id != "__building__") {
        ImGui::SameLine();
        ImGui::TextDisabled(" %s: %+d", locale_.get("dialogue.trust").c_str(), ds.npc_trust);
    }
    ImGui::Separator();

    // Conversation history
    auto playerLabel = locale_.get("dialogue.player");
    if (playerLabel.empty())
        playerLabel = "You"; // absolute fallback
    ImGui::BeginChild("History", ImVec2(0, 200), true);
    for (auto const &line : ds.history) {
        std::string text;
        if (line.use_template) {
            text = ctrl_.resolve_dialogue_text(line.template_type, line.variant_index, line.slots);
        }
        else if (line.use_raw) {
            text = line.raw_text;
        }
        else {
            text = locale_.get(line.text_key);
        }

        if (line.speaker == DialogueLine::player) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.f), "%s: %s", playerLabel.c_str(),
                               text.c_str());
        }
        else {
            char const *name = line.npc_name.empty() ? ds.npc_name.c_str() : line.npc_name.c_str();
            ImGui::TextWrapped("%s: %s", name, text.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("%s", locale_.get("dialogue.ask_about").c_str());

    // Topics
    for (auto const &topic_id : ds.available_topics) {
        std::string label;
        if (topic_id == "__quest_turnin__")
            label = "- " + locale_.get("quest.turnin_label");
        else if (topic_id == "__quest__")
            label = "- " + locale_.get("quest.label");
        else if (topic_id == "__attack__")
            label = "- " + locale_.get("dialogue.attack");
        else {
            label = "- " + locale_.get("topic." + topic_id);
            if (label == "- ")
                label = "- " + topic_id;
        }
        if (ImGui::Selectable(label.c_str())) {
            ctrl_.do_dialogue_action(topic_id);
        }
    }

    // Tell topics
    if (!ds.available_actions.empty()) {
        ImGui::Separator();
        ImGui::Text("%s", locale_.get("dialogue.share_what_you_know").c_str());
        for (auto const &action : ds.available_actions) {
            if (!action.starts_with("tell:"))
                continue;
            std::string topic_id = action.substr(5);
            std::string label = "+ " + locale_.get("topic." + topic_id);
            if (label == "+ ")
                label = "+ " + topic_id;
            if (ImGui::Selectable(label.c_str())) {
                ctrl_.do_dialogue_action(action);
            }
        }
    }

    // Town service actions (__inn__ etc.)
    bool has_town_services = std::ranges::any_of(ds.available_actions, [](auto const &a) {
        return a.starts_with("__");
    });
    if (has_town_services) {
        ImGui::Separator();
        for (auto const &action : ds.available_actions) {
            if (action == "__inn__") {
                if (ImGui::Button("🏨 Rest at Inn")) {
                    ctrl_.do_dialogue_action(action);
                }
            }
            else if (action == "__market__") {
                if (ImGui::Button("🏪 Buy Supplies (Market)")) {
                    ctrl_.do_dialogue_action(action);
                }
            }
            else if (action == "__temple__") {
                if (ImGui::Button("⛪ Visit Temple (Heal Ailments)")) {
                    ctrl_.do_dialogue_action(action);
                }
            }
            else if (action == "__blacksmith__") {
                if (ImGui::Button("🔧 Blacksmith (Upgrade Gear)")) {
                    ctrl_.do_dialogue_action(action);
                }
            }
        }
    }
    else {
        // NPC actions bar
        ImGui::Separator();
        if (ds.can_gift && ImGui::Button(locale_.get("dialogue.gift").c_str())) {
            ctrl_.do_dialogue_action("__gift__");
        }
        ImGui::SameLine();
        if (ds.can_threaten && ImGui::Button(locale_.get("dialogue.threaten").c_str())) {
            ctrl_.do_dialogue_action("__threaten__");
        }
        ImGui::SameLine();
    }
    if (ImGui::Button(locale_.get("dialogue.leave").c_str())) {
        ctrl_.end_dialogue();
    }

    ImGui::End();
}

void UIManager::render_journal()
{
    ImGui::Begin(locale_.get("ui.journal").c_str(), &show_journal_);
    ImGui::Text("%s", locale_.get("ui.journal_placeholder").c_str());
    static char buf[4096] = {};
    ImGui::InputTextMultiline("##journal_entry", buf, sizeof(buf), ImVec2(-1, 300));
    ImGui::End();
}

void UIManager::render_inventory()
{
    ImGui::Begin(locale_.get("ui.inventory").c_str(), &show_inventory_);
    ImGui::Text("%s", locale_.get("ui.inventory_placeholder").c_str());
    ImGui::End();
}

void UIManager::render_help_panel()
{
    ImGui::SetNextWindowSize(ImVec2(320, 380), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(300, 80), ImGuiCond_Appearing);
    ImGui::Begin(locale_.get("help.title").c_str());
    ImGui::Text("%s", locale_.get("help.wasd").c_str());
    ImGui::Text("%s", locale_.get("help.e").c_str());
    ImGui::Text("%s", locale_.get("help.r").c_str());
    ImGui::Text("%s", locale_.get("help.g").c_str());
    ImGui::Separator();
    ImGui::Text("%s", locale_.get("help.f1").c_str());
    ImGui::Text("%s", locale_.get("help.f2").c_str());
    ImGui::Text("%s", locale_.get("help.f3").c_str());
    ImGui::Text("%s", locale_.get("help.f4").c_str());
    ImGui::Text("%s", locale_.get("help.f8").c_str());
    ImGui::Text("%s", locale_.get("help.f5").c_str());
    ImGui::Text("%s", locale_.get("help.f9").c_str());
    ImGui::Text("%s", locale_.get("help.f10").c_str());
    ImGui::Text("%s", locale_.get("help.f11").c_str());
    ImGui::Text("%s", locale_.get("help.f12").c_str());
    ImGui::Separator();
    ImGui::TextDisabled("%s", locale_.get("help.close").c_str());
    ImGui::End();
}

void UIManager::render_load_menu()
{
    ImGui::SetNextWindowSize(ImVec2(250, 300), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(500, 200), ImGuiCond_Appearing);
    ImGui::Begin("Load Game", nullptr, ImGuiWindowFlags_NoResize);

    auto slots = ctrl_.available_save_slots();
    if (slots.empty()) {
        ImGui::TextDisabled("%s", locale_.get("resp.no_save").c_str());
    }
    else {
        for (int slot : slots) {
            if (ImGui::Selectable(std::to_string(slot).c_str())) {
                ctrl_.load_from_slot(slot);
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button(locale_.get("mp.close").c_str())) {
        show_load_menu_ = false;
    }
    ImGui::End();
}

void UIManager::render_multiplayer_menu()
{
    assert(show_multiplayer_);
    ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(400, 200), ImGuiCond_Appearing);
    bool show = true;
    ImGui::Begin("Multiplayer", &show, ImGuiWindowFlags_NoResize);
    if (!show)
        show_multiplayer_ = false;

    switch (ctrl_.get_session_mode()) {
    case SessionMode::local:
        render_local();
        break;
    case SessionMode::host:
        render_hosting();
        break;
    case SessionMode::client:
        render_client();
        break;
    }

    ImGui::Separator();
    if (ImGui::Button(locale_.get("mp.close").c_str()))
        show_multiplayer_ = false;
    ImGui::End();
}

void UIManager::load_server_list()
{
    std::ifstream f("saves/servers.txt");
    if (!f.is_open())
        return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty())
            server_list_.push_back(line);
    }
}

void UIManager::save_server_list()
{
    std::ofstream f("saves/servers.txt");
    for (auto const &ip : server_list_)
        f << ip << '\n';
}

void UIManager::render_map()
{
    ImGui::Begin(locale_.get("ui.map").c_str(), &show_map_, ImGuiWindowFlags_AlwaysAutoResize);

    assert(location_defs_);

    // Determine bounding box of all towns
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    for (auto const &ld : *location_defs_) {
        min_x = std::min(min_x, ld.world_pos.x);
        min_y = std::min(min_y, ld.world_pos.y);
        max_x = std::max(max_x, ld.world_pos.x);
        max_y = std::max(max_y, ld.world_pos.y);
    }
    float range_x = max_x - min_x + 200.f; // add padding
    float range_y = max_y - min_y + 200.f;

    ImVec2 canvas_size(400, 300);
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("map_canvas", canvas_size);

    // Background
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                      IM_COL32(20, 18, 15, 220));

    // Draw town markers
    auto const &discovered = client_.discovered_towns();
    for (auto const &ld : *location_defs_) {
        bool is_discovered = std::ranges::any_of(discovered, [&](auto const &d) {
            return d.loc_id == ld.id;
        });

        // Map world coords to canvas coords
        float nx = (ld.world_pos.x - min_x + 100.f) / range_x;
        float ny = (ld.world_pos.y - min_y + 100.f) / range_y;
        ImVec2 pos = ImVec2(canvas_pos.x + nx * canvas_size.x,
                            canvas_pos.y + ny * canvas_size.y);

        if (is_discovered) {
            dl->AddCircleFilled(pos, 5, IM_COL32(255, 220, 140, 255));
            dl->AddText(ImVec2(pos.x + 8, pos.y - 5),
                        IM_COL32(200, 190, 170, 255),
                        locale_.get(ld.display_name).c_str());
        }
        else {
            // Undiscovered: dim dot
            dl->AddCircleFilled(pos, 3, IM_COL32(60, 55, 45, 200));
        }
    }

    ImGui::End();
}

void UIManager::render_hosting()
{
    ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "%s", locale_.get("mp.hosting").c_str());
    ImGui::TextDisabled("%s", locale_.get("mp.hosting_hint").c_str());
    ImGui::Text("%s: %zu", locale_.get("mp.remote_entities").c_str(),
                client_.remote_entities().size());
    ImGui::Separator();

    if (ImGui::Button(locale_.get("mp.stop_hosting").c_str())) {
        ctrl_.stop_listen();
        ctrl_.set_session_mode(SessionMode::local);
    }

    render_client_list();
}

void UIManager::render_local()
{
    ImGui::Text("%s", locale_.get("mp.host").c_str());
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt(locale_.get("mp.port").c_str(), &host_port_);
    host_port_ = std::clamp(host_port_, 1, 65535);
    if (ImGui::Button(locale_.get("mp.host_btn").c_str())) {
        ctrl_.start_host_session(host_port_);
    }
    ImGui::Separator();
    ImGui::Text("%s", locale_.get("mp.join").c_str());
    host_ip_.resize(63);
    ImGui::InputText(locale_.get("mp.ip").c_str(), host_ip_.data(), host_ip_.size() + 1);
    host_ip_.resize(std::strlen(host_ip_.c_str()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##port", &host_port_);
    host_port_ = std::clamp(host_port_, 1, 65535);
    if (ImGui::Button(locale_.get("mp.connect").c_str())) {
        spdlog::debug("Clicked button, Connecting to {}:{}", host_ip_, host_port_);
        ctrl_.start_client_session(host_ip_, host_port_);
        auto entry = host_ip_ + ":" + std::to_string(host_port_);
        if (!std::ranges::contains(server_list_, entry)) {
            server_list_.push_back(entry);
            save_server_list();
        }
    }
    if (!server_list_.empty()) {
        ImGui::Separator();
        ImGui::Text("%s", locale_.get("mp.saved_servers").c_str());
        for (int i = 0; i < static_cast<int>(server_list_.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Button(server_list_[i].c_str())) {
                // Parse "ip:port" or just "ip"
                auto colon = server_list_[i].rfind(':');
                {
                    std::string ip;
                    int port = 27015;
                    if (colon != std::string::npos) {
                        ip = server_list_[i].substr(0, colon);
                        port = std::stoi(server_list_[i].substr(colon + 1));
                    }
                    else {
                        ip = server_list_[i];
                    }
                    host_ip_ = ip;
                    host_port_ = port;
                }
                spdlog::debug("Clicked button, Connecting to {}:{}", host_ip_, host_port_);
                ctrl_.start_client_session(host_ip_, host_port_);
            }
            ImGui::SameLine();
            if (ImGui::Button("X")) {
                server_list_.erase(server_list_.begin() + i);
                save_server_list();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
}

void UIManager::render_client()
{
    ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "%s Host: %s", locale_.get("mp.connected").c_str(),
                       client_.session_remote_info().c_str());
    ImGui::Text("%s: %zu", locale_.get("mp.remote_entities").c_str(),
                client_.remote_entities().size());
    ImGui::Separator();
    render_chat();
    if (ImGui::Button(locale_.get("mp.disconnect").c_str())) {
        // Return to local
        ctrl_.start_local_session();
    }
}

void UIManager::render_client_list()
{
    auto const &sessions = ctrl_.server_sessions();

    if (sessions.empty()) {
        ImGui::TextDisabled("%s", locale_.get("mp.no_clients").c_str());
        return;
    }

    if (ImGui::CollapsingHeader(locale_.get("mp.clients").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // 表格方式显示
        if (ImGui::BeginTable("client_list", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {

            ImGui::TableSetupColumn(locale_.get("mp.id").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    60.0f);
            ImGui::TableSetupColumn(locale_.get("mp.address").c_str(),
                                    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(locale_.get("mp.status").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    80.0f);
            ImGui::TableSetupColumn(locale_.get("mp.kick").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    50.0f);
            ImGui::TableHeadersRow();

            int idx = 1;
            for (auto const &tg : sessions) {
                ImGui::TableNextRow();

                // ID
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", idx++);

                // Address
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", tg->remote_info().c_str());

                // Status
                ImGui::TableSetColumnIndex(2);
                if (tg.get()->is_open()) {
                    ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "%s",
                                       locale_.get("mp.connected").c_str());
                }
                else {
                    ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "%s",
                                       locale_.get("mp.disconnected").c_str());
                }

                // Kick
                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(tg.get());
                if (ImGui::SmallButton("X"))
                    ctrl_.kick_session(tg, "kicked by host");
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Unindent();
    }
}

void UIManager::render_chat()
{
    ImGui::Text("%s", locale_.get("mp.chat").c_str());
    ImGui::BeginChild("ChatLog", ImVec2(0, 100), true);
    for (auto const &msg : client_.chat_history())
        ImGui::TextWrapped("%s", msg.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.f)
        ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
    chat_buf_.resize(255);
    ImGui::InputText("##chat", chat_buf_.data(), chat_buf_.size() + 1,
                     ImGuiInputTextFlags_EnterReturnsTrue);
    chat_buf_.resize(std::strlen(chat_buf_.c_str()));
    if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (!chat_buf_.empty()) {
            client_.send_chat(chat_buf_);
            chat_buf_.clear();
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(locale_.get("mp.send").c_str())) {
        if (!chat_buf_.empty()) {
            client_.send_chat(chat_buf_);
            chat_buf_.clear();
        }
    }
}

void UIManager::render_debug_legend()
{
    ImGui::SetNextWindowPos(ImVec2(780, 10), ImGuiCond_Always);
    ImGui::Begin("##debug_legend", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);

    ImGui::TextColored(ImVec4(0, 1, 1, 1), "■");
    ImGui::SameLine();
    ImGui::TextDisabled("texture bounds");

    ImGui::TextColored(ImVec4(1, 0.65f, 0, 1), "■");
    ImGui::SameLine();
    ImGui::TextDisabled("collision volume");

    ImGui::End();
}

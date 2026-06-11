#include "ui/ui-manager.hpp"
#include "core/app.hpp"
#include "core/game-mode.hpp"
#include "core/locale-manager.hpp"
#include "entities/components/combat-stats.hpp"
#include "net/network-manager.hpp"
#include "survival/condition-tracker.hpp"
#include "ui/imgui_impl_sdl3.h"
#include "world/world-state.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <imgui.h>

UIManager::UIManager(SDL_Window *window, SDL_Renderer *renderer)
    : window_(window), renderer_(renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Load CJK font for Chinese support
    auto &fonts = *io.Fonts;
    fonts.Clear();
    // Default font first, then merge CJK
    fonts.AddFontDefault();
    // Try Noto Sans Mono CJK as a regular font (includes CJK)
    ImFontConfig cfg;
    cfg.MergeMode = true;
    static ImWchar const cjkRanges[] = {
        0x0020, 0x00FF, 0x2000, 0x206F, 0x3000, 0x30FF, 0x31F0,
        0x31FF, 0xFF00, 0xFFEF, 0x4E00, 0x9FFF, 0,
    };
    // Try CJK fonts across platforms
    static char const *cjkPaths[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simsun.ttc",
        "C:\\Windows\\Fonts\\msgothic.ttc",
#elif __APPLE__
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
#else
        "/usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
#endif
    };
    for (char const *path : cjkPaths) {
        FILE *test = fopen(path, "rb");
        if (test) {
            fclose(test);
            fonts.AddFontFromFileTTF(path, 16.f, &cfg, cjkRanges);
            break;
        }
    }
    fonts.Build();

    ImGui_ImplSDL3_Init(window);
    ImGui_ImplSDLRenderer3_Init(renderer);
    SDL_StartTextInput(window);
    load_server_list();
}

UIManager::~UIManager()
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::process_event(SDL_Event const &event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
}

void UIManager::update(float /*dt*/) {}

void UIManager::render(WorldState const &world_state, App const &app)
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    render_hud(world_state, app);
    if (app.game_mode().dialogue().active) {
        render_dialogue(app);
    }
    if (show_journal_)
        render_journal(app);
    if (show_inventory_)
        render_inventory(app);
    if (show_map_)
        render_map(app);
    if (app.show_help())
        render_help_panel(app);
    if (app.show_load_menu())
        render_load_menu(app);
    if (app.show_multiplayer()) {
        if (!chat_active_) {
            chat_buf_[0] = '\0';
            chat_active_ = true;
        }
        render_multiplayer_menu(app);
    }
    else {
        if (chat_active_) {
            chat_active_ = false;
            chat_buf_[0] = '\0';
        }
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

void UIManager::render_hud(WorldState const &world_state, App const &app)
{
    auto const &loc = app.locale();

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::Begin("HUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs);

    ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.4f, 1.0f), "%s",
                       loc.get("game.title").c_str());
    ImGui::SameLine();
    ImGui::Text(" | %s: %s", loc.get("menu.language").c_str(),
                loc.language_name().c_str());

    ImGui::Separator();

    static char const *seasonKeys[] = {"season.spring", "season.summer",
                                       "season.autumn", "season.winter"};
    ImGui::Text("%s: %d | %s: %s | %s: %.1f", loc.get("hud.day").c_str(),
                world_state.day(), loc.get("hud.season").c_str(),
                loc.get(seasonKeys[world_state.season()]).c_str(),
                loc.get("hud.time").c_str(), world_state.time_of_day());
    ImGui::TextDisabled("%s", loc.get("hud.keys").c_str());

    auto const &sv = app.survival().state();
    auto *cs = app.player_combat_stats();
    ImGui::Separator();
    if (cs) {
        ImGui::Text("%s: %d/%d | %s: %.0f | %s: %.0f | %s: %.0f",
                    loc.get("hud.hp").c_str(), cs->hp, cs->max_hp,
                    loc.get("hud.food").c_str(), sv.food,
                    loc.get("hud.water").c_str(), sv.water,
                    loc.get("hud.energy").c_str(), sv.energy);
    }
    else {
        ImGui::Text("%s: %.0f | %s: %.0f | %s: %.0f",
                    loc.get("hud.food").c_str(), sv.food,
                    loc.get("hud.water").c_str(), sv.water,
                    loc.get("hud.energy").c_str(), sv.energy);
    }
    ImGui::End();

    // Language switcher
    ImGui::SetNextWindowPos(ImVec2(10, 110));
    ImGui::Begin("Lang", nullptr,
                 ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_AlwaysAutoResize);
    for (int i = 0; i < app.locale().language_count(); ++i) {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::Button(app.locale().language_name(i).c_str())) {
            const_cast<App &>(app).set_ui_language(i);
        }
    }
    ImGui::End();

    // Death overlay
    auto const &surv = app.survival().state();
    auto *pcs = app.player_combat_stats();
    if ((pcs && !pcs->alive) || surv.health <= 0.f) {
        ImGui::SetNextWindowPos(ImVec2(440, 300), ImGuiCond_Always);
        ImGui::Begin("DeathOverlay", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(1.f, 0.1f, 0.1f, 1.f), "%s",
                           app.locale().get("resp.dead").c_str());
        ImGui::TextDisabled("%s", app.locale().get("resp.dead_hint").c_str());
        ImGui::End();
    }
}

void UIManager::render_dialogue(App const &app)
{
    auto const &ds = app.game_mode().dialogue();
    auto const &loc = app.locale();

    ImGui::SetNextWindowSize(ImVec2(500, 480), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(50, 200), ImGuiCond_Appearing);
    ImGui::Begin(loc.get("dialogue.title").c_str(), nullptr,
                 ImGuiWindowFlags_NoResize);

    // Title + trust indicator
    ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.4f, 1.0f), "%s",
                       ds.npc_name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(" %s: %+d", loc.get("dialogue.trust").c_str(),
                        ds.npc_trust);
    ImGui::Separator();

    // Conversation history
    auto playerLabel = loc.get("dialogue.player");
    if (playerLabel.empty())
        playerLabel = "You"; // absolute fallback
    ImGui::BeginChild("History", ImVec2(0, 200), true);
    for (auto const &line : ds.history) {
        std::string text;
        if (line.use_raw) {
            text = line.raw_text;
        }
        else {
            text = loc.get(line.text_key);
        }

        if (line.speaker == DialogueLine::player) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.f), "%s: %s",
                               playerLabel.c_str(), text.c_str());
        }
        else {
            char const *name = line.npc_name.empty() ? ds.npc_name.c_str()
                                                     : line.npc_name.c_str();
            ImGui::TextWrapped("%s: %s", name, text.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("%s", loc.get("dialogue.ask_about").c_str());

    // Topics
    for (auto const &topic_id : ds.available_topics) {
        std::string label;
        if (topic_id == "__quest_turnin__")
            label = "- " + loc.get("quest.turnin_label");
        else if (topic_id == "__quest__")
            label = "- " + loc.get("quest.label");
        else if (topic_id == "__attack__")
            label = "- " + loc.get("dialogue.attack");
        else {
            label = "- " + loc.get("topic." + topic_id);
            if (label == "- ")
                label = "- " + topic_id;
        }
        if (ImGui::Selectable(label.c_str())) {
            const_cast<App &>(app).do_dialogue_action(topic_id);
        }
    }

    // Tell topics
    if (!ds.available_actions.empty()) {
        ImGui::Separator();
        ImGui::Text("%s", loc.get("dialogue.share_what_you_know").c_str());
        for (auto const &action : ds.available_actions) {
            if (!action.starts_with("tell:"))
                continue;
            std::string topic_id = action.substr(5);
            std::string label = "+ " + loc.get("topic." + topic_id);
            if (label == "+ ")
                label = "+ " + topic_id;
            if (ImGui::Selectable(label.c_str())) {
                const_cast<App &>(app).do_dialogue_action(action);
            }
        }
    }

    // Actions bar
    ImGui::Separator();
    if (ds.can_gift && ImGui::Button(loc.get("dialogue.gift").c_str())) {
        const_cast<App &>(app).do_dialogue_action("__gift__");
    }
    ImGui::SameLine();
    if (ds.can_threaten &&
        ImGui::Button(loc.get("dialogue.threaten").c_str())) {
        const_cast<App &>(app).do_dialogue_action("__threaten__");
    }
    ImGui::SameLine();
    if (ImGui::Button(loc.get("dialogue.leave").c_str())) {
        const_cast<App &>(app).end_dialogue();
    }

    ImGui::End();
}

void UIManager::render_journal(App const &app)
{
    auto const &loc = app.locale();
    ImGui::Begin(loc.get("ui.journal").c_str(), &show_journal_);
    ImGui::Text("%s", loc.get("ui.journal_placeholder").c_str());
    static char buf[4096] = {};
    ImGui::InputTextMultiline("##journal_entry", buf, sizeof(buf),
                              ImVec2(-1, 300));
    ImGui::End();
}

void UIManager::render_inventory(App const &app)
{
    auto const &loc = app.locale();
    ImGui::Begin(loc.get("ui.inventory").c_str(), &show_inventory_);
    ImGui::Text("%s", loc.get("ui.inventory_placeholder").c_str());
    ImGui::End();
}

void UIManager::render_help_panel(App const &app)
{
    auto const &loc = app.locale();
    ImGui::SetNextWindowSize(ImVec2(300, 280), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(300, 100), ImGuiCond_Appearing);
    ImGui::Begin(loc.get("help.title").c_str());
    ImGui::Text("%s", loc.get("help.wasd").c_str());
    ImGui::Text("%s", loc.get("help.e").c_str());
    ImGui::Text("%s", loc.get("help.r").c_str());
    ImGui::Text("%s", loc.get("help.f1").c_str());
    ImGui::Text("%s", loc.get("help.f2").c_str());
    ImGui::Text("%s", loc.get("help.f5").c_str());
    ImGui::Text("%s", loc.get("help.f9").c_str());
    ImGui::Text("%s", loc.get("help.f10").c_str());
    ImGui::Separator();
    ImGui::TextDisabled("%s", loc.get("help.close").c_str());
    ImGui::End();
}

void UIManager::render_load_menu(App const &app)
{
    ImGui::SetNextWindowSize(ImVec2(250, 300), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(500, 200), ImGuiCond_Appearing);
    ImGui::Begin("Load Game", nullptr, ImGuiWindowFlags_NoResize);

    auto slots = const_cast<App &>(app).available_save_slots();
    if (slots.empty()) {
        ImGui::TextDisabled("No save files found.");
    }
    else {
        for (int slot : slots) {
            std::string label = "Slot " + std::to_string(slot);
            if (ImGui::Selectable(label.c_str())) {
                const_cast<App &>(app).load_from_slot(slot);
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Close")) {
        const_cast<App &>(app).set_show_load_menu(false);
    }
    ImGui::End();
}

void UIManager::render_multiplayer_menu(App const &app)
{
    if (!app.show_multiplayer())
        return;
    ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(400, 200), ImGuiCond_Appearing);
    bool show = true;
    ImGui::Begin("Multiplayer", &show, ImGuiWindowFlags_NoResize);
    if (!show)
        const_cast<App &>(app).set_show_multiplayer(false);

    auto const &loc = app.locale();
    auto &mutApp = const_cast<App &>(app);
    auto &net = mutApp.network();
    if (!net.is_connected()) {
        ImGui::Text("%s", loc.get("mp.host").c_str());
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Port", &host_port_);
        if (host_port_ < 1)
            host_port_ = 1;
        if (host_port_ > 65535)
            host_port_ = 65535;
        if (ImGui::Button(loc.get("mp.host_btn").c_str())) {
            mutApp.stop_session();
            mutApp.start_host_session(host_port_);
        }
        ImGui::Separator();
        ImGui::Text("%s", loc.get("mp.join").c_str());
        host_ip_.reserve(64);
        ImGui::InputText(loc.get("mp.ip").c_str(), host_ip_.data(),
                         host_ip_.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##port", &host_port_);
        host_port_ = std::clamp(host_port_, 1, 65535);
        if (ImGui::Button(loc.get("mp.connect").c_str())) {
            mutApp.stop_session();
            mutApp.start_client_session(host_ip_, host_port_);
            auto entry = host_ip_ + ":" + std::to_string(host_port_);
            if (!std::ranges::contains(server_list_, entry)) {
                server_list_.push_back(entry);
                save_server_list();
            }
        }
        if (!server_list_.empty()) {
            ImGui::Separator();
            ImGui::Text("%s", loc.get("mp.saved_servers").c_str());
            for (int i = 0; i < (int)server_list_.size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::Button(server_list_[i].c_str())) {
                    // Parse "ip:port" or just "ip"
                    auto colon = server_list_[i].rfind(':');
                    std::string ip, portStr;
                    int port = 27015;
                    if (colon != std::string::npos) {
                        ip = server_list_[i].substr(0, colon);
                        portStr = server_list_[i].substr(colon + 1);
                        try {
                            port = std::stoi(portStr);
                        }
                        catch (...) {
                        }
                    }
                    else {
                        ip = server_list_[i];
                    }
                    host_ip_ = ip;
                    host_port_ = port;
                    mutApp.stop_session();
                    mutApp.start_client_session(ip, port);
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
    else {
        if (app.session_mode() == SessionMode::host) {
            ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "%s",
                               loc.get("mp.hosting").c_str());
            ImGui::TextDisabled("%s", loc.get("mp.hosting_hint").c_str());
        }
        else {
            ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "%s",
                               loc.get("mp.connected").c_str());
        }
        ImGui::Text("%s: %zu", loc.get("mp.remote_entities").c_str(),
                    app.client().remote_entities().size());
        ImGui::Separator();
        ImGui::Text("%s", loc.get("mp.chat").c_str());
        ImGui::BeginChild("ChatLog", ImVec2(0, 100), true);
        for (auto const &msg : app.client().chat_history())
            ImGui::TextWrapped("%s", msg.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.f)
            ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();
        ImGui::InputText("##chat", chat_buf_, sizeof(chat_buf_),
                         ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemDeactivatedAfterEdit() ||
            ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (strlen(chat_buf_) > 0) {
                mutApp.client().send_chat(chat_buf_);
                chat_buf_[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(loc.get("mp.send").c_str())) {
            if (strlen(chat_buf_) > 0) {
                mutApp.client().send_chat(chat_buf_);
                chat_buf_[0] = '\0';
            }
        }
        if (ImGui::Button(loc.get("mp.disconnect").c_str()))
            mutApp.stop_session();
    }
    ImGui::Separator();
    if (ImGui::Button(loc.get("mp.close").c_str()))
        const_cast<App &>(app).set_show_multiplayer(false);
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

void UIManager::render_map(App const &app)
{
    auto const &loc = app.locale();
    ImGui::Begin(loc.get("ui.map").c_str(), &show_map_);
    ImGui::Text("%s", loc.get("ui.map_placeholder").c_str());
    ImGui::End();
}

#include "ui/ui-manager.hpp"
#include "core/app.hpp"
#include "core/game-mode.hpp"
#include "core/locale-manager.hpp"
#include "entities/components/combat-stats.hpp"
#include "net/server.hpp"
#include "world/world-state.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <spdlog/spdlog.h>

UIManager::UIManager(SDL_Window *window, SDL_Renderer *renderer)
    : window_(window), renderer_(renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

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
        if (std::filesystem::exists(path)) {
            spdlog::info("Loading font in {}", path);
            auto *f = fonts.AddFontFromFileTTF(path, 16.f, &cfg, cjkRanges);
            assert(f != nullptr);
            break;
        }
    }
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

void UIManager::update(float /*dt*/) {}

void UIManager::render(WorldState *world_state, App &app)
{
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();

    if (world_state)
        render_hud(*world_state, app);

    // clang-format off
    if (app.dialogue().active)             render_dialogue(app);
    if (show_journal_)                     render_journal(app);
    if (show_inventory_)                   render_inventory(app);
    if (show_map_)                         render_map(app);
    if (app.show_help())                   render_help_panel(app);
    if (app.show_load_menu())              render_load_menu(app);

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
    // clang-format on

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

void UIManager::render_hud(WorldState const &world_state, App &app)
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

    ImGui::SameLine();
    ImGui::Text(" | %s: %.02f", "FPS", app.stopwatch().fps());

    ImGui::Separator();

    static char const *seasonKeys[] = {"season.spring", "season.summer",
                                       "season.autumn", "season.winter"};
    ImGui::Text("%s: %d | %s: %s | %s: %.0f", loc.get("hud.day").c_str(),
                world_state.day(), loc.get("hud.season").c_str(),
                loc.get(seasonKeys[world_state.season()]).c_str(),
                loc.get("hud.time").c_str(), world_state.time_of_day());
    ImGui::TextDisabled("%s", loc.get("hud.keys").c_str());

    auto const &sv = app.client().survival();
    auto *cs = app.client().player_stats();
    ImGui::Separator();
    assert(cs != nullptr);
    ImGui::Text("%s: %d/%d | %s: %.0f | %s: %.0f | %s: %.0f",
                loc.get("hud.hp").c_str(), cs->hp, cs->max_hp,
                loc.get("hud.food").c_str(), sv.food,
                loc.get("hud.water").c_str(), sv.water,
                loc.get("hud.energy").c_str(), sv.energy);
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
            app.set_ui_language(i);
        }
    }
    ImGui::End();

    // Death overlay
    auto const &surv = app.client().survival();
    auto *pcs = app.client().player_stats();
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
    auto const &ds = app.dialogue();
    auto const &loc = app.locale();

    ImGui::SetNextWindowSize(ImVec2(500, 480), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_Appearing);
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
    assert(app.show_multiplayer());
    ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(400, 200), ImGuiCond_Appearing);
    bool show = true;
    ImGui::Begin("Multiplayer", &show, ImGuiWindowFlags_NoResize);
    if (!show)
        const_cast<App &>(app).set_show_multiplayer(false);

    auto &mutApp = const_cast<App &>(app);
    switch (app.session_mode()) {
    case SessionMode::local:
        render_local(mutApp);
        break;
    case SessionMode::host:
        render_hosting(mutApp);
        break;
    case SessionMode::client:
        render_client(mutApp);
        break;
    }

    ImGui::Separator();
    if (ImGui::Button(app.locale().get("mp.close").c_str()))
        mutApp.set_show_multiplayer(false);
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

void UIManager::render_hosting(App &app)
{
    auto const &loc = app.locale();
    ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "%s",
                       loc.get("mp.hosting").c_str());
    ImGui::TextDisabled("%s", loc.get("mp.hosting_hint").c_str());
    ImGui::Text("%s: %zu", loc.get("mp.remote_entities").c_str(),
                app.client().remote_entities().size());
    ImGui::Separator();

    if (ImGui::Button(loc.get("mp.stop_hosting").c_str())) {
        app.game_mode().stop_host();
        // app.start_local_session();
        app.set_session_mode(SessionMode::local);
    }

    render_client_list(app);

    render_chat(app);
    // if (ImGui::Button(loc.get("mp.disconnect").c_str()))
    //     app.stop_session();
}

void UIManager::render_local(App &app)
{
    auto const &loc = app.locale();

    ImGui::Text("%s", loc.get("mp.host").c_str());
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("Port", &host_port_);
    host_port_ = std::clamp(host_port_, 1, 65535);
    if (ImGui::Button(loc.get("mp.host_btn").c_str())) {
        app.start_host_session(host_port_);
    }
    ImGui::Separator();
    ImGui::Text("%s", loc.get("mp.join").c_str());
    host_ip_.resize(63);
    ImGui::InputText(loc.get("mp.ip").c_str(), host_ip_.data(),
                     host_ip_.size() + 1);
    host_ip_.resize(std::strlen(host_ip_.c_str()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##port", &host_port_);
    host_port_ = std::clamp(host_port_, 1, 65535);
    if (ImGui::Button(loc.get("mp.connect").c_str())) {
        spdlog::debug("Clicked button, Connecting to {}:{}", host_ip_,
                      host_port_);
        app.start_client_session(host_ip_, host_port_);
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
                spdlog::debug("Clicked button, Connecting to {}:{}", host_ip_,
                              host_port_);
                app.start_client_session(host_ip_, host_port_);
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

void UIManager::render_client(App &app)
{
    auto const &loc = app.locale();
    ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "%s",
                       loc.get("mp.connected").c_str());
    ImGui::Text("%s: %zu", loc.get("mp.remote_entities").c_str(),
                app.client().remote_entities().size());
    ImGui::Separator();
    render_chat(app);
    if (ImGui::Button(loc.get("mp.disconnect").c_str())) {
        // Return to local
        app.start_local_session();
    }
}

void UIManager::render_client_list(App &app)
{
    auto const &loc = app.locale();
    auto *server = app.game_mode().server();

    if (server->transports_guards().empty()) {
        ImGui::TextDisabled("%s", loc.get("mp.no_clients").c_str());
        return;
    }

    if (ImGui::CollapsingHeader(loc.get("mp.clients").c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // 表格方式显示
        if (ImGui::BeginTable("client_list", 4,
                              ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_RowBg)) {

            ImGui::TableSetupColumn(loc.get("mp.id").c_str(),
                                    ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn(loc.get("mp.address").c_str(),
                                    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(loc.get("mp.status").c_str(),
                                    ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Kick", ImGuiTableColumnFlags_WidthFixed,
                                    50.0f);
            ImGui::TableHeadersRow();

            for (auto const &tg : server->transports_guards()) {
                ImGui::TableNextRow();

                // ID
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", typeid(tg.get()).name());

                // Address
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%p", tg.get().get());

                // Status
                ImGui::TableSetColumnIndex(2);
                if (tg.get()->is_open()) {
                    ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "%s",
                                       loc.get("mp.connected").c_str());
                }
                else {
                    ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "%s",
                                       loc.get("mp.disconnected").c_str());
                }

                // Kick
                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(tg.get().get());
                if (ImGui::SmallButton("X"))
                    server->kick(tg.get(), "kicked by host");
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Unindent();
    }
}

void UIManager::render_chat(App &app)
{
    auto const &loc = app.locale();
    ImGui::Text("%s", loc.get("mp.chat").c_str());
    ImGui::BeginChild("ChatLog", ImVec2(0, 100), true);
    for (auto const &msg : app.client().chat_history())
        ImGui::TextWrapped("%s", msg.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.f)
        ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
    chat_buf_.resize(255);
    ImGui::InputText("##chat", chat_buf_.data(), chat_buf_.size() + 1,
                     ImGuiInputTextFlags_EnterReturnsTrue);
    chat_buf_.resize(std::strlen(chat_buf_.c_str()));
    if (ImGui::IsItemDeactivatedAfterEdit() ||
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (!chat_buf_.empty()) {
            app.client().send_chat(chat_buf_);
            chat_buf_.clear();
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(loc.get("mp.send").c_str())) {
        if (!chat_buf_.empty()) {
            app.client().send_chat(chat_buf_);
            chat_buf_.clear();
        }
    }
}

#include "ui/UIManager.hpp"
#include "ui/imgui_impl_sdl3.h"
#include "world/WorldState.hpp"
#include "core/App.hpp"
#include "core/GameMode.hpp"
#include "core/LocaleManager.hpp"
#include "survival/ConditionTracker.hpp"
#include "entities/components/CombatStats.hpp"
#include "net/NetworkManager.hpp"
#include <fstream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <imgui.h>

UIManager::UIManager(SDL_Window* window, SDL_Renderer* renderer)
    : m_window(window), m_renderer(renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Load CJK font for Chinese support
    auto& fonts = *io.Fonts;
    fonts.Clear();
    // Default font first, then merge CJK
    fonts.AddFontDefault();
    // Try Noto Sans Mono CJK as a regular font (includes CJK)
    ImFontConfig cfg;
    cfg.MergeMode = true;
    static const ImWchar cjkRanges[] = {
        0x0020, 0x00FF,
        0x2000, 0x206F,
        0x3000, 0x30FF,
        0x31F0, 0x31FF,
        0xFF00, 0xFFEF,
        0x4E00, 0x9FFF,
        0,
    };
    // Try CJK fonts across platforms
    static const char* cjkPaths[] = {
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
    for (const char* path : cjkPaths) {
        FILE* test = fopen(path, "rb");
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
    loadServerList();
}

UIManager::~UIManager() {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(event);
}

void UIManager::update(float /*dt*/) {}

void UIManager::render(const WorldState& worldState, const App& app) {
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    renderHUD(worldState, app);
    if (app.gameMode().dialogue().active) {
        renderDialogue(app);
    }
    if (m_showJournal) renderJournal(app);
    if (m_showInventory) renderInventory(app);
    if (m_showMap) renderMap(app);
    if (app.showHelp()) renderHelpPanel(app);
    if (app.showLoadMenu()) renderLoadMenu(app);
    if (app.showMultiplayer()) {
        if (!m_chatActive) { m_chatBuf[0] = '\0'; m_chatActive = true; }
        renderMultiplayerMenu(app);
    } else {
        if (m_chatActive) { m_chatActive = false; m_chatBuf[0] = '\0'; }
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
}

void UIManager::renderHUD(const WorldState& worldState, const App& app) {
    const auto& loc = app.locale();

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::Begin("HUD", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);

    ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.4f, 1.0f), "%s", loc.get("game.title").c_str());
    ImGui::SameLine();
    ImGui::Text(" | %s: %s", loc.get("menu.language").c_str(), loc.languageName().c_str());

    ImGui::Separator();

    static const char* seasonKeys[] = {"season.spring", "season.summer", "season.autumn", "season.winter"};
    ImGui::Text("%s: %d | %s: %s | %s: %.1f",
        loc.get("hud.day").c_str(), worldState.day(),
        loc.get("hud.season").c_str(), loc.get(seasonKeys[worldState.season()]).c_str(),
        loc.get("hud.time").c_str(), worldState.timeOfDay());
    ImGui::TextDisabled("%s", loc.get("hud.keys").c_str());

    const auto& sv = app.survival().state();
    auto* cs = app.playerCombatStats();
    ImGui::Separator();
    if (cs) {
        ImGui::Text("%s: %d/%d | %s: %.0f | %s: %.0f | %s: %.0f",
            loc.get("hud.hp").c_str(), cs->hp, cs->maxHp,
            loc.get("hud.food").c_str(), sv.food,
            loc.get("hud.water").c_str(), sv.water,
            loc.get("hud.energy").c_str(), sv.energy);
    } else {
        ImGui::Text("%s: %.0f | %s: %.0f | %s: %.0f",
            loc.get("hud.food").c_str(), sv.food,
            loc.get("hud.water").c_str(), sv.water,
            loc.get("hud.energy").c_str(), sv.energy);
    }
    ImGui::End();

    // Language switcher
    ImGui::SetNextWindowPos(ImVec2(10, 110));
    ImGui::Begin("Lang", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
    for (int i = 0; i < app.locale().languageCount(); ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::Button(app.locale().languageName(i).c_str())) {
            const_cast<App&>(app).setUILanguage(i);
        }
    }
    ImGui::End();

    // Death overlay
    const auto& surv = app.survival().state();
    auto* pcs = app.playerCombatStats();
    if ((pcs && !pcs->alive) || surv.health <= 0.f) {
        ImGui::SetNextWindowPos(ImVec2(440, 300), ImGuiCond_Always);
        ImGui::Begin("DeathOverlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(1.f, 0.1f, 0.1f, 1.f), "%s",
            app.locale().get("resp.dead").c_str());
        ImGui::TextDisabled("%s", app.locale().get("resp.dead_hint").c_str());
        ImGui::End();
    }
}

void UIManager::renderDialogue(const App& app) {
    const auto& ds = app.gameMode().dialogue();
    const auto& loc = app.locale();

    ImGui::SetNextWindowSize(ImVec2(500, 480), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(50, 200), ImGuiCond_Appearing);
    ImGui::Begin(loc.get("dialogue.title").c_str(), nullptr, ImGuiWindowFlags_NoResize);

    // Title + trust indicator
    ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.4f, 1.0f), "%s", ds.npcName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(" %s: %+d", loc.get("dialogue.trust").c_str(), ds.npcTrust);
    ImGui::Separator();

    // Conversation history
    auto playerLabel = loc.get("dialogue.player");
    if (playerLabel.empty()) playerLabel = "You";  // absolute fallback
    ImGui::BeginChild("History", ImVec2(0, 200), true);
    for (const auto& line : ds.history) {
        std::string text;
        if (line.useRaw) {
            text = line.rawText;
        } else {
            text = loc.get(line.textKey);
        }

        if (line.speaker == DialogueLine::Player) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.f),
                "%s: %s", playerLabel.c_str(), text.c_str());
        } else {
            const char* name = line.npcName.empty() ? ds.npcName.c_str() : line.npcName.c_str();
            ImGui::TextWrapped("%s: %s", name, text.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("%s", loc.get("dialogue.ask_about").c_str());

    // Topics
    for (const auto& topicId : ds.availableTopics) {
        std::string label;
        if (topicId == "__quest_turnin__") label = "- " + loc.get("quest.turnin_label");
        else if (topicId == "__quest__") label = "- " + loc.get("quest.label");
        else if (topicId == "__attack__") label = "- " + loc.get("dialogue.attack");
        else {
            label = "- " + loc.get("topic." + topicId);
            if (label == "- ") label = "- " + topicId;
        }
        if (ImGui::Selectable(label.c_str())) {
            const_cast<App&>(app).doDialogueAction(topicId);
        }
    }

    // Tell topics
    if (!ds.availableActions.empty()) {
        ImGui::Separator();
        ImGui::Text("%s", loc.get("dialogue.share_what_you_know").c_str());
        for (const auto& action : ds.availableActions) {
            if (!action.starts_with("tell:")) continue;
            std::string topicId = action.substr(5);
            std::string label = "+ " + loc.get("topic." + topicId);
            if (label == "+ ") label = "+ " + topicId;
            if (ImGui::Selectable(label.c_str())) {
                const_cast<App&>(app).doDialogueAction(action);
            }
        }
    }

    // Actions bar
    ImGui::Separator();
    if (ds.canGift && ImGui::Button(loc.get("dialogue.gift").c_str())) {
        const_cast<App&>(app).doDialogueAction("__gift__");
    }
    ImGui::SameLine();
    if (ds.canThreaten && ImGui::Button(loc.get("dialogue.threaten").c_str())) {
        const_cast<App&>(app).doDialogueAction("__threaten__");
    }
    ImGui::SameLine();
    if (ImGui::Button(loc.get("dialogue.leave").c_str())) {
        const_cast<App&>(app).endDialogue();
    }

    ImGui::End();
}

void UIManager::renderJournal(const App& app) {
    const auto& loc = app.locale();
    ImGui::Begin(loc.get("ui.journal").c_str(), &m_showJournal);
    ImGui::Text("%s", loc.get("ui.journal_placeholder").c_str());
    static char buf[4096] = {};
    ImGui::InputTextMultiline("##journal_entry", buf, sizeof(buf), ImVec2(-1, 300));
    ImGui::End();
}

void UIManager::renderInventory(const App& app) {
    const auto& loc = app.locale();
    ImGui::Begin(loc.get("ui.inventory").c_str(), &m_showInventory);
    ImGui::Text("%s", loc.get("ui.inventory_placeholder").c_str());
    ImGui::End();
}

void UIManager::renderHelpPanel(const App& app) {
    const auto& loc = app.locale();
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

void UIManager::renderLoadMenu(const App& app) {
    ImGui::SetNextWindowSize(ImVec2(250, 300), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(500, 200), ImGuiCond_Appearing);
    ImGui::Begin("Load Game", nullptr, ImGuiWindowFlags_NoResize);

    auto slots = const_cast<App&>(app).availableSaveSlots();
    if (slots.empty()) {
        ImGui::TextDisabled("No save files found.");
    } else {
        for (int slot : slots) {
            std::string label = "Slot " + std::to_string(slot);
            if (ImGui::Selectable(label.c_str())) {
                const_cast<App&>(app).loadFromSlot(slot);
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Close")) {
        const_cast<App&>(app).setShowLoadMenu(false);
    }
    ImGui::End();
}

void UIManager::renderMultiplayerMenu(const App& app) {
    if (!app.showMultiplayer()) return;
    ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(400, 200), ImGuiCond_Appearing);
    bool show = true;
    ImGui::Begin("Multiplayer", &show, ImGuiWindowFlags_NoResize);
    if (!show) const_cast<App&>(app).setShowMultiplayer(false);

    auto* net = const_cast<App&>(app).networkMut();
    if (!net->isConnected()) {
        ImGui::Text("Host Game");
        if (ImGui::Button("Host (port 27015)")) {
            net->host();
        }
        ImGui::Separator();
        ImGui::Text("Join Game");
        ImGui::InputText("IP", m_hostIp, sizeof(m_hostIp));
        if (ImGui::Button("Connect")) {
            net->connect(m_hostIp);
            // Add to server list
            std::string ip(m_hostIp);
            if (std::find(m_serverList.begin(), m_serverList.end(), ip) == m_serverList.end()) {
                m_serverList.push_back(ip);
                saveServerList();
            }
        }
        // Server list
        if (!m_serverList.empty()) {
            ImGui::Separator();
            ImGui::Text("Saved Servers:");
            for (int i = 0; i < (int)m_serverList.size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::Button(m_serverList[i].c_str())) {
                    strncpy(m_hostIp, m_serverList[i].c_str(), sizeof(m_hostIp) - 1);
                    net->connect(m_serverList[i]);
                }
                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    m_serverList.erase(m_serverList.begin() + i);
                    saveServerList();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        }
    } else {
        if (net->isHosting()) {
            ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "Hosting on port 27015");
        } else {
            ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "Connected!");
        }
        ImGui::Text("Remote entities: %zu", net->remoteEntities().size());

        ImGui::Separator();
        ImGui::Text("Chat:");
        ImGui::BeginChild("ChatLog", ImVec2(0, 100), true);
        for (const auto& msg : net->chatHistory()) {
            ImGui::TextWrapped("%s", msg.c_str());
        }
        ImGui::EndChild();
        ImGui::InputText("##chat", m_chatBuf, sizeof(m_chatBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (strlen(m_chatBuf) > 0) {
                const_cast<NetworkManager*>(net)->sendChat(m_chatBuf);
                m_chatBuf[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Send")) {
            if (strlen(m_chatBuf) > 0) {
                const_cast<NetworkManager*>(net)->sendChat(m_chatBuf);
                m_chatBuf[0] = '\0';
            }
        }

        if (ImGui::Button("Disconnect")) {
            net->disconnect();
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Close")) {
        const_cast<App&>(app).setShowMultiplayer(false);
    }
    ImGui::End();
}

void UIManager::loadServerList() {
    std::ifstream f("saves/servers.txt");
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) m_serverList.push_back(line);
    }
}

void UIManager::saveServerList() {
    std::ofstream f("saves/servers.txt");
    for (const auto& ip : m_serverList) f << ip << '\n';
}

void UIManager::renderMap(const App& app) {
    const auto& loc = app.locale();
    ImGui::Begin(loc.get("ui.map").c_str(), &m_showMap);
    ImGui::Text("%s", loc.get("ui.map_placeholder").c_str());
    ImGui::End();
}

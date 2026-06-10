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

    void processEvent(const SDL_Event& event);
    void update(float dt);
    void render(const WorldState& worldState, const App& app);

private:
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    bool m_showJournal = false;
    bool m_showInventory = false;
    bool m_showMap = false;

    void renderHUD(const WorldState& worldState, const App& app);
    void renderJournal(const App& app);
    void renderInventory(const App& app);
    void renderMap(const App& app);
    void renderDialogue(const App& app);
    void renderHelpPanel(const App& app);
    void renderLoadMenu(const App& app);
    void renderMultiplayerMenu(const App& app);
    char m_hostIp[32] = "127.0.0.1";
    char m_chatBuf[256] = {};
    bool m_chatActive = false;
    std::vector<std::string> m_serverList;
    void loadServerList();
    void saveServerList();
};

#pragma once

struct ImDrawData;
struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

// Platform backend (input)
bool ImGui_ImplSDL3_Init(SDL_Window* window);
void ImGui_ImplSDL3_Shutdown();
void ImGui_ImplSDL3_NewFrame();
bool ImGui_ImplSDL3_ProcessEvent(const SDL_Event& event);

// Renderer backend (SDL_Renderer)
bool ImGui_ImplSDLRenderer3_Init(SDL_Renderer* renderer);
void ImGui_ImplSDLRenderer3_Shutdown();
void ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData* drawData, SDL_Renderer* renderer);

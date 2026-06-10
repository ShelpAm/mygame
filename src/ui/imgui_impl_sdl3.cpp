#include "ui/imgui_impl_sdl3.h"
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vector>

// ============= Platform backend =============

static SDL_Window *g_Window = nullptr;
static Uint64 g_Time = 0;

bool ImGui_ImplSDL3_Init(SDL_Window *window)
{
    g_Window = window;
    g_Time = SDL_GetTicks();

    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_sdl3";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    return true;
}

void ImGui_ImplSDL3_Shutdown()
{
    g_Window = nullptr;
}

void ImGui_ImplSDL3_NewFrame()
{
    ImGuiIO &io = ImGui::GetIO();

    int w, h;
    SDL_GetWindowSize(g_Window, &w, &h);
    io.DisplaySize = ImVec2((float)w, (float)h);

    Uint64 currentTime = SDL_GetTicks();
    double deltaTime = (currentTime - g_Time) / 1000.0;
    if (deltaTime <= 0.0)
        deltaTime = 0.001;
    io.DeltaTime = (float)deltaTime;
    g_Time = currentTime;

    // Mouse
    float mx, my;
    Uint32 mouseButtons = SDL_GetMouseState(&mx, &my);
    io.AddMousePosEvent(mx, my);
    io.AddMouseButtonEvent(0, (mouseButtons & SDL_BUTTON_LMASK) != 0);
    io.AddMouseButtonEvent(1, (mouseButtons & SDL_BUTTON_RMASK) != 0);
    io.AddMouseButtonEvent(2, (mouseButtons & SDL_BUTTON_MMASK) != 0);

    // Keyboard
    int numKeys;
    bool const *keys = SDL_GetKeyboardState(&numKeys);

    auto mapKey = [](SDL_Scancode sc) -> ImGuiKey {
        switch (sc) {
        case SDL_SCANCODE_TAB:
            return ImGuiKey_Tab;
        case SDL_SCANCODE_LEFT:
            return ImGuiKey_LeftArrow;
        case SDL_SCANCODE_RIGHT:
            return ImGuiKey_RightArrow;
        case SDL_SCANCODE_UP:
            return ImGuiKey_UpArrow;
        case SDL_SCANCODE_DOWN:
            return ImGuiKey_DownArrow;
        case SDL_SCANCODE_PAGEUP:
            return ImGuiKey_PageUp;
        case SDL_SCANCODE_PAGEDOWN:
            return ImGuiKey_PageDown;
        case SDL_SCANCODE_HOME:
            return ImGuiKey_Home;
        case SDL_SCANCODE_END:
            return ImGuiKey_End;
        case SDL_SCANCODE_INSERT:
            return ImGuiKey_Insert;
        case SDL_SCANCODE_DELETE:
            return ImGuiKey_Delete;
        case SDL_SCANCODE_BACKSPACE:
            return ImGuiKey_Backspace;
        case SDL_SCANCODE_SPACE:
            return ImGuiKey_Space;
        case SDL_SCANCODE_RETURN:
            return ImGuiKey_Enter;
        case SDL_SCANCODE_ESCAPE:
            return ImGuiKey_Escape;
        case SDL_SCANCODE_APOSTROPHE:
            return ImGuiKey_Apostrophe;
        case SDL_SCANCODE_COMMA:
            return ImGuiKey_Comma;
        case SDL_SCANCODE_MINUS:
            return ImGuiKey_Minus;
        case SDL_SCANCODE_PERIOD:
            return ImGuiKey_Period;
        case SDL_SCANCODE_SLASH:
            return ImGuiKey_Slash;
        case SDL_SCANCODE_SEMICOLON:
            return ImGuiKey_Semicolon;
        case SDL_SCANCODE_EQUALS:
            return ImGuiKey_Equal;
        case SDL_SCANCODE_LEFTBRACKET:
            return ImGuiKey_LeftBracket;
        case SDL_SCANCODE_BACKSLASH:
            return ImGuiKey_Backslash;
        case SDL_SCANCODE_RIGHTBRACKET:
            return ImGuiKey_RightBracket;
        case SDL_SCANCODE_GRAVE:
            return ImGuiKey_GraveAccent;
        case SDL_SCANCODE_CAPSLOCK:
            return ImGuiKey_CapsLock;
        case SDL_SCANCODE_SCROLLLOCK:
            return ImGuiKey_ScrollLock;
        case SDL_SCANCODE_NUMLOCKCLEAR:
            return ImGuiKey_NumLock;
        case SDL_SCANCODE_PRINTSCREEN:
            return ImGuiKey_PrintScreen;
        case SDL_SCANCODE_PAUSE:
            return ImGuiKey_Pause;
        case SDL_SCANCODE_LCTRL:
            return ImGuiKey_LeftCtrl;
        case SDL_SCANCODE_LSHIFT:
            return ImGuiKey_LeftShift;
        case SDL_SCANCODE_LALT:
            return ImGuiKey_LeftAlt;
        case SDL_SCANCODE_LGUI:
            return ImGuiKey_LeftSuper;
        case SDL_SCANCODE_RCTRL:
            return ImGuiKey_RightCtrl;
        case SDL_SCANCODE_RSHIFT:
            return ImGuiKey_RightShift;
        case SDL_SCANCODE_RALT:
            return ImGuiKey_RightAlt;
        case SDL_SCANCODE_RGUI:
            return ImGuiKey_RightSuper;
        case SDL_SCANCODE_0:
            return ImGuiKey_0;
        case SDL_SCANCODE_1:
            return ImGuiKey_1;
        case SDL_SCANCODE_2:
            return ImGuiKey_2;
        case SDL_SCANCODE_3:
            return ImGuiKey_3;
        case SDL_SCANCODE_4:
            return ImGuiKey_4;
        case SDL_SCANCODE_5:
            return ImGuiKey_5;
        case SDL_SCANCODE_6:
            return ImGuiKey_6;
        case SDL_SCANCODE_7:
            return ImGuiKey_7;
        case SDL_SCANCODE_8:
            return ImGuiKey_8;
        case SDL_SCANCODE_9:
            return ImGuiKey_9;
        case SDL_SCANCODE_A:
            return ImGuiKey_A;
        case SDL_SCANCODE_B:
            return ImGuiKey_B;
        case SDL_SCANCODE_C:
            return ImGuiKey_C;
        case SDL_SCANCODE_D:
            return ImGuiKey_D;
        case SDL_SCANCODE_E:
            return ImGuiKey_E;
        case SDL_SCANCODE_F:
            return ImGuiKey_F;
        case SDL_SCANCODE_G:
            return ImGuiKey_G;
        case SDL_SCANCODE_H:
            return ImGuiKey_H;
        case SDL_SCANCODE_I:
            return ImGuiKey_I;
        case SDL_SCANCODE_J:
            return ImGuiKey_J;
        case SDL_SCANCODE_K:
            return ImGuiKey_K;
        case SDL_SCANCODE_L:
            return ImGuiKey_L;
        case SDL_SCANCODE_M:
            return ImGuiKey_M;
        case SDL_SCANCODE_N:
            return ImGuiKey_N;
        case SDL_SCANCODE_O:
            return ImGuiKey_O;
        case SDL_SCANCODE_P:
            return ImGuiKey_P;
        case SDL_SCANCODE_Q:
            return ImGuiKey_Q;
        case SDL_SCANCODE_R:
            return ImGuiKey_R;
        case SDL_SCANCODE_S:
            return ImGuiKey_S;
        case SDL_SCANCODE_T:
            return ImGuiKey_T;
        case SDL_SCANCODE_U:
            return ImGuiKey_U;
        case SDL_SCANCODE_V:
            return ImGuiKey_V;
        case SDL_SCANCODE_W:
            return ImGuiKey_W;
        case SDL_SCANCODE_X:
            return ImGuiKey_X;
        case SDL_SCANCODE_Y:
            return ImGuiKey_Y;
        case SDL_SCANCODE_Z:
            return ImGuiKey_Z;
        case SDL_SCANCODE_F1:
            return ImGuiKey_F1;
        case SDL_SCANCODE_F2:
            return ImGuiKey_F2;
        case SDL_SCANCODE_F3:
            return ImGuiKey_F3;
        case SDL_SCANCODE_F4:
            return ImGuiKey_F4;
        case SDL_SCANCODE_F5:
            return ImGuiKey_F5;
        case SDL_SCANCODE_F6:
            return ImGuiKey_F6;
        case SDL_SCANCODE_F7:
            return ImGuiKey_F7;
        case SDL_SCANCODE_F8:
            return ImGuiKey_F8;
        case SDL_SCANCODE_F9:
            return ImGuiKey_F9;
        case SDL_SCANCODE_F10:
            return ImGuiKey_F10;
        case SDL_SCANCODE_F11:
            return ImGuiKey_F11;
        case SDL_SCANCODE_F12:
            return ImGuiKey_F12;
        default:
            return ImGuiKey_None;
        }
    };

    for (int i = 0; i < numKeys; i++) {
        ImGuiKey key = mapKey((SDL_Scancode)i);
        if (key != ImGuiKey_None) {
            io.AddKeyEvent(key, keys[i]);
        }
    }

    io.AddKeyEvent(ImGuiMod_Ctrl,
                   keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]);
    io.AddKeyEvent(ImGuiMod_Shift,
                   keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
    io.AddKeyEvent(ImGuiMod_Alt,
                   keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT]);
    io.AddKeyEvent(ImGuiMod_Super,
                   keys[SDL_SCANCODE_LGUI] || keys[SDL_SCANCODE_RGUI]);
}

bool ImGui_ImplSDL3_ProcessEvent(SDL_Event const &event)
{
    ImGuiIO &io = ImGui::GetIO();

    switch (event.type) {
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.which == SDL_TOUCH_MOUSEID)
            break;
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return io.WantCaptureMouse;
    case SDL_EVENT_TEXT_INPUT:
        io.AddInputCharactersUTF8(event.text.text);
        return io.WantCaptureKeyboard;
    }
    return false;
}

// ============= Renderer backend =============

static SDL_Renderer *g_Renderer = nullptr;
static SDL_Texture *g_FontTexture = nullptr;

bool ImGui_ImplSDLRenderer3_Init(SDL_Renderer *renderer)
{
    g_Renderer = renderer;
    ImGui::GetIO().BackendRendererName = "imgui_impl_sdlrenderer3";

    // Build font atlas
    unsigned char *pixels;
    int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    g_FontTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                      SDL_TEXTUREACCESS_STATIC, width, height);
    if (!g_FontTexture)
        return false;

    SDL_SetTextureBlendMode(g_FontTexture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(g_FontTexture, nullptr, pixels, width * 4);
    ImGui::GetIO().Fonts->SetTexID((ImTextureID)g_FontTexture);

    return true;
}

void ImGui_ImplSDLRenderer3_Shutdown()
{
    if (g_FontTexture) {
        SDL_DestroyTexture(g_FontTexture);
        g_FontTexture = nullptr;
    }
    g_Renderer = nullptr;
}

void ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData *drawData,
                                           SDL_Renderer *renderer)
{
    if (!drawData || drawData->CmdListsCount == 0)
        return;

    for (int n = 0; n < drawData->CmdListsCount; n++) {
        ImDrawList const *cmdList = drawData->CmdLists[n];

        // Build vertex arrays for SDL3 RenderGeometryRaw
        std::vector<float> xy(cmdList->VtxBuffer.Size * 2);
        std::vector<SDL_FColor> colors(cmdList->VtxBuffer.Size);
        std::vector<float> uv(cmdList->VtxBuffer.Size * 2);

        for (int i = 0; i < cmdList->VtxBuffer.Size; i++) {
            ImDrawVert const &v = cmdList->VtxBuffer[i];
            xy[i * 2 + 0] = v.pos.x;
            xy[i * 2 + 1] = v.pos.y;
            colors[i].r = (float)((v.col >> IM_COL32_R_SHIFT) & 0xFF) / 255.f;
            colors[i].g = (float)((v.col >> IM_COL32_G_SHIFT) & 0xFF) / 255.f;
            colors[i].b = (float)((v.col >> IM_COL32_B_SHIFT) & 0xFF) / 255.f;
            colors[i].a = (float)((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.f;
            uv[i * 2 + 0] = v.uv.x;
            uv[i * 2 + 1] = v.uv.y;
        }

        for (int i = 0; i < cmdList->CmdBuffer.Size; i++) {
            ImDrawCmd const *cmd = &cmdList->CmdBuffer[i];
            if (cmd->UserCallback) {
                cmd->UserCallback(cmdList, cmd);
                continue;
            }

            SDL_Rect clip = {(int)cmd->ClipRect.x, (int)cmd->ClipRect.y,
                             (int)(cmd->ClipRect.z - cmd->ClipRect.x),
                             (int)(cmd->ClipRect.w - cmd->ClipRect.y)};
            SDL_SetRenderClipRect(renderer, &clip);

            SDL_Texture *tex = (SDL_Texture *)cmd->GetTexID();
            SDL_RenderGeometryRaw(
                renderer, tex, xy.data(), (int)sizeof(float) * 2, colors.data(),
                (int)sizeof(SDL_FColor), uv.data(), (int)sizeof(float) * 2,
                cmdList->VtxBuffer.Size,
                cmdList->IdxBuffer.Data + cmd->IdxOffset, cmd->ElemCount,
                sizeof(ImDrawIdx));
        }
    }

    SDL_SetRenderClipRect(renderer, nullptr);
}

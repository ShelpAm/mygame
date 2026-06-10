#pragma once

#include <SDL3/SDL.h>
#include <unordered_map>
#include "core/Math.hpp"

class InputManager {
public:
    enum class Action {
        MoveUp, MoveDown, MoveLeft, MoveRight,
        Interact, OpenJournal, OpenInventory, OpenMap,
        Pause, Escape, Confirm, Cancel
    };

    InputManager();
    void update();

    bool isPressed(Action action) const;
    bool justPressed(Action action) const;
    bool justReleased(Action action) const;

    Vec2f mouseWorldPos() const { return m_mouseWorldPos; }
    Vec2i mouseScreenPos() const { return m_mouseScreenPos; }
    bool mouseMoved() const { return m_mouseMoved; }

    void setKeyBinding(Action action, SDL_Scancode scancode);

private:
    struct ActionState {
        bool pressed = false;
        bool wasPressed = false;
    };

    mutable std::unordered_map<Action, ActionState> m_actions;
    std::unordered_map<Action, SDL_Scancode> m_bindings;
    const bool* m_keyboardState = nullptr;

    Vec2f m_mouseWorldPos;
    Vec2i m_mouseScreenPos;
    bool m_mouseMoved = false;

    void initDefaultBindings();
};

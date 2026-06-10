#include "core/InputManager.hpp"

InputManager::InputManager() {
    initDefaultBindings();
}

void InputManager::initDefaultBindings() {
    setKeyBinding(Action::MoveUp, SDL_SCANCODE_W);
    setKeyBinding(Action::MoveDown, SDL_SCANCODE_S);
    setKeyBinding(Action::MoveLeft, SDL_SCANCODE_A);
    setKeyBinding(Action::MoveRight, SDL_SCANCODE_D);
    setKeyBinding(Action::Interact, SDL_SCANCODE_E);
    setKeyBinding(Action::OpenJournal, SDL_SCANCODE_J);
    setKeyBinding(Action::OpenInventory, SDL_SCANCODE_I);
    setKeyBinding(Action::OpenMap, SDL_SCANCODE_M);
    setKeyBinding(Action::Pause, SDL_SCANCODE_ESCAPE);
    setKeyBinding(Action::Escape, SDL_SCANCODE_ESCAPE);
    setKeyBinding(Action::Confirm, SDL_SCANCODE_RETURN);
    setKeyBinding(Action::Cancel, SDL_SCANCODE_BACKSPACE);
}

void InputManager::setKeyBinding(Action action, SDL_Scancode scancode) {
    m_bindings[action] = scancode;
    m_actions[action] = {};  // Ensure action state exists
}

void InputManager::update() {
    m_keyboardState = SDL_GetKeyboardState(nullptr);

    for (auto& [action, state] : m_actions) {
        state.wasPressed = state.pressed;
        auto it = m_bindings.find(action);
        state.pressed = it != m_bindings.end() && m_keyboardState[it->second];
    }

    float mx, my;
    SDL_GetMouseState(&mx, &my);
    m_mouseScreenPos = {static_cast<int>(mx), static_cast<int>(my)};
    m_mouseWorldPos = {mx, my};
}

bool InputManager::isPressed(Action action) const {
    auto it = m_actions.find(action);
    return it != m_actions.end() && it->second.pressed;
}

bool InputManager::justPressed(Action action) const {
    auto it = m_actions.find(action);
    return it != m_actions.end() && it->second.pressed && !it->second.wasPressed;
}

bool InputManager::justReleased(Action action) const {
    auto it = m_actions.find(action);
    return it != m_actions.end() && !it->second.pressed && it->second.wasPressed;
}

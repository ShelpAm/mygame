#include "core/input-manager.hpp"
InputManager::InputManager() {
    init_default_bindings();
}

void InputManager::init_default_bindings() {
    set_key_binding(Action::MoveUp, SDL_SCANCODE_W);
    set_key_binding(Action::MoveDown, SDL_SCANCODE_S);
    set_key_binding(Action::MoveLeft, SDL_SCANCODE_A);
    set_key_binding(Action::MoveRight, SDL_SCANCODE_D);
    set_key_binding(Action::Interact, SDL_SCANCODE_E);
    set_key_binding(Action::OpenJournal, SDL_SCANCODE_J);
    set_key_binding(Action::OpenInventory, SDL_SCANCODE_I);
    set_key_binding(Action::OpenMap, SDL_SCANCODE_M);
    set_key_binding(Action::Pause, SDL_SCANCODE_ESCAPE);
    set_key_binding(Action::Escape, SDL_SCANCODE_ESCAPE);
    set_key_binding(Action::Confirm, SDL_SCANCODE_RETURN);
    set_key_binding(Action::Cancel, SDL_SCANCODE_BACKSPACE);
}

void InputManager::set_key_binding(Action action, SDL_Scancode scancode) {
    bindings_[action] = scancode;
    actions_[action] = {};  // Ensure action state exists
}

void InputManager::update() {
    keyboard_state_ = SDL_GetKeyboardState(nullptr);

    for (auto& [action, state] : actions_) {
        state.wasPressed = state.pressed;
        auto it = bindings_.find(action);
        state.pressed = it != bindings_.end() && keyboard_state_[it->second];
    }

    float mx, my;
    SDL_GetMouseState(&mx, &my);
    mouse_screen_pos_ = {static_cast<int>(mx), static_cast<int>(my)};
    mouse_world_pos_ = {mx, my};
}

bool InputManager::is_pressed(Action action) const {
    auto it = actions_.find(action);
    return it != actions_.end() && it->second.pressed;
}

bool InputManager::just_pressed(Action action) const {
    auto it = actions_.find(action);
    return it != actions_.end() && it->second.pressed && !it->second.wasPressed;
}

bool InputManager::just_released(Action action) const {
    auto it = actions_.find(action);
    return it != actions_.end() && !it->second.pressed && it->second.wasPressed;
}

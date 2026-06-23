#include "core/input-manager.hpp"

#include <cassert>

InputManager::InputManager()
{
    init_default_bindings();
}

void InputManager::init_default_bindings()
{
    set_key_binding(Action::move_up, SDL_SCANCODE_W);
    set_key_binding(Action::move_down, SDL_SCANCODE_S);
    set_key_binding(Action::move_left, SDL_SCANCODE_A);
    set_key_binding(Action::move_right, SDL_SCANCODE_D);
    set_key_binding(Action::interact, SDL_SCANCODE_E);
    set_key_binding(Action::rest, SDL_SCANCODE_R);
    set_key_binding(Action::guard, SDL_SCANCODE_G);
    // set_key_binding(Action::open_journal, SDL_SCANCODE_J);
    // set_key_binding(Action::open_inventory, SDL_SCANCODE_I);
    // set_key_binding(Action::open_map, SDL_SCANCODE_M);
    // set_key_binding(Action::pause, SDL_SCANCODE_P);
    // set_key_binding(Action::escape, SDL_SCANCODE_ESCAPE);
    // set_key_binding(Action::confirm, SDL_SCANCODE_RETURN);
    set_key_binding(Action::select_melee, SDL_SCANCODE_1);
    set_key_binding(Action::select_ranged, SDL_SCANCODE_2);
    set_key_binding(Action::help, SDL_SCANCODE_F1);
    set_key_binding(Action::recruit, SDL_SCANCODE_F2);
    set_key_binding(Action::recruit_ranged, SDL_SCANCODE_F3);
    set_key_binding(Action::cycle_formation, SDL_SCANCODE_F4);
    set_key_binding(Action::quick_save, SDL_SCANCODE_F5);
    set_key_binding(Action::respawn, SDL_SCANCODE_F8);
    set_key_binding(Action::load_menu, SDL_SCANCODE_F9);
    set_key_binding(Action::multiplayer, SDL_SCANCODE_F10);
    set_key_binding(Action::debug_toggle, SDL_SCANCODE_F12);
    // set_key_binding(Action::cancel, SDL_SCANCODE_BACKSPACE);
}

void InputManager::set_key_binding(Action action, SDL_Scancode scancode)
{
    bindings_[action] = scancode;
    actions_[action] = ActionState{
        .pressed = false,
        .was_pressed = false,
    }; // Ensure action state exists
}

void InputManager::update()
{
    keyboard_state_ = SDL_GetKeyboardState(nullptr);

    for (auto &[action, state] : actions_) {
        assert(bindings_.contains(action) && "Action missing key binding");

        state.was_pressed = state.pressed;
        state.pressed = keyboard_state_[bindings_[action]];
    }

    float mx;
    float my;
    SDL_GetMouseState(&mx, &my);
    mouse_screen_pos_ = {static_cast<int>(mx), static_cast<int>(my)};
    mouse_world_pos_ = {mx, my};
}

bool InputManager::is_pressed(Action action) const
{
    auto it = actions_.find(action);
    return it != actions_.end() && it->second.pressed;
}

bool InputManager::just_pressed(Action action) const
{
    auto it = actions_.find(action);
    return it != actions_.end() && it->second.pressed && !it->second.was_pressed;
}

bool InputManager::just_released(Action action) const
{
    auto it = actions_.find(action);
    return it != actions_.end() && !it->second.pressed && it->second.was_pressed;
}

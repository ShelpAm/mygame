#pragma once

#include <SDL3/SDL.h>
#include <unordered_map>
#include "core/math.hpp"
class InputManager {
public:
    enum class Action {
        move_up, move_down, move_left, move_right,
        interact, open_journal, open_inventory, open_map,
        pause, escape, confirm, cancel
    };

    InputManager();
    void update();

    bool is_pressed(Action action) const;
    bool just_pressed(Action action) const;
    bool just_released(Action action) const;

    Vec2f mouse_world_pos() const { return mouse_world_pos_; }
    Vec2i mouse_screen_pos() const { return mouse_screen_pos_; }
    bool mouse_moved() const { return mouse_moved_; }

    void set_key_binding(Action action, SDL_Scancode scancode);

private:
    struct ActionState {
        bool pressed = false;
        bool was_pressed = false;
    };

    mutable std::unordered_map<Action, ActionState> actions_;
    std::unordered_map<Action, SDL_Scancode> bindings_;
    const bool* keyboard_state_ = nullptr;

    Vec2f mouse_world_pos_;
    Vec2i mouse_screen_pos_;
    bool mouse_moved_ = false;

    void init_default_bindings();
};

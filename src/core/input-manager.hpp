#pragma once

#include "core/math.hpp"

#include <SDL3/SDL.h>
#include <unordered_map>

class InputManager {
  public:
    enum class Action : std::uint8_t {
        move_up,
        move_down,
        move_left,
        move_right,
        interact,
        rest,
        recruit,
        recruit_ranged,
        guard,
        quick_save,
        load_menu,
        help,
        multiplayer,
        open_journal,
        open_inventory,
        open_map,
        pause,
        escape,
        confirm,
        cancel,
        toggle_debug,
        toggle_log_level,
        respawn,
        cycle_formation,
        select_melee,
        select_ranged
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
    bool const *keyboard_state_ = nullptr;

    Vec2f mouse_world_pos_{};
    Vec2i mouse_screen_pos_{};
    bool mouse_moved_ = false;

    void init_default_bindings();
};

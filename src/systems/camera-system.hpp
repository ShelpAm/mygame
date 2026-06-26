#pragma once

#include "core/math.hpp"
#include <SDL3/SDL.h>

class CameraSystem {
  public:
    explicit CameraSystem(int width, int height);

    void resize(int width, int height);
    void update(float dt);
    void set_target(Vec2f pos);
    void center_on(Vec2f pos);

    Vec2f world_to_screen(Vec2f world_pos) const;
    Vec2f screen_to_world(Vec2i screen_pos) const;

    SDL_FRect viewport() const;
    Vec2f center() const { return center_; }
    float zoom() const { return zoom_; }

  private:
    Vec2f center_;
    Vec2f target_;
    int width_;
    int height_;
    float zoom_ = 1.F;
    float smooth_speed_ = 8.F;
};

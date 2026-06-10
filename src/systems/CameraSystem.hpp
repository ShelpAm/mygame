#pragma once

#include "core/Math.hpp"
#include <SDL3/SDL.h>

class CameraSystem {
public:
    explicit CameraSystem(int width, int height);

    void resize(int width, int height);
    void update(float dt);
    void setTarget(Vec2f pos);
    void centerOn(Vec2f pos);

    Vec2f worldToScreen(Vec2f worldPos) const;
    Vec2f screenToWorld(Vec2i screenPos) const;

    SDL_FRect viewport() const;
    Vec2f center() const { return m_center; }
    float zoom() const { return m_zoom; }

private:
    Vec2f m_center;
    Vec2f m_target;
    int m_width;
    int m_height;
    float m_zoom = 1.f;
    float m_smoothSpeed = 8.f;
};

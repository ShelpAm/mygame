#include "systems/camera-system.hpp"
CameraSystem::CameraSystem(int width, int height) : width_(width), height_(height)
{
}

void CameraSystem::resize(int width, int height)
{
    width_ = width;
    height_ = height;
}

void CameraSystem::update(float dt)
{
    float dx = target_.x - center_.x;
    float dy = target_.y - center_.y;
    center_.x += dx * smooth_speed_ * dt;
    center_.y += dy * smooth_speed_ * dt;
}

void CameraSystem::set_target(Vec2f pos)
{
    target_ = pos;
}

void CameraSystem::center_on(Vec2f pos)
{
    target_ = pos;
    center_ = pos;
}

Vec2f CameraSystem::world_to_screen(Vec2f world_pos) const
{
    return {((world_pos.x - center_.x) * zoom_) + (static_cast<float>(width_) / 2.F),
            ((world_pos.y - center_.y) * zoom_) + (static_cast<float>(height_) / 2.F)};
}

Vec2f CameraSystem::screen_to_world(Vec2i screen_pos) const
{
    return {
        (static_cast<float>(screen_pos.x) - static_cast<float>(width_) / 2.f) / zoom_ + center_.x,
        (static_cast<float>(screen_pos.y) - static_cast<float>(height_) / 2.f) / zoom_ + center_.y};
}

// View port of world pos
SDL_FRect CameraSystem::viewport() const
{
    float halfW = static_cast<float>(width_) / (2.f * zoom_);
    float halfH = static_cast<float>(height_) / (2.f * zoom_);
    return {center_.x - halfW, center_.y - halfH, halfW * 2.f, halfH * 2.f};
}

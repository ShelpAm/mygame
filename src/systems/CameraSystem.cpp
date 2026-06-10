#include "systems/CameraSystem.hpp"

CameraSystem::CameraSystem(int width, int height)
    : m_width(width), m_height(height)
{}

void CameraSystem::resize(int width, int height) {
    m_width = width;
    m_height = height;
}

void CameraSystem::update(float dt) {
    float dx = m_target.x - m_center.x;
    float dy = m_target.y - m_center.y;
    m_center.x += dx * m_smoothSpeed * dt;
    m_center.y += dy * m_smoothSpeed * dt;
}

void CameraSystem::setTarget(Vec2f pos) {
    m_target = pos;
}

void CameraSystem::centerOn(Vec2f pos) {
    m_target = pos;
    m_center = pos;
}

Vec2f CameraSystem::worldToScreen(Vec2f worldPos) const {
    return {
        (worldPos.x - m_center.x) * m_zoom + static_cast<float>(m_width) / 2.f,
        (worldPos.y - m_center.y) * m_zoom + static_cast<float>(m_height) / 2.f
    };
}

Vec2f CameraSystem::screenToWorld(Vec2i screenPos) const {
    return {
        (static_cast<float>(screenPos.x) - static_cast<float>(m_width) / 2.f) / m_zoom + m_center.x,
        (static_cast<float>(screenPos.y) - static_cast<float>(m_height) / 2.f) / m_zoom + m_center.y
    };
}

SDL_FRect CameraSystem::viewport() const {
    float halfW = static_cast<float>(m_width) / (2.f * m_zoom);
    float halfH = static_cast<float>(m_height) / (2.f * m_zoom);
    return {
        m_center.x - halfW,
        m_center.y - halfH,
        halfW * 2.f,
        halfH * 2.f
    };
}

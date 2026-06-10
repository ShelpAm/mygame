#include "core/GameClock.hpp"

GameClock::GameClock() {
    m_lastTick = Clock::now();
}

void GameClock::restart() {
    m_lastTick = Clock::now();
    m_deltaTime = 0.f;
    m_totalTime = 0.f;
    m_frameCount = 0;
}

float GameClock::tick() {
    auto now = Clock::now();
    auto elapsed = std::chrono::duration<float>(now - m_lastTick).count();
    m_lastTick = now;

    m_deltaTime = std::min(elapsed, 0.1f);
    m_totalTime += m_deltaTime;
    ++m_frameCount;

    m_fpsAccumulator += m_deltaTime;
    ++m_fpsFrames;
    if (m_fpsAccumulator >= FPS_UPDATE_INTERVAL) {
        m_fps = static_cast<float>(m_fpsFrames) / m_fpsAccumulator;
        m_fpsAccumulator = 0.f;
        m_fpsFrames = 0;
    }

    return m_deltaTime;
}

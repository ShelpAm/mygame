#pragma once

#include <chrono>

class GameClock {
public:
    GameClock();

    void restart();
    float tick();

    float deltaTime() const { return m_deltaTime; }
    float totalTime() const { return m_totalTime; }
    int frameCount() const { return m_frameCount; }
    float fps() const { return m_fps; }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_lastTick;
    float m_deltaTime = 0.f;
    float m_totalTime = 0.f;
    float m_fpsAccumulator = 0.f;
    int m_frameCount = 0;
    int m_fpsFrames = 0;
    float m_fps = 0.f;
    static constexpr float FPS_UPDATE_INTERVAL = 0.5f;
};

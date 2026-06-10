#include "core/game-clock.hpp"
GameClock::GameClock()
{
    last_tick_ = Clock::now();
}

void GameClock::restart()
{
    last_tick_ = Clock::now();
    delta_time_ = 0.f;
    total_time_ = 0.f;
    frame_count_ = 0;
}

float GameClock::tick()
{
    auto now = Clock::now();
    auto elapsed = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;

    delta_time_ = std::min(elapsed, 0.1f);
    total_time_ += delta_time_;
    ++frame_count_;

    fps_accumulator_ += delta_time_;
    ++fps_frames_;
    if (fps_accumulator_ >= fps_update_interval) {
        fps_ = static_cast<float>(fps_frames_) / fps_accumulator_;
        fps_accumulator_ = 0.f;
        fps_frames_ = 0;
    }

    return delta_time_;
}

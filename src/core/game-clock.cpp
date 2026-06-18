#include "core/game-clock.hpp"
Stopwatch::Stopwatch()
{
    last_tick_ = Clock::now();
}

void Stopwatch::restart()
{
    last_tick_ = Clock::now();
    delta_time_ = 0.F;
    total_time_ = 0.F;
    frame_count_ = 0;
}

float Stopwatch::tick()
{
    auto now = Clock::now();
    delta_time_ = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;

    total_time_ += delta_time_;
    ++frame_count_;

    fps_accumulator_ += delta_time_;
    ++fps_frames_;
    if (fps_accumulator_ >= fps_update_interval) {
        fps_ = static_cast<float>(fps_frames_) / fps_accumulator_;
        fps_accumulator_ = 0.F;
        fps_frames_ = 0;
    }

    return delta_time_;
}

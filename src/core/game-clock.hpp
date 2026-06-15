#pragma once

#include <chrono>

class Stopwatch {
  public:
    Stopwatch();

    void restart();

    /// @return Delta time since last tick.
    float tick();

    float delta_time() const
    {
        return delta_time_;
    }
    float total_time() const
    {
        return total_time_;
    }
    int frame_count() const
    {
        return frame_count_;
    }
    float fps() const
    {
        return fps_;
    }

  private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint last_tick_;
    float delta_time_ = 0.f;
    float total_time_ = 0.f;
    float fps_accumulator_ = 0.f;
    int frame_count_ = 0;
    int fps_frames_ = 0;
    float fps_ = 0.f;
    static constexpr float fps_update_interval = 0.5f;
};

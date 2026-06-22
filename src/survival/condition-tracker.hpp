#pragma once

#include "net/sync-io.hpp"

struct SurvivalState {
    float food = 100.f;   // 0-100, decays ~5/day
    float water = 100.f;  // 0-100, decays ~7/day
    float health = 100.f; // 0-100, decays when food/water at 0
    float energy = 100.f; // 0-100, decays with activity, recovers with sleep

    float equipment_wear = 0.f; // 0-100

    static constexpr float food_decay_per_day = 5.f;
    static constexpr float water_decay_per_day = 7.f;
    static constexpr float health_decay_starving = 10.f;   // per day when food==0 or water==0
    static constexpr float energy_decay_moving = 2.f;      // per hour of movement
    static constexpr float energy_recover_sleeping = 15.f; // per hour of rest

    // food(4) + water(4) + health(4) + energy(4) = 16 bytes
    static constexpr uint16_t kSyncWireSize = 16;

    void write_sync(SyncWriter &w) const
    {
        w.write(food);
        w.write(water);
        w.write(health);
        w.write(energy);
    }
    void read_sync(SyncReader &r)
    {
        food = r.read<float>();
        water = r.read<float>();
        health = r.read<float>();
        energy = r.read<float>();
    }
};

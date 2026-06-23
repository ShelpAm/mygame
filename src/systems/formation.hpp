#pragma once

#include "core/math.hpp"
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

struct FormationContext {
    std::size_t count;
    Vec2f target_facing;
    Vec2f target_position;
    float time; // time elapsed in world
};

struct Formation {
    Formation() = default;
    Formation(Formation const &) = default;
    Formation(Formation &&) = delete;
    Formation &operator=(Formation const &) = default;
    Formation &operator=(Formation &&) = delete;
    virtual ~Formation() = default;
    virtual std::vector<Vec2f> compute_offsets(FormationContext const &ctx) = 0;
    virtual char const *name() const = 0;
};

struct WedgeFormation : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override
    {
        std::vector<Vec2f> result(ctx.count);
        float const s = std::sqrt(static_cast<float>(ctx.count));
        float const angle_deg = 20.F;
        float const angle_rad = angle_deg * std::numbers::pi_v<float> / 180.0F;
        float const tan_half = std::tan(angle_rad);
        float const row_spacing = 60.F;
        float const base_offset = 32.F + 25.F / s;
        float const rx = -ctx.target_facing.y;
        float const ry = ctx.target_facing.x;

        std::size_t row = 0;
        std::size_t cum = 0;
        for (auto i = 0UZ; i < ctx.count; ++i) {
            while (i >= cum + row + 1) {
                cum += row + 1;
                ++row;
            }
            std::size_t row_size = row + 1;
            std::size_t col = i - cum;
            float dist = base_offset + static_cast<float>(row) * row_spacing;
            float half_width = dist * tan_half;
            float local_x = half_width *
                            (2.0F * static_cast<float>(col) + 1.0F - static_cast<float>(row_size)) /
                            static_cast<float>(row_size);
            float local_y = -dist;
            result[i] = {local_x * rx + local_y * ctx.target_facing.x,
                         local_x * ry + local_y * ctx.target_facing.y};
        }
        return result;
    }
    char const *name() const override { return "Wedge"; }
};

struct LineFormation : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override
    {
        std::vector<Vec2f> result(ctx.count);
        float const s = std::sqrt(static_cast<float>(ctx.count));
        float const spacing = 18.F + 45.F / s;
        float const total = (ctx.count - 1) * spacing;
        float const rx = -ctx.target_facing.y;
        float const ry = ctx.target_facing.x;
        for (auto i = 0UZ; i < ctx.count; ++i) {
            float x = -total / 2.F + i * spacing;
            result[i] = {x * rx - 40.F * ctx.target_facing.x, x * ry - 40.F * ctx.target_facing.y};
        }
        return result;
    }
    char const *name() const override { return "Line"; }
};

struct CircleFormation : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override
    {
        std::vector<Vec2f> result(ctx.count);
        float const s = std::sqrt(static_cast<float>(ctx.count));
        float const radius = 28.F + 22.F * s;
        if (ctx.count <= 1) {
            if (ctx.count == 1)
                result[0] = {0, -radius};
            return result;
        }
        float const rx = -ctx.target_facing.y;
        float const ry = ctx.target_facing.x;
        for (int i = 0; i < ctx.count; ++i) {
            float angle = static_cast<float>(i) / ctx.count * std::numbers::pi_v<float> * 2.F;
            float lx = std::cos(angle) * radius;
            float ly = std::sin(angle) * radius - radius;
            result[i] = {lx * rx + ly * ctx.target_facing.x, lx * ry + ly * ctx.target_facing.y};
        }
        return result;
    }
    char const *name() const override { return "Circle"; }
};

// ljf
struct SquareFormation : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override
    {
        std::vector<Vec2f> result(ctx.count);
        float const s = std::sqrt(static_cast<float>(ctx.count));
        float const spacing = 18.F + 45.F / s;
        int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(ctx.count))));
        int rows = static_cast<int>(std::ceil(static_cast<float>(ctx.count) / cols));
        float total_w = (cols - 1) * spacing;
        float total_h = (rows - 1) * spacing;
        float const rx = -ctx.target_facing.y;
        float const ry = ctx.target_facing.x;

        for (int i = 0; i < ctx.count; ++i) {
            int row = i / cols;
            int col = i % cols;
            float local_x = -total_w / 2.f + col * spacing;
            float local_y = -total_h / 2.f + row * spacing - 40.f;
            result[i] = {local_x * rx + local_y * ctx.target_facing.x,
                         local_x * ry + local_y * ctx.target_facing.y};
        }
        return result;
    }
    char const *name() const override { return "Square"; }
};

struct OuterCircleFormation : Formation {

    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override
    {
        std::vector<Vec2f> result(ctx.count);
        if (ctx.count == 0)
            return result;

        float const s = std::sqrt(static_cast<float>(ctx.count));
        float const radius = 28.F + 22.F * s;
        float const rx = -ctx.target_facing.y;
        float const ry = ctx.target_facing.x;

        for (int i = 0; i < ctx.count; ++i) {
            float angle = (i * 2.F * std::numbers::pi_v<float>) / ctx.count * ctx.time / 5.F;
            float x = std::sin(angle) * radius;
            float y = std::cos(angle) * radius;
            result[i] = {x * rx + y * ctx.target_facing.x, x * ry + y * ctx.target_facing.y};
        }
        return result;
    }
    char const *name() const override { return "OuterCircle"; }
};

struct SnakeFormation : Formation {
    mutable std::deque<Vec2f> path;

    auto compute_offsets(FormationContext const &ctx) -> std::vector<Vec2f> override
    {
        std::vector<Vec2f> result(ctx.count);
        if (ctx.count == 0)
            return result;

        float const s = std::sqrt(static_cast<float>(ctx.count));
        float const distance_per_unit = 22.F + 32.F / s;

        // Record leader position when they actually move
        if (path.empty()) {
            path.push_back(ctx.target_position);
        }
        else {
            float const move_dist = (ctx.target_position - path.back()).length();
            if (move_dist > 2.0F) {
                path.push_back(ctx.target_position);
            }
        }

        int soldier_idx = 0;
        float current_path_dist = 0.f;

        // Segment 1: leader's current position → most recent breadcrumb
        {
            Vec2f const &p1 = ctx.target_position;
            Vec2f const &p2 = path.back();
            float const seg_len = (p1 - p2).length();

            while (soldier_idx < ctx.count) {
                float const target_dist = (soldier_idx + 1) * distance_per_unit;
                if (current_path_dist + seg_len >= target_dist) {
                    float const t = (target_dist - current_path_dist) / (seg_len + 1e-5F);
                    result[soldier_idx] = (p1 + (p2 - p1) * t) - ctx.target_position;
                    soldier_idx++;
                }
                else {
                    break;
                }
            }
            current_path_dist += seg_len;
        }

        // Segment 2: trace backward through historical breadcrumbs
        if (soldier_idx < ctx.count && path.size() > 1) {
            for (int i = static_cast<int>(path.size()) - 1; i > 0; --i) {
                Vec2f const &p1 = path[i];
                Vec2f const &p2 = path[i - 1];
                float const seg_len = (p1 - p2).length();

                while (soldier_idx < ctx.count) {
                    float const target_dist = (soldier_idx + 1) * distance_per_unit;
                    if (current_path_dist + seg_len >= target_dist) {
                        float const t = (target_dist - current_path_dist) / (seg_len + 1e-5F);
                        result[soldier_idx] = (p1 + (p2 - p1) * t) - ctx.target_position;
                        soldier_idx++;
                    }
                    else {
                        break;
                    }
                }
                if (soldier_idx >= ctx.count)
                    break;
                current_path_dist += seg_len;
            }
        }

        // Segment 3: extrapolate past the oldest breadcrumb along tail
        // direction
        if (soldier_idx < ctx.count) {
            Vec2f tail_dir = {-ctx.target_facing.x, -ctx.target_facing.y};
            Vec2f tail_pos = ctx.target_position;

            if (path.size() >= 2) {
                tail_pos = path.front();
                Vec2f d = path[0] - path[1];
                float len = d.length();
                tail_dir = len > 1e-5F ? d * (1.F / len) : tail_dir;
            }
            else if (!path.empty()) {
                tail_pos = path.front();
            }

            for (; soldier_idx < ctx.count; ++soldier_idx) {
                float const target_dist = (soldier_idx + 1) * distance_per_unit;
                float const extra_dist = target_dist - current_path_dist;
                Vec2f const target_pos = tail_pos + tail_dir * extra_dist;
                result[soldier_idx] = target_pos - ctx.target_position;
            }
        }

        // Keep path bounded
        size_t const max_safe_points = static_cast<size_t>(ctx.count) * 3 + 60;
        while (path.size() > max_safe_points)
            path.pop_front();

        return result;
    }

    char const *name() const override { return "Snake"; }
};

struct KineticWings : Formation {
    auto compute_offsets(FormationContext const &ctx) -> std::vector<Vec2f> override
    {
        std::vector<Vec2f> result(ctx.count);
        if (ctx.count == 0)
            return result;

        Vec2f const forward = ctx.target_facing;
        Vec2f const right = {-forward.y, forward.x};
        Vec2f const back = {-forward.x, -forward.y};

        // 🌟 自适应：根据总人数动态收敛横向间距，防止人多时翅膀飞出屏幕
        float const spread_factor = 50.f / std::log(static_cast<float>(ctx.count) + 1.5f);

        for (int i = 0; i < ctx.count; ++i) {
            int const side = (i % 2 == 0) ? 1 : -1; // 1为右翼，-1为左翼
            int const feather_idx = i / 2;          // 这一侧的第几根羽毛

            float const t = static_cast<float>(feather_idx + 1);

            // 基础骨架：利用抛物线方程 y = ax^2 让翅膀自然向后掠
            float const x = t * spread_factor * side;
            float const y = (t * t * 1.8f) + 25.f;

            // 🌟
            // 动态呼吸：越往外侧的羽毛（t越大），扇动幅度越大，且带有波动相位差
            float const flap_wave = std::sin(ctx.time * 4.5f - t * 0.4f) * (2.f + t * 1.8f);

            result[i] = (right * x) + (back * (y + flap_wave));
        }
        return result;
    }

    auto name() const -> char const * override { return "Kinetic Wings"; }
};

struct LivingBuzzsaw : Formation {
    float const unit_size = 48.f;
    float const minion_max_speed = 220.f; // 小兵移速上限

    auto compute_offsets(FormationContext const &ctx) -> std::vector<Vec2f> override
    {
        std::vector<Vec2f> result(ctx.count);
        if (ctx.count == 0)
            return result;

        // 4秒一个“蓄速-超载”循环
        float const cycle = std::fmod(ctx.time, 4.f);

        // 🌟 动态半径与速度的完美物理互补
        float radius_mod = 0.f;
        if (cycle > 2.5f && cycle < 3.0f) {
            // 超载扩张：向外锯开
            float const t = (cycle - 2.5f) / 0.5f;
            radius_mod =
                std::sin(t * std::numbers::pi_v<float> * 0.5f) * 40.f; // 额外向外扩张40像素
        }
        else if (cycle >= 3.0f) {
            // 缩回锯片
            float const t = (cycle - 3.0f) / 1.0f;
            radius_mod = 40.f * (1.f - t);
        }

        for (int i = 0; i < ctx.count; ++i) {
            // 1.
            // 分层逻辑：奇数兵在内圈，偶数兵在外圈，错开排布防止48像素刚体自相残杀
            int const layer = i % 2;
            float const base_radius = (layer == 0) ? unit_size : (unit_size * 1.8f);
            float const final_radius = base_radius + radius_mod;

            // 2. 核心物理限速：根据当前半径，反推小兵能承受的最大角速度
            float const max_safe_omega = minion_max_speed / final_radius;

            // 3. 让旋转带有非线性的“转速切分”
            float const current_rot = ctx.time * (max_safe_omega * 0.9f);

            // 内外圈逆向旋转，绞肉感直接翻倍
            float const direction = (layer == 0) ? 1.f : -1.f;
            float const angle =
                (2.f * std::numbers::pi_v<float> * i) / ctx.count + (current_rot * direction);

            result[i] = {final_radius * std::cos(angle), final_radius * std::sin(angle)};
        }
        return result;
    }

    auto name() const -> char const * override { return "Living Buzzsaw"; }
};

struct SafeLivingGreatsword : Formation {
    float const unit_size = 48.f;
    float const minion_max_speed = 220.f; // 🌟 明确指出小兵的物理移速上限

    auto compute_offsets(FormationContext const &ctx) -> std::vector<Vec2f> override
    {
        std::vector<Vec2f> result(ctx.count);
        if (ctx.count == 0)
            return result;

        // 1. 动态计算剑的最大长度 R
        float const sword_length = unit_size + (ctx.count / 2) * 52.f;

        // 2. 根据物理公式 V = w * R -> w = V / R 限速
        // 挥砍角速度绝对不能超过这个上限，否则剑尖小兵物理上绝对追不上！
        float const max_omega = minion_max_speed / (sword_length + 1e-5f);

        // 设定一个安全的挥砍速度（不超过物理极限的 85%，留点富余给刚体挤压）
        float const swing_speed = std::min(4.5f, max_omega * 0.85f);

        // 3. 基于安全速度计算旋转周期
        float const period = (2.f * std::numbers::pi_v<float>) / (swing_speed + 0.1f);
        float const local_time = std::fmod(ctx.time, period);
        float angle_offset = 0.f;

        // 4. 蓄力与安全挥砍
        if (local_time < period * 0.6f) {
            angle_offset = std::sin(ctx.time * 40.f) * 0.03f; // 战栗蓄力
        }
        else {
            float const t = (local_time - period * 0.6f) / (period * 0.4f);
            angle_offset = -0.8f + 1.6f * t; // 在安全角速度内平滑挥砍
        }

        Vec2f const base_fwd = ctx.target_facing;
        float const final_angle = std::atan2(base_fwd.y, base_fwd.x) + angle_offset;
        Vec2f const fwd{std::cos(final_angle), std::sin(final_angle)};
        Vec2f const side{-fwd.y, fwd.x};

        for (int i = 0; i < ctx.count; ++i) {
            if (i == 0) {
                result[i] = fwd * sword_length; // 剑尖
            }
            else if (i % 2 == 0) {
                result[i] = fwd * (unit_size + (i / 2) * 52.f); // 剑身
            }
            else {
                int const wing = (i % 4 < 2) ? 1 : -1;
                int const rank = (i + 1) / 4;
                result[i] = fwd * 55.f + side * (wing * rank * 50.f); // 护手
            }
        }
        return result;
    }

    auto name() const -> char const * override { return "Safe Greatsword"; }
};
// Registry: add new formations here (name, factory). One line per formation.
inline auto &formation_registry()
{
    static std::vector<std::pair<char const *, std::function<std::unique_ptr<Formation>()>>> reg =
        [] {
            decltype(reg) res;
            auto add = [&res](auto const &t) {
                res.push_back({t.name(), []() {
                                   return std::make_unique<std::remove_cvref_t<decltype(t)>>();
                               }});
            };
            add(WedgeFormation{});
            add(LineFormation{});
            add(CircleFormation{});
            add(SquareFormation{});
            add(OuterCircleFormation{});
            add(SnakeFormation{});
            add(KineticWings{});
            add(LivingBuzzsaw{});
            add(SafeLivingGreatsword{});
            return res;
        }();
    return reg;
}

#include "systems/formation.hpp"
#include <cmath>
#include <numbers>

// ── WedgeFormation ──────────────────────────────────────────────────────────

std::vector<Vec2f> WedgeFormation::compute_offsets(FormationContext const &ctx)
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

char const *WedgeFormation::name() const { return "Wedge"; }

// ── LineFormation ───────────────────────────────────────────────────────────

std::vector<Vec2f> LineFormation::compute_offsets(FormationContext const &ctx)
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

char const *LineFormation::name() const { return "Line"; }

// ── CircleFormation ─────────────────────────────────────────────────────────

std::vector<Vec2f> CircleFormation::compute_offsets(FormationContext const &ctx)
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

char const *CircleFormation::name() const { return "Circle"; }

// ── SquareFormation ─────────────────────────────────────────────────────────

std::vector<Vec2f> SquareFormation::compute_offsets(FormationContext const &ctx)
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

char const *SquareFormation::name() const { return "Square"; }

// ── OuterCircleFormation ────────────────────────────────────────────────────

std::vector<Vec2f> OuterCircleFormation::compute_offsets(FormationContext const &ctx)
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

char const *OuterCircleFormation::name() const { return "OuterCircle"; }

// ── SnakeFormation ──────────────────────────────────────────────────────────

std::vector<Vec2f> SnakeFormation::compute_offsets(FormationContext const &ctx)
{
    std::vector<Vec2f> result(ctx.count);
    if (ctx.count == 0)
        return result;

    float const s = std::sqrt(static_cast<float>(ctx.count));
    float const distance_per_unit = 22.F + 32.F / s;

    if (path.empty())
        path.push_back(ctx.target_position);
    else {
        float const move_dist = (ctx.target_position - path.back()).length();
        if (move_dist > 2.0F)
            path.push_back(ctx.target_position);
    }

    int soldier_idx = 0;
    float current_path_dist = 0.f;

    // Segment 1: leader → most recent breadcrumb
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
            else
                break;
        }
        current_path_dist += seg_len;
    }

    // Segment 2: trace backward through breadcrumbs
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
                else
                    break;
            }
            if (soldier_idx >= ctx.count)
                break;
            current_path_dist += seg_len;
        }
    }

    // Segment 3: extrapolate past oldest breadcrumb
    if (soldier_idx < ctx.count) {
        Vec2f tail_dir = {-ctx.target_facing.x, -ctx.target_facing.y};
        Vec2f tail_pos = ctx.target_position;

        if (path.size() >= 2) {
            tail_pos = path.front();
            Vec2f d = path[0] - path[1];
            float len = d.length();
            tail_dir = len > 1e-5F ? d * (1.F / len) : tail_dir;
        }
        else if (!path.empty())
            tail_pos = path.front();

        for (; soldier_idx < ctx.count; ++soldier_idx) {
            float const target_dist = (soldier_idx + 1) * distance_per_unit;
            float const extra_dist = target_dist - current_path_dist;
            Vec2f const target_pos = tail_pos + tail_dir * extra_dist;
            result[soldier_idx] = target_pos - ctx.target_position;
        }
    }

    size_t const max_safe_points = static_cast<size_t>(ctx.count) * 3 + 60;
    while (path.size() > max_safe_points)
        path.pop_front();

    return result;
}

char const *SnakeFormation::name() const { return "Snake"; }

// ── KineticWings ────────────────────────────────────────────────────────────

std::vector<Vec2f> KineticWings::compute_offsets(FormationContext const &ctx)
{
    std::vector<Vec2f> result(ctx.count);
    if (ctx.count == 0)
        return result;

    Vec2f const forward = ctx.target_facing;
    Vec2f const right = {-forward.y, forward.x};
    Vec2f const back = {-forward.x, -forward.y};

    float const spread_factor = 50.f / std::log(static_cast<float>(ctx.count) + 1.5f);

    for (int i = 0; i < ctx.count; ++i) {
        int const side = (i % 2 == 0) ? 1 : -1;
        int const feather_idx = i / 2;
        float const t = static_cast<float>(feather_idx + 1);
        float const x = t * spread_factor * side;
        float const y = (t * t * 1.8f) + 25.f;
        float const flap_wave = std::sin(ctx.time * 4.5f - t * 0.4f) * (2.f + t * 1.8f);

        result[i] = (right * x) + (back * (y + flap_wave));
    }
    return result;
}

auto KineticWings::name() const -> char const * { return "Kinetic Wings"; }

// ── LivingBuzzsaw ───────────────────────────────────────────────────────────

std::vector<Vec2f> LivingBuzzsaw::compute_offsets(FormationContext const &ctx)
{
    std::vector<Vec2f> result(ctx.count);
    if (ctx.count == 0)
        return result;

    float const cycle = std::fmod(ctx.time, 4.f);
    float radius_mod = 0.f;
    if (cycle > 2.5f && cycle < 3.0f) {
        float const t = (cycle - 2.5f) / 0.5f;
        radius_mod = std::sin(t * std::numbers::pi_v<float> * 0.5f) * 40.f;
    }
    else if (cycle >= 3.0f) {
        float const t = (cycle - 3.0f) / 1.0f;
        radius_mod = 40.f * (1.f - t);
    }

    for (int i = 0; i < ctx.count; ++i) {
        int const layer = i % 2;
        float const base_radius = (layer == 0) ? unit_size : (unit_size * 1.8f);
        float const final_radius = base_radius + radius_mod;
        float const max_safe_omega = minion_max_speed / final_radius;
        float const current_rot = ctx.time * (max_safe_omega * 0.9f);
        float const direction = (layer == 0) ? 1.f : -1.f;
        float const angle =
            (2.f * std::numbers::pi_v<float> * i) / ctx.count + (current_rot * direction);

        result[i] = {final_radius * std::cos(angle), final_radius * std::sin(angle)};
    }
    return result;
}

auto LivingBuzzsaw::name() const -> char const * { return "Living Buzzsaw"; }

// ── SafeLivingGreatsword ────────────────────────────────────────────────────

std::vector<Vec2f> SafeLivingGreatsword::compute_offsets(FormationContext const &ctx)
{
    std::vector<Vec2f> result(ctx.count);
    if (ctx.count == 0)
        return result;

    float const sword_length = unit_size + (ctx.count / 2) * 52.f;
    float const max_omega = minion_max_speed / (sword_length + 1e-5f);
    float const swing_speed = std::min(4.5f, max_omega * 0.85f);
    float const period = (2.f * std::numbers::pi_v<float>) / (swing_speed + 0.1f);
    float const local_time = std::fmod(ctx.time, period);
    float angle_offset = 0.f;

    if (local_time < period * 0.6f)
        angle_offset = std::sin(ctx.time * 40.f) * 0.03f;
    else {
        float const t = (local_time - period * 0.6f) / (period * 0.4f);
        angle_offset = -0.8f + 1.6f * t;
    }

    Vec2f const base_fwd = ctx.target_facing;
    float const final_angle = std::atan2(base_fwd.y, base_fwd.x) + angle_offset;
    Vec2f const fwd{std::cos(final_angle), std::sin(final_angle)};
    Vec2f const side{-fwd.y, fwd.x};

    for (int i = 0; i < ctx.count; ++i) {
        if (i == 0)
            result[i] = fwd * sword_length;
        else if (i % 2 == 0)
            result[i] = fwd * (unit_size + (i / 2) * 52.f);
        else {
            int const wing = (i % 4 < 2) ? 1 : -1;
            int const rank = (i + 1) / 4;
            result[i] = fwd * 55.f + side * (wing * rank * 50.f);
        }
    }
    return result;
}

auto SafeLivingGreatsword::name() const -> char const * { return "Safe Greatsword"; }

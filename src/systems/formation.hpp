#pragma once

#include "core/math.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

struct Formation {
    virtual ~Formation() = default;
    virtual Vec2f compute_offset(int index, int count,
                                 Vec2f const &facing) const = 0;
    virtual char const *name() const = 0;
};

struct WedgeFormation : Formation {
    float angle_deg = 30.f;
    float row_spacing = 45.f;
    float base_offset = 50.f;

    Vec2f compute_offset(int index, int /*count*/,
                         Vec2f const &facing) const override
    {
        float angle_rad = angle_deg * 3.1415926535f / 180.0f;
        float tan_half = std::tan(angle_rad);

        int row = 0, cum = 0;
        while (true) {
            int row_size = row + 1;
            if (index < cum + row_size) {
                int col = index - cum;
                float dist = base_offset + row * row_spacing;
                float half_width = dist * tan_half;
                float local_x =
                    row_size == 1
                        ? 0.0f
                        : -half_width +
                              (col + 0.5f) * (2.f * half_width / row_size);
                float local_y = -dist;
                float rx = -facing.y, ry = facing.x;
                return {local_x * rx + local_y * facing.x,
                        local_x * ry + local_y * facing.y};
            }
            cum += row_size;
            ++row;
        }
    }
    char const *name() const override
    {
        return "Wedge";
    }
};

struct LineFormation : Formation {
    float spacing = 50.f;

    Vec2f compute_offset(int index, int count,
                         Vec2f const &facing) const override
    {
        float total = (count - 1) * spacing;
        float x = -total / 2.f + index * spacing;
        float rx = -facing.y, ry = facing.x;
        return {x * rx - 40.f * facing.x, x * ry - 40.f * facing.y};
    }
    char const *name() const override
    {
        return "Line";
    }
};

struct CircleFormation : Formation {
    Vec2f compute_offset(int index, int count,
                         Vec2f const &facing) const override
    {
        float radius = 6.f * count;
        if (count <= 1)
            return {0, -radius};
        float angle = (float)index / count * 6.2831853f;
        float rx = -facing.y, ry = facing.x;
        float lx = std::cos(angle) * radius;
        float ly = std::sin(angle) * radius - radius;
        return {lx * rx + ly * facing.x, lx * ry + ly * facing.y};
    }
    char const *name() const override
    {
        return "Circle";
    }
};

// ljf
struct SquareFormation : Formation {
    float spacing = 50.f;

    Vec2f compute_offset(int index, int count,
                         Vec2f const &facing) const override
    {
        int cols = (int)std::ceil(std::sqrt((float)count));
        int rows = (int)std::ceil((float)count / cols);

        int row = index / cols;
        int col = index % cols;

        float total_w = (cols - 1) * spacing;
        float total_h = (rows - 1) * spacing;

        float local_x = -total_w / 2.f + col * spacing;
        float local_y = -total_h / 2.f + row * spacing - 40.f;

        float rx = -facing.y, ry = facing.x;
        return {local_x * rx + local_y * facing.x,
                local_x * ry + local_y * facing.y};
    }
    char const *name() const override
    {
        return "Square";
    }
};

struct OuterCircleFormation : Formation {

    Vec2f compute_offset(int index, int count,
                         Vec2f const &facing) const override
    {
        if (count == 0)
            return {0, 0};

        float radius = 6.f * count; // 圆圈半径

        // 把 360 度 (2*PI) 根据小兵总数平分
        // 💡 这里的 index 和 count 组合，能让圈永远是完美的正圆
        float angle = (index * 2.f * 3.14159265f) / count;

        // 利用三角函数算出本地圆周坐标
        float x = std::sin(angle) * radius;
        float y = std::cos(angle) * radius;

        // 这里甚至不需要依靠 facing 旋转，因为它本身就是一个自适应的圆
        // 当然如果你希望圆阵跟着玩家朝向一起转动，可以加上 facing 变换：
        float rx = -facing.y, ry = facing.x;
        return {x * rx + y * facing.x, x * ry + y * facing.y};
    }
    char const *name() const override
    {
        return "OuterCircle";
    }
};

struct SnakeFormation : Formation {
    float follow_back_distance = 45.f; // 每一个身体节点离前一个人的距离

    Vec2f compute_offset(int index, int count,
                         Vec2f const &facing) const override
    {
        // 💡 妙招：每个小兵的阵型偏移，其实就是“死死顶在前一个人的正后方”
        // 这样只要外层逻辑让他们往这个 offset 走，结合运动学就会自然拧成一条蛇
        float total_back = (index + 1) * follow_back_distance;

        return {-total_back * facing.x, -total_back * facing.y};
    }
    char const *name() const override
    {
        return "Snake";
    }
};

// Registry: add new formations here (name, factory). One line per formation.
inline auto &formation_registry()
{
    static std::vector<
        std::pair<char const *, std::function<std::unique_ptr<Formation>()>>>
        reg = [] {
            decltype(reg) res;
            auto add = [&res](auto t) {
                res.push_back({t.name(), [t]() {
                                   return std::make_unique<decltype(t)>();
                               }});
            };
            add(WedgeFormation{});
            add(LineFormation{});
            add(CircleFormation{});
            add(SquareFormation{});
            add(OuterCircleFormation{});
            add(SnakeFormation{});
            return res;
        }();
    return reg;
}

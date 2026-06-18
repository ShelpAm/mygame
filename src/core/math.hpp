#pragma once

#include <cmath>
#include <ostream>

struct Vec2f {
    float x{};
    float y{};

    constexpr Vec2f() = default;
    constexpr Vec2f(float x, float y) : x(x), y(y) {}
    Vec2f operator+(Vec2f const &o) const { return {x + o.x, y + o.y}; }
    Vec2f operator-(Vec2f const &o) const { return {x - o.x, y - o.y}; }
    Vec2f operator*(float s) const { return {x * s, y * s}; }
    Vec2f &operator+=(Vec2f const &o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    Vec2f &operator-=(Vec2f const &o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    float length() const { return std::hypot(x, y); }
};

struct Vec2i {
    int x{};
    int y{};

    constexpr Vec2i() = default;
    constexpr Vec2i(int x, int y) : x(x), y(y) {}
    auto operator<=>(Vec2i const &) const = default;
};

inline std::ostream &operator<<(std::ostream &os, Vec2i const &v)
{
    return os << "(" << v.x << ", " << v.y << ")";
}

template <> struct std::hash<Vec2i> {
    std::size_t operator()(Vec2i const &v) const
    {
        return std::hash<int>{}(v.x) ^ (std::hash<int>{}(v.y) << 1);
    }
};

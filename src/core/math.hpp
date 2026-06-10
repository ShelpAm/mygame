#pragma once

#include <cmath>
#include <ostream>

struct Vec2f {
    float x = 0.f;
    float y = 0.f;

    Vec2f operator+(Vec2f const &o) const
    {
        return {x + o.x, y + o.y};
    }
    Vec2f operator-(Vec2f const &o) const
    {
        return {x - o.x, y - o.y};
    }
    Vec2f operator*(float s) const
    {
        return {x * s, y * s};
    }
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
    float length() const
    {
        return std::hypot(x, y);
    }
};

struct Vec2i {
    int x = 0;
    int y = 0;
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

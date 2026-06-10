#pragma once

#include <cmath>
#include <compare>
#include <functional>
#include <ostream>

struct Vec2f {
    float x = 0.f;
    float y = 0.f;

    Vec2f operator+(const Vec2f& o) const { return {x + o.x, y + o.y}; }
    Vec2f operator-(const Vec2f& o) const { return {x - o.x, y - o.y}; }
    Vec2f operator*(float s) const { return {x * s, y * s}; }
    Vec2f& operator+=(const Vec2f& o) { x += o.x; y += o.y; return *this; }
    Vec2f& operator-=(const Vec2f& o) { x -= o.x; y -= o.y; return *this; }
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Vec2i {
    int x = 0;
    int y = 0;
    auto operator<=>(const Vec2i&) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const Vec2i& v) {
    return os << "(" << v.x << ", " << v.y << ")";
}

template <>
struct std::hash<Vec2i> {
    std::size_t operator()(const Vec2i& v) const {
        return std::hash<int>{}(v.x) ^ (std::hash<int>{}(v.y) << 1);
    }
};

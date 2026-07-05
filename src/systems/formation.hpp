#pragma once

#include "core/math.hpp"
#include <deque>
#include <functional>
#include <memory>
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
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    char const *name() const override;
};

struct LineFormation : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    char const *name() const override;
};

struct CircleFormation : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    char const *name() const override;
};

struct SquareFormation : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    char const *name() const override;
};

struct OuterCircleFormation : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    char const *name() const override;
};

struct SnakeFormation : Formation {
    mutable std::deque<Vec2f> path;
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    char const *name() const override;
};

struct KineticWings : Formation {
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    auto name() const -> char const * override;
};

struct LivingBuzzsaw : Formation {
    float const unit_size = 48.f;
    float const minion_max_speed = 220.F;
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    auto name() const -> char const * override;
};

struct SafeLivingGreatsword : Formation {
    float const unit_size = 48.f;
    float const minion_max_speed = 220.f;
    std::vector<Vec2f> compute_offsets(FormationContext const &ctx) override;
    auto name() const -> char const * override;
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

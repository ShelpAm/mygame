#pragma once

#include <array>
#include <optional>
#include <rfl/yaml.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// 1. 战斗组件配置
struct CombatConfig {
    int max_hp = 0;
    int hp = 0;
    int attack = 0;
    int defense = 0;
    float attack_range = 0.0f;
};

// 2. 移动组件配置
struct MovementConfig {
    float max_speed = 0.0f;
};

// 3. 碰撞体配置 (用 std::array 完美对应 YAML 的 [-10, -32] 数组)
struct ColliderConfig {
    std::array<float, 2> min{0.0f, 0.0f};
    std::array<float, 2> max{0.0f, 0.0f};
};

// 4. 视野配置
struct VisionConfig {
    float range = 0.0f;
    float arc = 360.0f;
};

// 5. 渲染/视觉配置
struct VisualConfig {
    std::array<float, 2> origin{0.5f, 0.5f};
    float scale = 1.0f;
};

// 6. AI 行为树配置
struct AiConfig {
    float engage_range = 0.0f;
    std::string stance;
};

// 7. 单个实体的完整配置
// 🌟 核心：除了必有的 clip，其余全用 std::optional 包装。
// 如果 YAML 里没写某个模块，reflect-cpp 会自动将其设为 std::nullopt！
struct EntityConfig {
    std::optional<std::string> clip;
    std::optional<CombatConfig> combat;
    std::optional<MovementConfig> movement;
    std::optional<ColliderConfig> collider;
    std::optional<VisionConfig> vision;
    std::optional<VisualConfig> visual;
    std::optional<AiConfig> ai;
    std::optional<float> interact_radius;
};

// 8. 整个 YAML 文件的根结构
// YAML 的最外层是一个以“实体类型名”（如 player, building）为 Key 的 Map
using EntityDatabase = std::unordered_map<std::string, EntityConfig>;

inline EntityDatabase read_entity_database_from_yaml(std::string const &yaml_path)
{
    std::ifstream ifs(yaml_path);
    auto obj = rfl::yaml::read<EntityDatabase>(ifs);
    return obj.value();
}

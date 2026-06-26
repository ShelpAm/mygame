# 城镇系统规划

## Context

玩家反馈当前游戏「无聊」——世界太空旷、NPC 不生动、缺乏目标和成长感。需要一个大规模城镇系统来填充世界、增加游戏深度。

**目标**：分 5 个阶段，从「让世界看起来像个世界」到完整的城镇经济/建设系统。每个阶段独立可玩。

---

## 整体架构

```
初始化:
  locations.json ──> terrain_generator ──> NavigationSystem (blocked tiles)
                                      ──> TileGrid (client rendering)

运行时:
  GameMode (server thread)
    ├─ WorldState::LocationState (town data)
    ├─ check_player_town_proximity() → town_discovered message
    ├─ NPC schedule system (Phase 3)
    ├─ Settlement system (Phase 4)
    └─ Economy/trade system (Phase 5)

  Client (render thread)
    ├─ TileGrid → render_town_overlays / render_town_labels
    ├─ UIManager → discovery toasts, town HUD, minimap
    └─ Town service UI (Phase 2)
```

所有阶段保持：服务器权威逻辑 + 客户端渲染分离的架构。新增 `ServerMsgType`/`ClientMsgType` 消息走现有网络通道。

---

## Phase 1：让世界活起来（最高优先级）

**目标**：空白草地变成有城镇、道路、地标的世界。看到地平线上的建筑群、知道那是「Ugarit」。

### 1.1 加载城镇数据

| 文件 | 动作 |
|------|------|
| `src/world/location-store.hpp` (NEW) | `LocationDefinition` 结构体 + `load_locations_from_json()` |
| `src/world/world-state.hpp` | `LocationState` 扩展：`tile_center`, `discovery_radius`, `npc_ids`；新增 `current_town()`, `discover_location()` |
| `src/world/world-state.cpp` | 实现玩家坐标与城镇中心的距离检测 |
| `src/core/game-mode.hpp` | 新增 `load_locations()`, `check_player_town_proximity()`, 成员 `location_defs_` |
| `src/core/game-mode.cpp` | 在 `init_world()` 中调用 `load_locations()` + terrain gen；在 `update()` 中检测玩家进入/离开城镇 |
| `src/net/net-packet.hpp` | 新增 `ServerMsgType::town_discovered` |
| `src/net/server.cpp` | 广播城镇发现消息 |
| `src/net/client.cpp` | 处理 `town_discovered` 消息 |

### 1.2 地形生成

| 文件 | 动作 |
|------|------|
| `src/world/terrain-generator.hpp` (NEW) | `TownFootprint` 结构体 + `generate_town_footprints()` |
| `src/world/terrain-generator.cpp` (NEW) | 对每个城镇：生成建筑瓦片簇 + 连接道路（Bresenham线算法） |
| `src/world/tile-grid.hpp` (NEW) | 客户端瓦片类型存储（mirrors server terrain for rendering） |
| `src/core/app.hpp` | 新增 `TileGrid tile_grid_` 成员 |
| `src/core/app.cpp` | 客户端也加载 locations.json→生成 TileGrid |

**地形生成逻辑**：
- 从 `locations.json` 读取每个城镇的坐标和路线
- 在城镇中心位置生成 3-8 个建筑瓦片（`type=4`, `walkable=false`, `blocks_vision=true`）
- 用 Bresenham 在路线两端点之间生成道路瓦片（`type=3`, `walkable=true`）
- 大型城镇更大，小城镇小一些

### 1.3 视觉渲染

| 文件 | 动作 |
|------|------|
| `src/systems/render-system.hpp` | 新增 `render_town_overlays()`, `render_town_labels()` |
| `src/systems/render-system.cpp` | 在瓦片渲染层之上绘制建筑/道路/水域叠层；在城镇上方绘制名称标签 |
| `assets/textures/tilemap/building.png` (NEW) | 建筑覆盖纹理 |
| `assets/textures/tilemap/road.png` (NEW) | 道路纹理 |
| `assets/textures/tilemap/water.png` (NEW) | 水域纹理（后续复用） |

### 1.4 发现+UI

| 文件 | 动作 |
|------|------|
| `src/ui/ui-manager.hpp` | 新增 `discovery_queue_`, `notification_timer_` |
| `src/ui/ui-manager.cpp` | 城镇发现弹窗提示；HUD 顶部显示当前城镇名；地图窗口显示城镇标记 |
| `src/map/` (后续) | 完整地图界面——现存的 `render_map()` stub 替换为实际标记 |

### Phase 1 验收标准

- [ ] 玩家走在地图上能看到不同的建筑集群（对应 4 个城镇坐标）
- [ ] 城镇之间有道路连接
- [ ] 首次接近城镇时弹出「发现 Ugarit」提示
- [ ] HUD 顶部显示当前所在城镇
- [ ] 地图窗口显示城镇位置标记
- [ ] NPC 可以拥有 `location_id` 字段关联城镇

---

## Phase 2：城镇功能

**依赖**：Phase 1 完成

### 2.1 城镇服务

| 新增/修改 | 内容 |
|-----------|------|
| `src/entities/components/interactable.hpp` | 新增 `TownService` component（inn/market/temple/blacksmith） |
| `src/core/game-mode.cpp` | 实现 `rest_at_inn()`（回能量）、`buy_supplies()`、`visit_temple()` |
| `src/ui/ui-manager.cpp` | 服务交互 UI（[E] 住宿、[E] 购买） |

### 2.2 建筑实体

| 新增/修改 | 内容 |
|-----------|------|
| `src/entities/components/building-data.hpp` (NEW) | 通用 `BuildingData` component（替代 DefenseStructure），含类型/可进入/关联NPC |
| `src/core/game-mode.cpp` | 在城镇位置生成 `EntityKind::structure` 实体，附加 BuildingData 组件 |

### 2.3 网络同步

| 文件 | 内容 |
|------|------|
| `src/net/net-packet.hpp` | 新增 `ClientMsgType::use_service`, `ServerMsgType::service_result` |
| `SyncComponent` | 新建 `building = 1 << 8` 位用于 BuildingData 同步 |

---

## Phase 3：NPC 的日常生活

**依赖**：Phase 2 完成

### 3.1 日程系统

| 文件 | 内容 |
|------|------|
| `src/entities/components/npc-state.hpp` | 扩展 NPCState：`ScheduleEntry` 列表（时间→位置→动作）、`roam_anchor/radius` |
| `src/systems/npc-scheduler-system.hpp` (NEW) | 每日循环系统：6-8点起床、8-18点工作/巡逻、18-21点社交、21-6点回家 |
| `src/core/game-mode.cpp` | 注册 `NpcSchedulerSystem` |

### 3.2 NPC 漫游+社交

| 文件 | 内容 |
|------|------|
| `npc-scheduler-system.cpp` | 空闲时段随机漫步（pathfind to random walkable tile）；社交时段 NPC 两两聚集交谈 |

### 3.3 NPC 对事件的反应

| 文件 | 内容 |
|------|------|
| `event-simulator.cpp` | 事件（战斗、瘟疫）触发时，受影响城镇的 NPC 逃往边缘或迁移 |

---

## Phase 4：玩家定居点建设

**依赖**：Phase 2 完成（Phase 3 可选）

### 4.1 建造系统

| 文件 | 内容 |
|------|------|
| `src/systems/building-system.hpp` (NEW) | 建筑模式切换、放置预览、合法性校验 |
| `src/core/game-mode.cpp` | `place_building()`, `demolish_building()` |
| `src/entities/components/settlement.hpp` (NEW) | `Settlement` component：资源/人口/建筑列表 |
| `src/ui/ui-manager.cpp` | 建造面板 UI：可选建筑+消耗资源的网格 |

### 4.2 防御结构

| 文件 | 内容 |
|------|------|
| `combat-system.cpp` | 城墙阻挡敌人，瞭望塔自动攻击（复用 projectile 系统） |

### 4.3 网络同步

| 文件 | 内容 |
|------|------|
| `net-packet.hpp` | 新增 `ClientMsgType::place_building` |

---

## Phase 5：经济与成长

**依赖**：Phase 2 完成（Phase 3/4 可选）

### 5.1 交易系统

| 文件 | 内容 |
|------|------|
| `location-store.hpp` | 扩展价格表（买入/卖出价，受派系权力+事件影响） |
| `src/systems/trade-system.hpp` (NEW) | 交易计算、路线危险度检查 |
| `src/ui/ui-manager.cpp` | 交易 UI：商品列表、价格、玩家库存 |

### 5.2 背包系统

| 文件 | 内容 |
|------|------|
| `src/entities/components/inventory.hpp` (NEW) | `Inventory` component：物品槽位/重量限制 |
| `ui-manager.cpp` | 替换现有的 inventory stub |

### 5.3 合成系统

| 文件 | 内容 |
|------|------|
| `assets/data/recipes.json` (NEW) | 配方表（输入→输出、所需工作台） |
| `src/systems/crafting-system.hpp` (NEW) | 合成逻辑 |

### 5.4 城镇发展

| 文件 | 内容 |
|------|------|
| `src/systems/town-development-system.hpp` (NEW) | 城镇人口/财富/安全度随时间变化；建筑增减 |
| `terrain-generator.cpp` | 动态更新地形瓦片（城镇繁荣→新建筑、衰落→废弃） |

---

## 第一阶段实施步骤

1. **`location-store.hpp`** — 数据结构 + JSON 加载函数
2. **`world-state.hpp/.cpp`** — 扩展 LocationState + current_town() 检测
3. **`terrain-generator.hpp/.cpp`** — 城镇足迹生成 + 道路连接
4. **`tile-grid.hpp`** — 客户端瓦片类型存储
5. **`game-mode.hpp/.cpp`** — 加载位置 + 生成地形 + 进入城镇检测
6. **`app.hpp/.cpp`** — 客户端加载 locations.json → 生成 TileGrid
7. **`render-system.hpp/.cpp`** — 建筑/道路覆盖渲染 + 城镇名称标签
8. **`net-packet.hpp`** — 新增 `ServerMsgType::town_discovered`
9. **`server.cpp`** — 广播城镇发现
10. **`client.cpp`** — 处理发现消息
11. **`ui-manager.hpp/.cpp`** — 发现弹窗 + HUD 城镇名 + 地图标记
12. **新建纹理** — 建筑/道路/水域贴图
12. **更新 CMakeLists.txt** — 添加新源文件

---

## 验证

1. 编译通过，所有测试通过
2. 启动游戏后能看到城镇建筑群和道路
3. 走进城镇能看到名字显示 + 发现提示
4. NPC 在对应城镇附近生成
5. 地图窗口显示城镇位置
6. 后期阶段：功能按阶段验收

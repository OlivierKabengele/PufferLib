#ifndef NMMO_STRATEGIC_CONTEXT_H
#define NMMO_STRATEGIC_CONTEXT_H

#include <stddef.h>

/* Include this AFTER nmmo3.h in the translation unit that implements the MMO.
 * This header deliberately forward-declares MMO to avoid re-including nmmo3.h,
 * which contains definitions rather than declarations.
 */
typedef struct MMO MMO;

#define SC_SCHEMA_VERSION 1
#define SC_MAX_NEARBY 48
#define SC_MAX_INVENTORY 12
#define SC_MAX_MARKET 32
#define SC_MAX_TILE_COUNTS 16

typedef struct {
    int id, type, tier, slot, equipped;
} SCInventoryEntry;

typedef struct {
    int entity_id, type, r, c, distance, hp, hp_max;
    int combat_level, element, ranged, in_combat;
} SCNearbyEntity;

typedef struct {
    int id, type, tier, r, c, distance;
    int requires_tool, harvestable;
} SCNearbyItem;

typedef struct {
    int item_id, type, tier, stock, asking_price;
    int affordable;
} SCMarketEntry;

typedef struct {
    int schema_version, tick, player_id;
    int map_width, map_height, sight_radius;
    int r, c, hp, hp_max, combat_level, profession_level;
    int element, direction, ui_mode, market_tier, sell_index;
    int gold, in_combat, ranged, goal;
    int equipment_attack, equipment_defense;
    int equipment[5];
    int free_inventory_slots, inventory_count;
    SCInventoryEntry inventory[SC_MAX_INVENTORY];
    int nearby_entity_count, nearby_entity_total;
    SCNearbyEntity nearby_entities[SC_MAX_NEARBY];
    int nearby_item_count, nearby_item_total;
    SCNearbyItem nearby_items[SC_MAX_NEARBY];
    int terrain_counts[SC_MAX_TILE_COUNTS];
    int walkable_adjacent[4]; /* down, up, right, left */
    int market_count;
    SCMarketEntry market[SC_MAX_MARKET];
    int max_tiers, max_levels;
    int enemy_aggro_range, in_combat_ticks;
    int can_attack, has_tool, held_tool_tier, can_buy_or_sell;
    int nearest_enemy_distance, nearest_resource_distance;
} StrategicContext;

/* Reads authoritative world state; radius <= 0 uses configured observation window.
 * Only entities and ground items within the configured radius are reported.
 * Returns 0 on success, -1 on invalid arguments or inactive player.
 */
int strategic_context_collect(const MMO* env, int player_id, int radius,
                              StrategicContext* out);

/* Serialize snapshot to valid JSON. Returns byte count (excluding trailing NUL),
 * or -1 if output capacity is insufficient or an argument is invalid.
 */
int strategic_context_to_json(const StrategicContext* ctx, char* dst,
                              size_t capacity);

/* Convenience collection + serialization; same negative error convention. */
int strategic_context_build_json(const MMO* env, int player_id, int radius,
                                 char* dst, size_t capacity);

/* Optional static guidance describing feasible high-level plan output.
 * The LLM should generate JSON with objective, subgoals and action preferences,
 * not raw policy output or assumptions about unseen map cells.
 */
const char* strategic_context_planner_contract(void);

#endif

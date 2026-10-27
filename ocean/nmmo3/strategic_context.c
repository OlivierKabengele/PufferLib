/* Compile in ONE translation unit where nmmo3.h has already been included:
 * #include "nmmo3.h"
 * #include "strategic_context.c"
 * nmmo3.h is a definition-heavy header and must not be duplicated across TUs.
 */
#include "strategic_context.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int sc_abs(int x) { return x < 0 ? -x : x; }
static int sc_item_type(int id) { return id > 0 ? (id - 1) % I_N + 1 : I_NULL; }
static int sc_item_tier(int id) { return id > 0 ? (id - 1) / I_N + 1 : 0; }
static int sc_valid(const MMO* env, int r, int c) {
    return r >= 0 && c >= 0 && r < env->height && c < env->width;
}
static int sc_walkable(const MMO* env, int r, int c) {
    if (!sc_valid(env, r, c)) return 0;
    int idx = r * env->width + c;
    int tile = (unsigned char)env->terrain[idx];
    return tile < 16 && tile < 8 && (!env->pids || env->pids[idx] == -1);
}

int strategic_context_collect(const MMO* env, int player_id, int radius,
                              StrategicContext* out) {
    if (!env || !out || !env->players || !env->terrain || !env->items ||
        player_id < 0 || player_id >= env->num_agents ||
        env->width <= 0 || env->height <= 0) return -1;
    const Entity* p = &env->players[player_id];
    if (p->type != ENTITY_PLAYER || !sc_valid(env, p->r, p->c)) return -1;
    memset(out, 0, sizeof(*out));
    out->schema_version = SC_SCHEMA_VERSION;
    out->tick = env->tick;
    out->player_id = player_id;
    out->map_width = env->width;
    out->map_height = env->height;
    out->sight_radius = radius > 0 ? radius : (env->x_window > env->y_window ? env->x_window : env->y_window);
    out->r = p->r; out->c = p->c;
    out->hp = p->hp; out->hp_max = p->hp_max;
    out->combat_level = p->comb_lvl; out->profession_level = p->prof_lvl;
    out->element = p->element; out->direction = p->dir;
    out->ui_mode = p->ui_mode; out->market_tier = p->market_tier;
    out->sell_index = p->sell_idx; out->gold = p->gold;
    out->in_combat = p->in_combat; out->ranged = p->ranged;
    out->goal = p->goal;
    out->equipment_attack = p->equipment_attack;
    out->equipment_defense = p->equipment_defense;
    out->max_tiers = env->tiers; out->max_levels = env->levels;
    out->enemy_aggro_range = NPC_AGGRO_RANGE;
    out->in_combat_ticks = IN_COMBAT_TICKS;
    out->nearest_enemy_distance = -1;
    out->nearest_resource_distance = -1;
    for (int i = 0; i < 5; i++) out->equipment[i] = p->equipment[i];
    out->has_tool = sc_item_type(p->equipment[SLOT_HELD]) == I_TOOL;
    out->held_tool_tier = out->has_tool ? sc_item_tier(p->equipment[SLOT_HELD]) : 0;
    out->can_attack = p->ui_mode == MODE_PLAY;
    out->can_buy_or_sell = p->in_combat == 0;
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        if (!p->inventory[i]) { out->free_inventory_slots++; continue; }
        SCInventoryEntry* entry = &out->inventory[out->inventory_count++];
        entry->slot = i; entry->id = p->inventory[i];
        entry->type = sc_item_type(entry->id);
        entry->tier = sc_item_tier(entry->id);
        entry->equipped = !!p->is_equipped[i];
    }
    const int dr[4] = {1, -1, 0, 0}, dc[4] = {0, 0, 1, -1};
    for (int i = 0; i < 4; i++)
        out->walkable_adjacent[i] = sc_walkable(env, p->r + dr[i], p->c + dc[i]);

    int r0 = p->r - out->sight_radius, r1 = p->r + out->sight_radius;
    int c0 = p->c - out->sight_radius, c1 = p->c + out->sight_radius;
    if (r0 < 0) r0 = 0;
    if (c0 < 0) c0 = 0;
    if (r1 >= env->height) r1 = env->height - 1;
    if (c1 >= env->width) c1 = env->width - 1;
    for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) {
        int dist = sc_abs(r - p->r) + sc_abs(c - p->c);
        /* Square viewport, consistent with the environment's observation window. */
        int idx = r * env->width + c;
        int tile = (unsigned char)env->terrain[idx];
        if (tile < SC_MAX_TILE_COUNTS) out->terrain_counts[tile]++;
        int id = env->items[idx];
        if (id) {
            out->nearby_item_total++;
            int type = sc_item_type(id), tier = sc_item_tier(id);
            if (out->nearest_resource_distance < 0 || dist < out->nearest_resource_distance)
                out->nearest_resource_distance = dist;
            if (out->nearby_item_count < SC_MAX_NEARBY) {
                SCNearbyItem* v = &out->nearby_items[out->nearby_item_count++];
                v->id = id; v->type = type; v->tier = tier;
                v->r = r; v->c = c; v->distance = dist;
                v->requires_tool = type != I_TOOL;
                v->harvestable = out->free_inventory_slots > 0 &&
                    (type == I_TOOL || (out->has_tool && out->held_tool_tier >= tier));
            }
        }
        if (!env->pids) continue;
        int pid = env->pids[idx];
        if (pid < 0 || pid == player_id || pid >= env->num_agents + env->num_enemies) continue;
        const Entity* entity = pid < env->num_agents ? &env->players[pid] : &env->enemies[pid - env->num_agents];
        if (entity->type == ENTITY_NULL || entity->hp <= 0) continue;
        out->nearby_entity_total++;
        if (entity->type == ENTITY_ENEMY &&
            (out->nearest_enemy_distance < 0 || dist < out->nearest_enemy_distance))
            out->nearest_enemy_distance = dist;
        if (out->nearby_entity_count < SC_MAX_NEARBY) {
            SCNearbyEntity* v = &out->nearby_entities[out->nearby_entity_count++];
            v->entity_id = pid; v->type = entity->type;
            v->r = r; v->c = c; v->distance = dist;
            v->hp = entity->hp; v->hp_max = entity->hp_max;
            v->combat_level = entity->comb_lvl; v->element = entity->element;
            v->ranged = entity->ranged; v->in_combat = entity->in_combat;
        }
    }
    /* Market listings are public/shared state, not an observation of map cells.
     * Only stocked entries are serialized; cap keeps LLM prompts bounded.
     * Market buy UI currently exposes just the first 11 item types. */
    if (env->market) for (int tier = 1; tier <= env->tiers && tier <= MAX_TIERS; tier++)
        for (int type = 1; type <= 11; type++) {
            int id = (tier - 1) * I_N + type;
            const ItemMarket* m = &env->market[id];
            if (!m->stock || out->market_count >= SC_MAX_MARKET) continue;
            SCMarketEntry* v = &out->market[out->market_count++];
            v->item_id = id; v->type = type; v->tier = tier;
            v->stock = m->stock;
            v->asking_price = m->offers[m->stock - 1].price;
            v->affordable = out->gold >= v->asking_price && out->free_inventory_slots > 0;
        }
    return 0;
}

typedef struct { char* ptr; size_t cap, used; int failed; } SCWriter;
static void sc_write(SCWriter* w, const char* fmt, ...) {
    if (w->failed) return;
    if (w->used >= w->cap) { w->failed = 1; return; }
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(w->ptr + w->used, w->cap - w->used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->cap - w->used) { w->failed = 1; return; }
    w->used += (size_t)n;
}

int strategic_context_to_json(const StrategicContext* x, char* dst, size_t capacity) {
    if (!x || !dst || !capacity) return -1;
    SCWriter w = {dst, capacity, 0, 0};
    sc_write(&w, "{\"schema_version\":%d,\"tick\":%d,\"player_id\":%d,", x->schema_version, x->tick, x->player_id);
    sc_write(&w, "\"world\":{\"width\":%d,\"height\":%d,\"sight_radius\":%d,\"max_tiers\":%d,\"max_levels\":%d},",
             x->map_width, x->map_height, x->sight_radius, x->max_tiers, x->max_levels);
    sc_write(&w, "\"player\":{\"r\":%d,\"c\":%d,\"hp\":%d,\"hp_max\":%d,\"combat_level\":%d,\"profession_level\":%d,\"element\":%d,\"direction\":%d,\"ui_mode\":%d,\"market_tier\":%d,\"sell_index\":%d,\"gold\":%d,\"in_combat\":%d,\"ranged\":%d,\"goal\":%d,\"attack_bonus\":%d,\"defense_bonus\":%d,",
             x->r,x->c,x->hp,x->hp_max,x->combat_level,x->profession_level,x->element,x->direction,x->ui_mode,x->market_tier,x->sell_index,x->gold,x->in_combat,x->ranged,x->goal,x->equipment_attack,x->equipment_defense);
    sc_write(&w, "\"equipment\":[%d,%d,%d,%d,%d],\"free_inventory_slots\":%d,\"inventory\":[", x->equipment[0],x->equipment[1],x->equipment[2],x->equipment[3],x->equipment[4],x->free_inventory_slots);
    for (int i = 0; i < x->inventory_count; i++) { const SCInventoryEntry* v = &x->inventory[i]; sc_write(&w, "%s{\"slot\":%d,\"id\":%d,\"type\":%d,\"tier\":%d,\"equipped\":%s}",i?",":"",v->slot,v->id,v->type,v->tier,v->equipped?"true":"false"); }
    sc_write(&w, "]},\"capabilities\":{\"can_attack\":%s,\"has_tool\":%s,\"tool_tier\":%d,\"market_unlocked\":%s,\"aggro_range\":%d,\"combat_cooldown_ticks\":%d},",
             x->can_attack?"true":"false",x->has_tool?"true":"false",x->held_tool_tier,x->can_buy_or_sell?"true":"false",x->enemy_aggro_range,x->in_combat_ticks);
    sc_write(&w, "\"nearby\":{\"visible_entities_total\":%d,\"entities_truncated\":%s,\"entities\":[",x->nearby_entity_total,x->nearby_entity_total>x->nearby_entity_count?"true":"false");
    for (int i=0;i<x->nearby_entity_count;i++) {const SCNearbyEntity* v=&x->nearby_entities[i];sc_write(&w,"%s{\"id\":%d,\"type\":%d,\"r\":%d,\"c\":%d,\"distance\":%d,\"hp\":%d,\"hp_max\":%d,\"combat_level\":%d,\"element\":%d,\"ranged\":%d,\"in_combat\":%d}",i?",":"",v->entity_id,v->type,v->r,v->c,v->distance,v->hp,v->hp_max,v->combat_level,v->element,v->ranged,v->in_combat);}
    sc_write(&w,"],\"visible_items_total\":%d,\"items_truncated\":%s,\"items\":[",x->nearby_item_total,x->nearby_item_total>x->nearby_item_count?"true":"false");
    for (int i=0;i<x->nearby_item_count;i++) {const SCNearbyItem* v=&x->nearby_items[i];sc_write(&w,"%s{\"id\":%d,\"type\":%d,\"tier\":%d,\"r\":%d,\"c\":%d,\"distance\":%d,\"requires_tool\":%s,\"harvestable\":%s}",i?",":"",v->id,v->type,v->tier,v->r,v->c,v->distance,v->requires_tool?"true":"false",v->harvestable?"true":"false");}
    sc_write(&w,"],\"terrain_counts\":[");
    for (int i=0;i<SC_MAX_TILE_COUNTS;i++) sc_write(&w,"%s%d",i?",":"",x->terrain_counts[i]);
    sc_write(&w,"],\"adjacent_passable\":[%s,%s,%s,%s],\"nearest_enemy_distance\":%d,\"nearest_item_distance\":%d},",
        x->walkable_adjacent[0]?"true":"false",x->walkable_adjacent[1]?"true":"false",x->walkable_adjacent[2]?"true":"false",x->walkable_adjacent[3]?"true":"false",x->nearest_enemy_distance,x->nearest_resource_distance);
    sc_write(&w,"\"market\":{\"visible_public_offers\":true,\"entries_truncated\":%s,\"entries\":[",x->market_count==SC_MAX_MARKET?"true":"false");
    for(int i=0;i<x->market_count;i++){const SCMarketEntry* v=&x->market[i];sc_write(&w,"%s{\"id\":%d,\"type\":%d,\"tier\":%d,\"stock\":%d,\"asking_price\":%d,\"affordable\":%s}",i?",":"",v->item_id,v->type,v->tier,v->stock,v->asking_price,v->affordable?"true":"false");}
    sc_write(&w,"]},\"action_codes\":{\"down\":%d,\"up\":%d,\"right\":%d,\"left\":%d,\"noop\":%d,\"attack\":%d,\"buy\":%d,\"sell\":%d,\"run_down\":%d,\"run_up\":%d,\"run_right\":%d,\"run_left\":%d,\"inventory_first\":%d,\"inventory_last\":%d},\"item_type_ids\":{\"helm\":%d,\"chest\":%d,\"legs\":%d,\"sword\":%d,\"bow\":%d,\"tool\":%d,\"earth\":%d,\"fire\":%d,\"air\":%d,\"water\":%d,\"herb\":%d,\"ore\":%d,\"wood\":%d,\"hilt\":%d,\"silver\":%d,\"gold\":%d}}",
        ATN_DOWN,ATN_UP,ATN_RIGHT,ATN_LEFT,ATN_NOOP,ATN_ATTACK,ATN_BUY,ATN_SELL,ATN_DOWN_SHIFT,ATN_UP_SHIFT,ATN_RIGHT_SHIFT,ATN_LEFT_SHIFT,ATN_ONE,ATN_EQUALS,
        I_HELM,I_CHEST,I_LEGS,I_SWORD,I_BOW,I_TOOL,I_EARTH,I_FIRE,I_AIR,I_WATER,I_HERB,I_ORE,I_WOOD,I_HILT,I_SILVER,I_GOLD);
    if (w.failed) {dst[0]='\0';return -1;}
    return (int)w.used;
}

int strategic_context_build_json(const MMO* env,int player_id,int radius,char* dst,size_t capacity){
    StrategicContext x;
    if(strategic_context_collect(env,player_id,radius,&x))return -1;
    return strategic_context_to_json(&x,dst,capacity);
}

const char* strategic_context_planner_contract(void){
    return "You are the strategic planner for a Neural MMO 3 RL agent. "
           "Treat context JSON as a snapshot of local observations plus public market data, not omniscient map knowledge. "
           "Return ONLY valid JSON: {\"objective\":\"survive|train_combat|harvest|equip|trade|explore\","
           "\"reason\":\"short explanation\",\"priority\":0.0,\"horizon_ticks\":25,"
           "\"target\":{\"kind\":\"enemy|item|position|market|none\",\"id\":0,\"r\":0,\"c\":0},"
           "\"subgoals\":[{\"goal\":\"string\",\"success_condition\":\"string\"}],"
           "\"action_preferences\":{\"attack\":0.0,\"harvest\":0.0,\"equip\":0.0,\"buy\":0.0,\"sell\":0.0,\"explore\":0.0,\"retreat\":0.0},"
           "\"replan_if\":[\"low_hp\",\"target_disappears\",\"inventory_full\"]}. "
           "Use only listed target IDs and map coordinates. Action preferences are guidance to RL, not direct actions. "
           "Inventory is 0-based and equipped gear cannot be replaced until unequipped. "
           "For resource harvest, stand on the ground item with free inventory and an equipped adequate-tier tool "
           "(ground tools are an exception). Market trades require non-combat state and multiple UI actions. "
           "Prioritize survival when vulnerable. Do not invent enemies or resources.";
}

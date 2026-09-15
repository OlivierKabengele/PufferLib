#ifndef STRATEGY_CONTEXT_H
#define STRATEGY_CONTEXT_H

#include "nmmo3.h"

#define STRATEGY_CONTEXT_MAX 16384
#define STRATEGY_CONTEXT_MAX_ENTITIES 32
#define STRATEGY_CONTEXT_MAX_RESOURCES 32

typedef struct {
    int id;
    int rel_r;
    int rel_c;
    int distance;
    int comb_lvl;
    int prof_lvl;
    int hp;
    int hp_max;
    int element;
    int in_combat;
    int ranged;
    int equipment_attack;
    int equipment_defense;
    int goal;
} StrategyEntity;

typedef struct {
    int item_id;
    int type;
    int tier;
    int rel_r;
    int rel_c;
    int distance;
} StrategyResource;

typedef struct {
    int pid;
    int self_r;
    int self_c;
    int self_comb_lvl;
    int self_prof_lvl;
    int self_hp;
    int self_hp_max;
    int self_gold;
    int self_in_combat;
    int self_equipment_attack;
    int self_equipment_defense;
    int self_wander_range;
    int self_ranged;
    int self_goal;
    int equipment[5];
    int inventory[INVENTORY_SIZE];
    int is_equipped[INVENTORY_SIZE];
    float reward_death;
    float reward_pioneer;
    float reward_comb_lvl;
    float reward_prof_lvl;
    float reward_item_atk_lvl;
    float reward_item_def_lvl;
    float reward_item_tool_lvl;
    float reward_market_buy;
    float reward_market_sell;
    int num_players;
    int num_enemies;
    StrategyEntity players[STRATEGY_CONTEXT_MAX_ENTITIES];
    StrategyEntity enemies[STRATEGY_CONTEXT_MAX_ENTITIES];
    int num_resources;
    StrategyResource resources[STRATEGY_CONTEXT_MAX_RESOURCES];
    int market_buys;
    int market_sells;
    char text[STRATEGY_CONTEXT_MAX];
} StrategyContext;

int build_strategy_context(MMO* env, int pid, StrategyContext* context);
int build_observation_context(MMO* env, int pid, StrategyContext* context);
int query_llm_action(MMO* env, int pid);
int query_llm_actions(MMO* env, int first_pid, int count);

#endif

#include "nmmo3.h"
#define OBS_SIZE 1707
#define NUM_ATNS 1
#define ACT_SIZES {26}
#define OBS_TENSOR_T ByteTensor

#define Env MMO
#define MY_LOG_EXTRA
#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) {
    env->width = dict_get(kwargs, "width")->value;
    env->height = dict_get(kwargs, "height")->value;
    env->num_agents = dict_get(kwargs, "num_agents")->value;
    env->num_enemies = dict_get(kwargs, "num_enemies")->value;
    env->num_resources = dict_get(kwargs, "num_resources")->value;
    env->num_weapons = dict_get(kwargs, "num_weapons")->value;
    env->num_gems = dict_get(kwargs, "num_gems")->value;
    env->tiers = dict_get(kwargs, "tiers")->value;
    env->levels = dict_get(kwargs, "levels")->value;
    env->teleportitis_prob = dict_get(kwargs, "teleportitis_prob")->value;
    env->enemy_respawn_ticks = dict_get(kwargs, "enemy_respawn_ticks")->value;
    env->item_respawn_ticks = dict_get(kwargs, "item_respawn_ticks")->value;
    env->x_window = dict_get(kwargs, "x_window")->value;
    env->y_window = dict_get(kwargs, "y_window")->value;
    env->reward_combat_level = dict_get(kwargs, "reward_combat_level")->value;
    env->reward_prof_level = dict_get(kwargs, "reward_prof_level")->value;
    env->reward_item_level = dict_get(kwargs, "reward_item_level")->value;
    env->reward_market = dict_get(kwargs, "reward_market")->value;
    env->reward_death = dict_get(kwargs, "reward_death")->value;
    init(env);
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "return_comb_lvl", log->return_comb_lvl);
    dict_set(out, "return_prof_lvl", log->return_prof_lvl);
    dict_set(out, "return_item_atk_lvl", log->return_item_atk_lvl);
    dict_set(out, "return_item_def_lvl", log->return_item_def_lvl);
    dict_set(out, "return_market_buy", log->return_market_buy);
    dict_set(out, "return_market_sell", log->return_market_sell);
    dict_set(out, "return_death", log->return_death);
    dict_set(out, "min_comb_prof", log->min_comb_prof);
    dict_set(out, "purchases", log->purchases);
    dict_set(out, "sales", log->sales);
    dict_set(out, "equip_attack", log->equip_attack);
    dict_set(out, "equip_defense", log->equip_defense);
    dict_set(out, "r", log->r);
    dict_set(out, "c", log->c);
    dict_set(out, "comb_lvl", log->comb_lvl);
    dict_set(out, "prof_lvl", log->prof_lvl);
}

void my_log_extra(StaticVec* vec, Dict* out) {
    static const char* action_keys[NMMO3_ACTION_COUNT] = {
        "action_counts/action_0", "action_counts/action_1",
        "action_counts/action_2", "action_counts/action_3",
        "action_counts/action_4", "action_counts/action_5",
        "action_counts/action_6", "action_counts/action_7",
        "action_counts/action_8", "action_counts/action_9",
        "action_counts/action_10", "action_counts/action_11",
        "action_counts/action_12", "action_counts/action_13",
        "action_counts/action_14", "action_counts/action_15",
        "action_counts/action_16", "action_counts/action_17",
        "action_counts/action_18", "action_counts/action_19",
        "action_counts/action_20", "action_counts/action_21",
        "action_counts/action_22", "action_counts/action_23",
        "action_counts/action_24", "action_counts/action_25",
    };
    MMO* envs = (MMO*)vec->envs;
    for (int action = 0; action < NMMO3_ACTION_COUNT; action++) {
        unsigned long long count = 0;
        for (int i = 0; i < vec->size; i++) {
            count += envs[i].action_counts[action];
        }
        dict_set(out, action_keys[action], (double)count);
    }
}

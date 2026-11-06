#include "nmmo3.h"
#include "strategy_context.c"
#define OBS_SIZE 1771
#define NUM_ATNS 1
#define ACT_SIZES {26}
#define OBS_TENSOR_T ByteTensor
// These are event totals per logging interval, not episode averages.
#define MY_LOG_SUM_FIELDS(X) \
    X(trade_buy_attempts) \
    X(trade_buy_successes) \
    X(trade_buy_failures) \
    X(trade_sell_attempts) \
    X(trade_sell_successes) \
    X(trade_sell_failures)
#define MY_LOG_LLM_TOKENS

#define Env MMO
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
    dict_set(out, "trade_buy_attempts", log->trade_buy_attempts);
    dict_set(out, "trade_buy_successes", log->trade_buy_successes);
    dict_set(out, "trade_buy_failures", log->trade_buy_failures);
    dict_set(out, "trade_sell_attempts", log->trade_sell_attempts);
    dict_set(out, "trade_sell_successes", log->trade_sell_successes);
    dict_set(out, "trade_sell_failures", log->trade_sell_failures);
    dict_set(out, "equip_attack", log->equip_attack);
    dict_set(out, "equip_defense", log->equip_defense);
    dict_set(out, "r", log->r);
    dict_set(out, "c", log->c);
    dict_set(out, "strategy_decisions", log->strategy_decisions);
    dict_set(out, "strategy_failures", log->strategy_failures);
    dict_set(out, "strategy_updates", log->strategy_updates);
    dict_set(out, "llm_input_tokens", log->llm_input_tokens);
    dict_set(out, "llm_output_tokens", log->llm_output_tokens);
    dict_set(out, "llm_cumulative_input_tokens", log->llm_cumulative_input_tokens);
    dict_set(out, "llm_cumulative_output_tokens", log->llm_cumulative_output_tokens);
    dict_set(out, "comb_lvl", log->comb_lvl);
    dict_set(out, "prof_lvl", log->prof_lvl);
}

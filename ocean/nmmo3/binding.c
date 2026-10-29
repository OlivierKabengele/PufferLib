/* Rename originals so vecenv.h calls the strategy-aware wrappers below. */
#define c_step nmmo3_base_c_step
#define c_reset nmmo3_base_c_reset
#define c_close nmmo3_base_c_close
#include "nmmo3.h"
#undef c_step
#undef c_reset
#undef c_close
#include "strategic_context.c"
#include "strategic_executor.c"
/* Only the first env in each worker uses remote planning by default.
 * Other envs preserve original behavior, avoiding thousands of network calls.
 * This file is compiled into the native PufferLib environment backend.
 */
static MMO *nmmo_strategy_env = NULL;
static SEStrategicRuntime nmmo_strategy_runtime = {0};
static int nmmo_strategy_initialized = 0;

static void nmmo_strategy_release(MMO *env) {
    if (env == nmmo_strategy_env) {
        se_runtime_stop(&nmmo_strategy_runtime);
        nmmo_strategy_env = NULL;
        nmmo_strategy_initialized = 0;
    }
}

void c_reset(MMO *env) {
    if (env == nmmo_strategy_env) {
        se_runtime_episode_reset(&nmmo_strategy_runtime);
    }
    nmmo3_base_c_reset(env);
    const char *provider = getenv("NMMO_STRATEGIC_PROVIDER");
    if (!provider || !*provider || nmmo_strategy_env != NULL) return;
    const char *inf = getenv("NMMO_STRATEGIC_INFLUENCE");
    int influence = inf ? atoi(inf) : 20;
    if (se_runtime_start(&nmmo_strategy_runtime, 1, provider, influence) == 0) {
        nmmo_strategy_env = env;
        nmmo_strategy_initialized = 1;
        fprintf(stderr, "[strategy] PufferLib training enabled, provider=%s, influence=%d%%, min_interval=%d ticks\n",
                provider, influence, SE_MIN_LLM_TICKS);
    } else {
        fprintf(stderr, "[strategy] WARNING: runtime initialization failed; original RL fallback\n");
    }
}

void c_step(MMO *env) {
    if (nmmo_strategy_initialized && env == nmmo_strategy_env) {
        /* Executed inside native PufferLib rollouts, after policy actions were produced. */
        se_runtime_before_action(&nmmo_strategy_runtime, env);
        se_runtime_guide_actions(&nmmo_strategy_runtime, env);
    }
    nmmo3_base_c_step(env);
}

void c_close(MMO *env) {
    nmmo_strategy_release(env);
    nmmo3_base_c_close(env);
}

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
    if (nmmo_strategy_initialized) {
        dict_set(out, "Input_tokens", (double)nmmo_strategy_runtime.input_tokens);
        dict_set(out, "Output_tokens", (double)nmmo_strategy_runtime.output_tokens);
        dict_set(out, "LLM_last_input_tokens", (double)nmmo_strategy_runtime.last_input_tokens);
        dict_set(out, "LLM_last_output_tokens", (double)nmmo_strategy_runtime.last_output_tokens);
        dict_set(out, "strategy_decisions", (double)nmmo_strategy_runtime.strategy_decisions);
        dict_set(out, "strategy_updates", (double)nmmo_strategy_runtime.strategy_updates);
        dict_set(out, "strategy_failure", (double)nmmo_strategy_runtime.strategy_failure);
    } else {
        dict_set(out, "Input_tokens", 0.0);
        dict_set(out, "Output_tokens", 0.0);
        dict_set(out, "LLM_last_input_tokens", 0.0);
        dict_set(out, "LLM_last_output_tokens", 0.0);
        dict_set(out, "strategy_decisions", 0.0);
        dict_set(out, "strategy_updates", 0.0);
        dict_set(out, "strategy_failure", 0.0);
    }
    MMO* envs = (MMO*)vec->envs;
    for (int action = 0; action < NMMO3_ACTION_COUNT; action++) {
        unsigned long long count = 0;
        for (int i = 0; i < vec->size; i++) {
            count += envs[i].action_counts[action];
        }
        dict_set(out, action_keys[action], (double)count);
    }
}

/* Optional explicit host callbacks for PufferLib. These do not change its
 * observations, action space, or training loop. A Python/native host can
 * call these through an extension binding when exposed in its build system.
 * Never run blocking LLM requests in my_log or each vector environment tick.
 */
int nmmo_strategic_snapshot(MMO* env, int agent_id, char* json, size_t capacity) {
    return strategic_context_build_json(env, agent_id, 0, json, capacity);
}
void nmmo_strategic_executor_init(SEStrategicExecutor* executor, int agent_id) {
    se_init(executor, agent_id);
}
int nmmo_strategic_executor_tick(MMO* env, int agent_id,
                                 SEStrategicExecutor* executor,
                                 SEStrategicGuidance* guidance) {
    StrategicContext ctx;
    if (!executor || !guidance || strategic_context_collect(env,agent_id,0,&ctx)) return -1;
    se_update(executor,&ctx);
    se_guidance(executor,&ctx,guidance);
    return se_can_request(executor,ctx.tick);
}
int nmmo_strategic_executor_install(MMO* env, int agent_id,
                                    SEStrategicExecutor* executor,
                                    const SEStrategicPlan* plan) {
    StrategicContext ctx;
    if (!executor || !plan || strategic_context_collect(env,agent_id,0,&ctx)) return -1;
    return se_install(executor,plan,&ctx);
}

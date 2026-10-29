#ifndef NMMO_STRATEGIC_EXECUTOR_H
#define NMMO_STRATEGIC_EXECUTOR_H

#include "strategic_context.h"
#include <stdint.h>

/* A single game tick clock is used. At most ONE initial LLM request per agent
 * can be issued at tick >= 0; subsequent requests are separated by >=500 ticks.
 * This is a per-agent cooldown (not global across agents).
 */
#define SE_MIN_LLM_TICKS 500
#define SE_PREF_COUNT 7
#define SE_MAX_SUBGOALS 5

typedef enum {
    SE_SURVIVE, SE_TRAIN_COMBAT, SE_HARVEST,
    SE_EQUIP, SE_TRADE, SE_EXPLORE
} SEObjective;
typedef enum {
    SE_TARGET_NONE, SE_TARGET_ENEMY, SE_TARGET_ITEM,
    SE_TARGET_POSITION, SE_TARGET_MARKET
} SETargetKind;
typedef enum {
    SE_EMPTY, SE_ACTIVE, SE_PENDING_REPLAN, SE_COMPLETE, SE_EXPIRED
} SEStatus;

typedef struct {
    SEObjective objective;
    SETargetKind kind;
    int target_id, target_r, target_c;
    int horizon_ticks;        /* 1..200: supplied by validated LLM JSON */
    float priority;           /* 0..1 */
    float preferences[SE_PREF_COUNT]; /* attack,harvest,equip,buy,sell,explore,retreat */
    int replan_on_low_hp, replan_on_target_lost, replan_on_inventory_full;
    int subgoal_count;
} SEStrategicPlan;

typedef struct {
    int player_id;
    int initialized;
    int last_llm_tick;        /* -1 means no LLM request has yet occurred */
    int installed_tick;
    int current_subgoal;
    int request_pending;
    int baseline_hp;
    int baseline_inventory_count;
    int baseline_combat_level;
    int baseline_profession_level;
    SEStatus status;
    SEStrategicPlan plan;
} SEStrategicExecutor;

typedef struct {
    int objective;            /* same SEObjective encoding */
    int target_kind;
    int target_id, relative_r, relative_c;
    int remaining_plan_ticks;
    int current_subgoal;
    int pending_replan;
    int fallback;             /* no active reliable plan */
    float priority;
    float preferences[SE_PREF_COUNT];
} SEStrategicGuidance;

void se_init(SEStrategicExecutor* e, int player_id);
/* Returns 1 if the caller is allowed to issue a request right now.
 * Call se_mark_llm_requested IMMEDIATELY BEFORE network invocation;
 * this consumes cooldown even if the call fails.
 */
int se_can_request(const SEStrategicExecutor* e, int tick);
int se_mark_llm_requested(SEStrategicExecutor* e, int tick);
int se_ticks_until_request(const SEStrategicExecutor* e, int tick);
/* Install a previously JSON-validated decision. Returns -1 for invalid input.
 * The tick must be at or after last_llm_tick; no change to the request clock.
 */
int se_install(SEStrategicExecutor* e, const SEStrategicPlan* plan,
               const StrategicContext* ctx);
/* Updates execution state. No network calls. */
void se_update(SEStrategicExecutor* e, const StrategicContext* ctx);
/* This is heuristic guidance, NOT a legal action mask or policy logits. */
void se_guidance(const SEStrategicExecutor* e, const StrategicContext* ctx,
                 SEStrategicGuidance* out);

/* Integrated demo runtime: optional, disabled unless a provider is specified. */
#define SE_CONTEXT_CAPACITY 65536
typedef struct {
    SEStrategicExecutor *agents;
    int count;
    int enabled;
    int influence_percent;  /* 0 = baseline RL, 0..100 target movement override */
    int epoch_offset;        /* monotonically accumulated ticks across c_reset() */
    int last_environment_tick;
    char working_dir[256];
    char provider[16];
    uint64_t input_tokens;
    uint64_t output_tokens;
    uint64_t last_input_tokens;
    uint64_t last_output_tokens;
    uint64_t strategy_decisions;
    uint64_t strategy_updates;
    uint64_t strategy_failure;
} SEStrategicRuntime;

int se_runtime_start(SEStrategicRuntime *rt, int num_agents, const char *provider,
             int influence_percent);
void se_runtime_stop(SEStrategicRuntime *rt);
void se_runtime_episode_reset(SEStrategicRuntime *rt);
/* Run once before RL forward(), not once per rendered frame. */
void se_runtime_before_action(SEStrategicRuntime *rt, MMO *env);
/* Run after policy inference, before c_step(). */
void se_runtime_guide_actions(SEStrategicRuntime *rt, MMO *env);

#endif

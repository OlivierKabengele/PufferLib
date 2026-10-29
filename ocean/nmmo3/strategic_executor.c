#include "strategic_executor.h"
#include <string.h>
#include <limits.h>
#include <math.h>

static int se_has_target(const SEStrategicPlan* p, const StrategicContext* c) {
    int i;
    if (p->kind == SE_TARGET_NONE || p->kind == SE_TARGET_POSITION) return 1;
    if (p->kind == SE_TARGET_ENEMY) {
        for (i=0; i<c->nearby_entity_count; ++i) {
            const SCNearbyEntity* x = &c->nearby_entities[i];
            if (x->entity_id == p->target_id && x->type == 2 &&
                x->r == p->target_r && x->c == p->target_c) return 1;
        }
    } else if (p->kind == SE_TARGET_ITEM) {
        for (i=0; i<c->nearby_item_count; ++i) {
            const SCNearbyItem* x = &c->nearby_items[i];
            if (x->id == p->target_id && x->r == p->target_r && x->c == p->target_c) return 1;
        }
    } else if (p->kind == SE_TARGET_MARKET) {
        for (i=0; i<c->market_count; ++i)
            if (c->market[i].item_id == p->target_id) return 1;
    }
    return 0;
}

void se_init(SEStrategicExecutor* e, int player_id) {
    if (!e) return;
    memset(e, 0, sizeof(*e));
    e->player_id = player_id;
    e->initialized = 1;
    e->last_llm_tick = -1;
    e->installed_tick = -1;
    e->request_pending = 1;
    e->status = SE_EMPTY;
}

int se_ticks_until_request(const SEStrategicExecutor* e, int tick) {
    if (!e || tick < 0) return INT_MAX;
    if (e->last_llm_tick < 0) return 0;
    if (tick < e->last_llm_tick) return INT_MAX; /* clock reset: reinitialize */
    const int64_t elapsed = (int64_t)tick - e->last_llm_tick;
    return elapsed >= SE_MIN_LLM_TICKS ? 0 : (int)(SE_MIN_LLM_TICKS-elapsed);
}

int se_can_request(const SEStrategicExecutor* e, int tick) {
    return e && e->request_pending && se_ticks_until_request(e,tick)==0;
}

int se_mark_llm_requested(SEStrategicExecutor* e, int tick) {
    if (!se_can_request(e,tick)) return -1;
    e->last_llm_tick = tick;
    e->request_pending = 0;
    return 0;
}

static int se_valid_plan(const SEStrategicPlan* p, const StrategicContext* c) {
    if (!p || !c || p->objective < SE_SURVIVE || p->objective > SE_EXPLORE ||
        p->kind < SE_TARGET_NONE || p->kind > SE_TARGET_MARKET ||
        p->horizon_ticks < 1 || p->horizon_ticks > 200 ||
        !isfinite(p->priority) || p->priority < 0 || p->priority > 1 ||
        p->subgoal_count < 1 || p->subgoal_count > SE_MAX_SUBGOALS) return 0;
    for (int i=0;i<SE_PREF_COUNT;i++)
        if (!isfinite(p->preferences[i]) || p->preferences[i]<0 || p->preferences[i]>1) return 0;
    if (p->kind == SE_TARGET_POSITION && (p->target_id != 0 ||
        p->target_r < 0 || p->target_r >= c->map_height ||
        p->target_c < 0 || p->target_c >= c->map_width)) return 0;
    if (p->kind == SE_TARGET_NONE &&
        (p->target_id || p->target_r || p->target_c)) return 0;
    if (p->kind == SE_TARGET_MARKET && (p->target_r || p->target_c)) return 0;
    return se_has_target(p,c);
}

int se_install(SEStrategicExecutor* e, const SEStrategicPlan* p,
               const StrategicContext* c) {
    if (!e || !c || e->player_id != c->player_id || !se_valid_plan(p,c) ||
        e->last_llm_tick < 0 || c->tick < e->last_llm_tick) return -1;
    e->plan = *p;
    e->installed_tick = c->tick;
    e->current_subgoal = 0;
    e->request_pending = 0;
    e->baseline_hp = c->hp;
    e->baseline_inventory_count = c->inventory_count;
    e->baseline_combat_level = c->combat_level;
    e->baseline_profession_level = c->profession_level;
    e->status = SE_ACTIVE;
    return 0;
}

void se_update(SEStrategicExecutor* e, const StrategicContext* c) {
    if (!e || !c || !e->initialized || c->player_id != e->player_id) return;
    if (e->status != SE_ACTIVE) return;
    if (c->tick < e->installed_tick) { e->status=SE_EXPIRED; e->request_pending=1; return; }
    int complete = 0;
    switch (e->plan.objective) {
        case SE_HARVEST: complete = c->inventory_count > e->baseline_inventory_count ||
                                    c->profession_level > e->baseline_profession_level; break;
        case SE_TRAIN_COMBAT: complete = c->combat_level > e->baseline_combat_level; break;
        case SE_EXPLORE: complete = (e->plan.kind==SE_TARGET_POSITION &&
                                    c->r==e->plan.target_r && c->c==e->plan.target_c); break;
        case SE_SURVIVE: complete = (c->hp > e->baseline_hp && c->in_combat==0); break;
        default: break; /* trade/equip require external verification, not guesses */
    }
    if (complete) { e->status=SE_COMPLETE; e->request_pending=1; return; }
    if ((int64_t)c->tick - e->installed_tick >= e->plan.horizon_ticks) {
        e->status=SE_EXPIRED; e->request_pending=1; return;
    }
    if ((e->plan.replan_on_low_hp && c->hp_max>0 && c->hp*10 <= c->hp_max*3) ||
        (e->plan.replan_on_inventory_full && c->free_inventory_slots==0) ||
        (e->plan.replan_on_target_lost && !se_has_target(&e->plan,c))) {
        e->status=SE_PENDING_REPLAN; e->request_pending=1;
    }
}

void se_guidance(const SEStrategicExecutor* e, const StrategicContext* c,
                 SEStrategicGuidance* out) {
    if (!out) return;
    memset(out,0,sizeof(*out));
    out->fallback=1;
    out->objective=SE_SURVIVE;
    out->target_kind=SE_TARGET_NONE;
    out->preferences[6]=0.5f; /* conservative retreat preference */
    if (!e || !c || e->player_id!=c->player_id) return;
    out->pending_replan=e->request_pending;
    if (e->status != SE_ACTIVE) return;
    const SEStrategicPlan* p=&e->plan;
    out->fallback=0;
    out->objective=p->objective;
    out->target_kind=p->kind;
    out->target_id=p->target_id;
    out->relative_r=p->target_r-c->r;
    out->relative_c=p->target_c-c->c;
    out->current_subgoal=e->current_subgoal;
    out->priority=p->priority;
    int remaining=p->horizon_ticks-(c->tick-e->installed_tick);
    out->remaining_plan_ticks=remaining>0 ? remaining : 0;
    memcpy(out->preferences,p->preferences,sizeof(out->preferences));
}

/* Demo runtime integrated here; no separate strategic_runtime.c required. */
/* Compile as part of a translation unit that already includes nmmo3.h,
 * strategic_context.c before this file, in that order.
 * Opt-in demo integration; each agent maintains a 500-tick request cooldown.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

int se_runtime_start(SEStrategicRuntime *rt, int count, const char *provider, int influence) {
    if (!rt || count <= 0) return -1;
    memset(rt, 0, sizeof(*rt));
    if (!provider || (strcmp(provider,"gpt") && strcmp(provider,"gemini") &&
        strcmp(provider,"ollama"))) return -1;
    if (influence < 0 || influence > 100) return -1;
    rt->agents = calloc((size_t)count, sizeof(SEStrategicExecutor));
    if (!rt->agents) return -1;
    rt->count = count;
    rt->last_environment_tick = -1;
    rt->influence_percent = influence;
    snprintf(rt->provider, sizeof(rt->provider), "%s", provider);
    /* Isolate state to this run so old bridge cooldowns do not leak in. */
    for (int attempt=0; attempt<100; ++attempt) {
        unsigned token = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)rt ^ (unsigned)attempt * 2654435761u;
        snprintf(rt->working_dir, sizeof(rt->working_dir), "/tmp/nmmo_strategy_%08x", token);
        if (mkdir(rt->working_dir, 0700) == 0) break;
        if (errno != EEXIST || attempt == 99) { free(rt->agents); rt->agents=NULL; return -1; }
    }
    for (int i=0; i<count; ++i) se_init(&rt->agents[i], i);
    rt->enabled = 1;
    return 0;
}

void se_runtime_episode_reset(SEStrategicRuntime *rt) {
    if (!rt || !rt->enabled) return;
    if (rt->last_environment_tick >= 0) {
        rt->epoch_offset += rt->last_environment_tick + 1;
    }
    rt->last_environment_tick = -1;
    for (int i = 0; i < rt->count; ++i) {
        int last_request = rt->agents[i].last_llm_tick;
        se_init(&rt->agents[i], i);
        rt->agents[i].last_llm_tick = last_request;
    }
}

void se_runtime_stop(SEStrategicRuntime *rt) {
    if (!rt) return;
    if (rt->working_dir[0]) {
        char path[512];
        for (int i=0;i<rt->count;i++) {
            snprintf(path,sizeof(path),"%s/context_%d.json",rt->working_dir,i); remove(path);
            snprintf(path,sizeof(path),"%s/plan_%d.txt",rt->working_dir,i); remove(path);
            snprintf(path,sizeof(path),"%s/usage_%d.txt",rt->working_dir,i); remove(path);
        }
        snprintf(path,sizeof(path),"%s/bridge_state.json",rt->working_dir); remove(path);
        snprintf(path,sizeof(path),"%s/bridge_state.json.lock",rt->working_dir); remove(path);
        rmdir(rt->working_dir);
    }
    free(rt->agents);
    memset(rt, 0, sizeof(*rt));
}

static int se_runtime_read_plan(const char *path, SEStrategicPlan *p) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char status[32] = {0};
    if (fscanf(f,"%31s",status) != 1 || strcmp(status,"planned")) { fclose(f); return -1; }
    int n = fscanf(f,"%d %d %d %d %d %d %f %f %f %f %f %f %f %f %d %d %d %d",
        (int*)&p->objective, (int*)&p->kind, &p->target_id, &p->target_r, &p->target_c,
        &p->horizon_ticks, &p->priority,
        &p->preferences[0],&p->preferences[1],&p->preferences[2],
        &p->preferences[3],&p->preferences[4],&p->preferences[5],&p->preferences[6],
        &p->replan_on_low_hp,&p->replan_on_target_lost,
        &p->replan_on_inventory_full,&p->subgoal_count);
    fclose(f);
    return n == 18 ? 0 : -1;
}

void se_runtime_before_action(SEStrategicRuntime *rt, MMO *env) {
    if (!rt || !rt->enabled || !env) return;
    rt->last_environment_tick = env->tick;
    int n = rt->count < env->num_agents ? rt->count : env->num_agents;
    for (int i=0; i<n; ++i) {
        SEStrategicExecutor *e=&rt->agents[i];
        StrategicContext ctx;
        if (strategic_context_collect(env,i,0,&ctx)) continue;
        ctx.tick += rt->epoch_offset;
        se_update(e,&ctx);
        if (!se_can_request(e,ctx.tick)) continue;
        /* Request consumes cooldown BEFORE network access or filesystem I/O. */
        if (se_mark_llm_requested(e,ctx.tick)) continue;
        char context_path[512], plan_path[512], state_path[512], usage_path[512], cmd[2200];
        snprintf(context_path,sizeof(context_path),"%s/context_%d.json",rt->working_dir,i);
        snprintf(plan_path,sizeof(plan_path),"%s/plan_%d.txt",rt->working_dir,i);
        snprintf(state_path,sizeof(state_path),"%s/bridge_state.json",rt->working_dir);
        snprintf(usage_path,sizeof(usage_path),"%s/usage_%d.txt",rt->working_dir,i);
        remove(usage_path);
        char *json=malloc(SE_CONTEXT_CAPACITY);
        if (!json) { e->request_pending=1; continue; }
        int len=strategic_context_to_json(&ctx,json,SE_CONTEXT_CAPACITY);
        FILE *f=fopen(context_path,"w");
        if (len<0 || !f) { if(f) fclose(f); free(json); e->request_pending=1; continue; }
        int wrote=(int)fwrite(json,1,(size_t)len,f);
        fclose(f); free(json);
        if (wrote!=len) { e->request_pending=1; continue; }
        /* Only generated /tmp paths and fixed provider names appear in command. */
        const char *bridge=getenv("NMMO_STRATEGIC_BRIDGE");
        const char *python=getenv("NMMO_STRATEGIC_PYTHON");
        if (!bridge || !*bridge) bridge="strategic_bridge.py";
        if (!python || !*python) python="python3";
        /* These paths are set by the local training CLI, not model output. */
        snprintf(cmd,sizeof(cmd),"'%s' '%s' '%s' --provider %s --state-file '%s' --compact-output '%s' --usage-output '%s'",
                 python,bridge,context_path,rt->provider,state_path,plan_path,usage_path);
        rt->strategy_updates++;
        int rc=system(cmd);
        FILE *uf = fopen(usage_path, "r");
        if (uf) {
            unsigned long long in = 0, out = 0;
            if (fscanf(uf, "%llu %llu", &in, &out) == 2) {
                rt->input_tokens += (uint64_t)in;
                rt->output_tokens += (uint64_t)out;
                rt->last_input_tokens = (uint64_t)in;
                rt->last_output_tokens = (uint64_t)out;
            }
            fclose(uf);
            remove(usage_path);
        }
        SEStrategicPlan p={0};
        if (rc==0 && se_runtime_read_plan(plan_path,&p)==0 && se_install(e,&p,&ctx)==0) {
            rt->strategy_decisions++;
            fprintf(stderr,"[strategy] tick=%d player=%d installed objective=%d target=%d\n",
                    env->tick,i,(int)p.objective,p.target_id);
        } else {
            rt->strategy_failure++;
            e->request_pending=1;
            fprintf(stderr,"[strategy] tick=%d player=%d planner unavailable/invalid; RL fallback\n",env->tick,i);
        }
    }
}

void se_runtime_guide_actions(SEStrategicRuntime *rt, MMO *env) {
    if (!rt || !rt->enabled || !env || rt->influence_percent <= 0) return;
    int n=rt->count < env->num_agents ? rt->count : env->num_agents;
    for(int i=0;i<n;++i) {
        SEStrategicExecutor *e=&rt->agents[i];
        if(e->status!=SE_ACTIVE || (e->plan.kind!=SE_TARGET_ITEM &&
           e->plan.kind!=SE_TARGET_ENEMY && e->plan.kind!=SE_TARGET_POSITION)) continue;
        /* Use the existing policy by default; only occasional navigation steering. */
        if ((int)(rand_r(&env->rng)%100) >= rt->influence_percent) continue;
        Entity *player=&env->players[i];
        if(player->ui_mode!=MODE_PLAY || player->hp<=0) continue;
        int dr=e->plan.target_r-player->r, dc=e->plan.target_c-player->c;
        int action=ATN_NOOP;
        if(abs(dr)>=abs(dc) && dr) action=dr>0 ? ATN_DOWN : ATN_UP;
        else if(dc) action=dc>0 ? ATN_RIGHT : ATN_LEFT;
        else if(dr) action=dr>0 ? ATN_DOWN : ATN_UP;
        if(action==ATN_NOOP) continue;
        int r=player->r+DELTAS[action][0], c=player->c+DELTAS[action][1];
        if(r<0 || c<0 || r>=env->height || c>=env->width) continue;
        int idx=r*env->width+c;
        if((unsigned char)env->terrain[idx]>=8 || env->pids[idx]!=-1) continue;
        env->actions[i]=(float)action;
    }
}

#include "strategy_context.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

static int abs_int(int value) {
    return value < 0 ? -value : value;
}

static MMO* strategy_active_env = NULL;

static int distance_between(int r1, int c1, int r2, int c2) {
    return abs_int(r1 - r2) + abs_int(c1 - c2);
}

static int is_local(MMO* env, Entity* self, Entity* other) {
    return abs_int(self->r - other->r) <= env->y_window &&
           abs_int(self->c - other->c) <= env->x_window;
}

static void fill_entity(StrategyEntity* dst, Entity* src, Entity* self, int id) {
    dst->id = id;
    dst->rel_r = src->r - self->r;
    dst->rel_c = src->c - self->c;
    dst->distance = distance_between(self->r, self->c, src->r, src->c);
    dst->comb_lvl = src->comb_lvl;
    dst->prof_lvl = src->prof_lvl;
    dst->hp = src->hp;
    dst->hp_max = src->hp_max;
    dst->element = src->element;
    dst->in_combat = src->in_combat;
    dst->ranged = src->ranged;
    dst->equipment_attack = src->equipment_attack;
    dst->equipment_defense = src->equipment_defense;
    dst->goal = src->goal;
}

static int append_text(char* text, int offset, const char* format, ...) {
    va_list args;
    int written;
    if (offset >= STRATEGY_CONTEXT_MAX - 1) return STRATEGY_CONTEXT_MAX;
    va_start(args, format);
    written = vsnprintf(text + offset, STRATEGY_CONTEXT_MAX - offset, format, args);
    va_end(args);
    if (written < 0) return -1;
    if (written >= STRATEGY_CONTEXT_MAX - offset) return STRATEGY_CONTEXT_MAX;
    return offset + written;
}

int build_strategy_context(MMO* env, int pid, StrategyContext* context) {
    int offset = 0;
    int group_alive = 0, group_dead = 0, group_r = 0, group_c = 0;
    int group_comb = 0, group_prof = 0, group_hp = 0, group_hp_max = 0, group_gold = 0;
    Entity* self;
    Reward* reward;

    if (env == NULL || context == NULL || pid < 0 || pid >= env->num_agents ||
        env->players == NULL || env->items == NULL || env->reward_struct == NULL) return -1;
    memset(context, 0, sizeof(*context));
    context->pid = pid;
    self = &env->players[pid];
    reward = &env->reward_struct[pid];
    context->self_r = self->r; context->self_c = self->c;
    context->self_comb_lvl = self->comb_lvl; context->self_prof_lvl = self->prof_lvl;
    context->self_hp = self->hp; context->self_hp_max = self->hp_max;
    context->self_gold = self->gold; context->self_in_combat = self->in_combat;
    context->self_equipment_attack = self->equipment_attack;
    context->self_equipment_defense = self->equipment_defense;
    context->self_wander_range = self->wander_range; context->self_ranged = self->ranged;
    context->self_goal = self->goal;
    memcpy(context->equipment, self->equipment, sizeof(context->equipment));
    memcpy(context->inventory, self->inventory, sizeof(context->inventory));
    memcpy(context->is_equipped, self->is_equipped, sizeof(context->is_equipped));
    context->reward_death = reward->death; context->reward_pioneer = reward->pioneer;
    context->reward_comb_lvl = reward->comb_lvl; context->reward_prof_lvl = reward->prof_lvl;
    context->reward_item_atk_lvl = reward->item_atk_lvl;
    context->reward_item_def_lvl = reward->item_def_lvl;
    context->reward_item_tool_lvl = reward->item_tool_lvl;
    context->reward_market_buy = reward->market_buy; context->reward_market_sell = reward->market_sell;
    for (int i = 0; i < env->num_agents && context->num_players < STRATEGY_CONTEXT_MAX_ENTITIES; i++)
        if (i != pid && is_local(env, self, &env->players[i]))
            fill_entity(&context->players[context->num_players++], &env->players[i], self, i);
    for (int i = 0; i < env->num_enemies && context->num_enemies < STRATEGY_CONTEXT_MAX_ENTITIES; i++)
        if (is_local(env, self, &env->enemies[i]))
            fill_entity(&context->enemies[context->num_enemies++], &env->enemies[i], self, i);
    for (int r = self->r - env->y_window; r <= self->r + env->y_window; r++) {
        if (r < 0 || r >= env->height) continue;
        for (int c = self->c - env->x_window; c <= self->c + env->x_window; c++) {
            int item_id;
            StrategyResource* resource;
            if (c < 0 || c >= env->width) continue;
            item_id = env->items[r * env->width + c];
            if (item_id == I_NULL) continue;
            if (context->num_resources >= STRATEGY_CONTEXT_MAX_RESOURCES) break;
            resource = &context->resources[context->num_resources++];
            resource->item_id = item_id;
            if (item_id >= 0 && item_id < (MAX_TIERS + 1) * I_N + 1) {
                resource->type = ITEMS[item_id].type; resource->tier = ITEMS[item_id].tier;
            }
            resource->rel_r = r - self->r; resource->rel_c = c - self->c;
            resource->distance = distance_between(self->r, self->c, r, c);
        }
        if (context->num_resources >= STRATEGY_CONTEXT_MAX_RESOURCES) break;
    }

    context->market_buys = env->market_buys;
    context->market_sells = env->market_sells;

    for (int i = 0; i < env->num_agents; i++) {
        Entity* player = &env->players[i];
        group_r += player->r;
        group_c += player->c;
        group_comb += player->comb_lvl;
        group_prof += player->prof_lvl;
        group_hp += player->hp;
        group_hp_max += player->hp_max;
        group_gold += player->gold;
        if (player->hp > 0) {
            group_alive++;
        } else {
            group_dead++;
        }
    }
    if (env->num_agents > 0) {
        group_r /= env->num_agents;
        group_c /= env->num_agents;
        group_comb /= env->num_agents;
        group_prof /= env->num_agents;
        group_hp /= env->num_agents;
        group_hp_max /= env->num_agents;
        group_gold /= env->num_agents;
    }

    offset = append_text(context->text, offset,
        "NMMO_STRATEGY_CONTEXT\nstrategy_scope=GROUP\ncontroller_agent=%d\ntick=%d\n\n"
        "GROUP_STATE\nagent_count=%d\nalive_agents=%d\ndead_agents=%d\n"
        "average_position=%d,%d\naverage_combat_level=%d\n"
        "average_profession_level=%d\naverage_hp=%d/%d\naverage_gold=%d\n\n"
        "SELF\n"
        "position=%d,%d\ncombat_level=%d\nprofession_level=%d\nhp=%d/%d\n"
        "gold=%d\nin_combat=%d\nequipment_attack=%d\nequipment_defense=%d\n"
        "wander_range=%d\nranged=%d\ngoal=%d\n\n",
        pid, env->tick, env->num_agents, group_alive, group_dead,
        group_r, group_c, group_comb, group_prof, group_hp, group_hp_max, group_gold,
        context->self_r, context->self_c, context->self_comb_lvl,
        context->self_prof_lvl, context->self_hp, context->self_hp_max, context->self_gold,
        context->self_in_combat, context->self_equipment_attack,
        context->self_equipment_defense, context->self_wander_range,
        context->self_ranged, context->self_goal);
    offset = append_text(context->text, offset, "EQUIPMENT slots=");
    for (int i = 0; i < 5; i++) offset = append_text(context->text, offset, "%d%s", context->equipment[i], i == 4 ? "\n" : ",");
    offset = append_text(context->text, offset, "INVENTORY items=");
    for (int i = 0; i < INVENTORY_SIZE; i++) offset = append_text(context->text, offset, "%d%s", context->inventory[i], i == INVENTORY_SIZE - 1 ? "\n" : ",");
    offset = append_text(context->text, offset,
        "\nPROGRESSION\nreward_death=%.2f\nreward_pioneer=%.2f\nreward_combat=%.2f\n"
        "reward_profession=%.2f\nreward_item_attack=%.2f\nreward_item_defense=%.2f\n"
        "reward_item_tool=%.2f\nreward_market_buy=%.2f\nreward_market_sell=%.2f\n",
        context->reward_death, context->reward_pioneer, context->reward_comb_lvl,
        context->reward_prof_lvl, context->reward_item_atk_lvl, context->reward_item_def_lvl,
        context->reward_item_tool_lvl, context->reward_market_buy, context->reward_market_sell);

    offset = append_text(context->text, offset, "\nNEARBY_PLAYERS count=%d\n", context->num_players);
    for (int i = 0; i < context->num_players; i++) {
        StrategyEntity* e = &context->players[i];
        offset = append_text(context->text, offset, "player id=%d rel=(%d,%d) dist=%d combat=%d prof=%d hp=%d/%d element=%d combat_state=%d ranged=%d atk=%d def=%d goal=%d\n", e->id, e->rel_r, e->rel_c, e->distance, e->comb_lvl, e->prof_lvl, e->hp, e->hp_max, e->element, e->in_combat, e->ranged, e->equipment_attack, e->equipment_defense, e->goal);
    }
    offset = append_text(context->text, offset, "\nNEARBY_ENEMIES count=%d\n", context->num_enemies);
    for (int i = 0; i < context->num_enemies; i++) {
        StrategyEntity* e = &context->enemies[i];
        offset = append_text(context->text, offset, "enemy id=%d rel=(%d,%d) dist=%d combat=%d hp=%d/%d element=%d combat_state=%d ranged=%d atk=%d def=%d goal=%d\n", e->id, e->rel_r, e->rel_c, e->distance, e->comb_lvl, e->hp, e->hp_max, e->element, e->in_combat, e->ranged, e->equipment_attack, e->equipment_defense, e->goal);
    }
    offset = append_text(context->text, offset, "\nNEARBY_ITEMS count=%d\n", context->num_resources);
    for (int i = 0; i < context->num_resources; i++) {
        StrategyResource* r = &context->resources[i];
        offset = append_text(context->text, offset, "item id=%d type=%d tier=%d rel=(%d,%d) dist=%d\n", r->item_id, r->type, r->tier, r->rel_r, r->rel_c, r->distance);
    }
    offset = append_text(context->text, offset, "\nRECENT_STRATEGIES count=%d\n", env->strategy_history_count);
    for (int i = 0; i < env->strategy_history_count; i++) {
        int history_idx = (env->strategy_history_next - env->strategy_history_count + i + STRATEGY_HISTORY_SIZE) % STRATEGY_HISTORY_SIZE;
        offset = append_text(context->text, offset, "%s\n", env->strategy_history[history_idx]);
    }
    // Read-only trading affordances for the high-level planner.
    // Market purchase consumes the most recently listed offer for each item.
    int best_weapon = 0, best_tool = 0, sellable = 0, free_slots = 0;
    int affordable_weapon = 0, affordable_tool = 0, upgrades_unaffordable = 0;
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        int id = self->inventory[i];
        if (!id) { free_slots++; continue; }
        int type = ITEMS[id].type, tier = ITEMS[id].tier;
        if ((type == I_SWORD || type == I_BOW) && tier > best_weapon) best_weapon = tier;
        if (type == I_TOOL && tier > best_tool) best_tool = tier;
    }
    for (int i = 0; i < 9 && i < INVENTORY_SIZE; i++) {
        int id = self->inventory[i];
        if (!id || self->is_equipped[i] || env->market[id].stock >= MAX_MARKET_OFFERS) continue;
        int type = ITEMS[id].type, tier = ITEMS[id].tier;
        if (((type == I_SWORD || type == I_BOW) && tier >= best_weapon) ||
            (type == I_TOOL && tier >= best_tool)) continue;
        sellable++;
    }
    offset = append_text(context->text, offset,
        "\nMARKET\nbuys=%d\nsells=%d\nbest_owned_weapon_tier=%d\n"
        "best_owned_tool_tier=%d\ngold=%d\nfree_inventory_slots=%d\n"
        "surplus_sellable_slots=%d\n", context->market_buys, context->market_sells,
        best_weapon, best_tool, self->gold, free_slots, sellable);
    for (int tier = 1; tier <= env->tiers && tier <= MAX_TIERS; tier++) {
        int types[3] = {I_SWORD, I_BOW, I_TOOL};
        for (int j = 0; j < 3; j++) {
            int type = types[j];
            if (tier <= (type == I_TOOL ? best_tool : best_weapon)) continue;
            int id = item_index(type, tier);
            ItemMarket* market = &env->market[id];
            if (market->stock == 0) continue;
            MarketOffer* offer = &market->offers[market->stock-1];
            if (offer->seller == pid) continue;
            int affordable = offer->price <= self->gold;
            if (affordable) {
                if (type == I_TOOL) affordable_tool++;
                else affordable_weapon++;
            } else upgrades_unaffordable++;
            offset = append_text(context->text, offset,
                "upgrade_offer type=%s tier=%d price=%d affordable=%d\n",
                type == I_TOOL ? "tool" : type == I_SWORD ? "sword" : "bow",
                tier, offer->price, affordable);
        }
    }
    offset = append_text(context->text, offset,
        "affordable_weapon_upgrades=%d\naffordable_tool_upgrades=%d\n"
        "unaffordable_upgrade_offers=%d\n"
        "NOTE: SELL lists an item; gold is received only if another agent BUYs it.\n",
        affordable_weapon, affordable_tool, upgrades_unaffordable);
    if (offset < 0) return -1;
    context->text[STRATEGY_CONTEXT_MAX - 1] = '\0';
    return offset >= STRATEGY_CONTEXT_MAX ? STRATEGY_CONTEXT_MAX - 1 : offset;
}

static int parse_strategy_value(const char* response, const char* key, float* value) {
    char pattern[96];
    const char* field;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    field = strstr(response, pattern);
    if (field == NULL || sscanf(field + strlen(pattern), " %*[: ] %f", value) != 1) {
        return 0;
    }
    return *value >= 0.0f && *value <= 1.0f;
}

static int parse_token_count(const char* response, const char* key, int* value) {
    char pattern[96];
    const char* field;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    field = strstr(response, pattern);
    if (field == NULL || sscanf(field + strlen(pattern), " %*[: ] %d", value) != 1) {
        return 0;
    }
    return *value >= 0;
}

int nmmo3_strategy_step(MMO* env, int pid) {
    int input_pipe[2], output_pipe[2], status;
    pid_t child;
    StrategyContext context;
    ssize_t bytes_read;

    if (env->strategy_pid > 0) {
        bytes_read = read(env->strategy_output_fd,
            env->strategy_response + env->strategy_response_bytes,
            sizeof(env->strategy_response) - 1 - env->strategy_response_bytes);
        if (bytes_read > 0) env->strategy_response_bytes += (int)bytes_read;
        if (waitpid(env->strategy_pid, &status, WNOHANG) == env->strategy_pid) {
            close(env->strategy_output_fd);
            env->strategy_output_fd = -1;
            env->strategy_pid = -1;
            strategy_active_env = NULL;
            env->strategy_response[env->strategy_response_bytes] = '\0';
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                int input_tokens, output_tokens;
                static const char* names[8] = {
                    "explore", "harvest", "equip", "trade",
                    "engage_NPC", "avoid_combat", "retreat", "recover"
                };
                static const char* scalar_keys[10] = {
                    "risk_tolerance", "combat_aggressiveness", "retreat_tendency",
                    "cooperation", "competition", "trade_preference",
                    "aggressive_opponent_response", "cooperative_opponent_response",
                    "unexpected_event_response", "confidence"
                };
                float value;
                int found = 0;
                if (parse_token_count(env->strategy_response, "llm_input_tokens", &input_tokens) &&
                    parse_token_count(env->strategy_response, "llm_output_tokens", &output_tokens)) {
                    env->log.llm_input_tokens = (float)input_tokens;
                    env->log.llm_output_tokens = (float)output_tokens;
                    env->llm_cumulative_input_tokens += (unsigned int)input_tokens;
                    env->llm_cumulative_output_tokens += (unsigned int)output_tokens;
                    env->log.llm_cumulative_input_tokens =
                        (float)env->llm_cumulative_input_tokens;
                    env->log.llm_cumulative_output_tokens =
                        (float)env->llm_cumulative_output_tokens;
                } else {
                    fprintf(stderr, "LLM response is missing valid token usage metadata\n");
                }
                const char* strategy_parameters = strstr(
                    env->strategy_response, "\"strategy_parameters\"");
                memset(env->strategy_features, 0, sizeof(env->strategy_features));
                for (int i = 0; i < 8; i++) {
                    if (strategy_parameters != NULL &&
                        parse_strategy_value(strategy_parameters, names[i], &value)) {
                        env->strategy_features[i] = (unsigned char)(value * 255.0f + 0.5f);
                        found = 1;
                    }
                }
                for (int i = 0; i < 10; i++) {
                    if (parse_strategy_value(env->strategy_response, scalar_keys[i], &value)) {
                        env->strategy_features[8 + i] = (unsigned char)(value * 255.0f + 0.5f);
                        found = 1;
                    }
                }
                for (int i = 0; i < 8; i++) {
                    char expected[64];
                    snprintf(expected, sizeof(expected), "\"strategy_id\": \"%s\"", names[i]);
                    if (strstr(env->strategy_response, expected) != NULL) {
                        env->strategy_features[18 + i] = 255;
                        break;
                    }
                }
                if (found) {
                    env->log.strategy_updates += 1;
                    const char* backend = getenv("NMMO3_STRATEGY_BACKEND");
                    const char* backend_name = backend ? backend : "LLM";
                    printf("\r\033[2K[%s] strategy update applied at tick %d", backend_name, env->tick);
                    fflush(stdout);
                    return 1;
                }
            }
            env->log.strategy_failures += 1;
            const char* backend = getenv("NMMO3_STRATEGY_BACKEND");
            const char* backend_name = backend ? backend : "LLM";
            printf("\r\033[2K[%s] strategy update failed; next update will be attempted at the next interval.", backend_name);
            fflush(stdout);
        }
        return 0;
    }

    if (strategy_active_env != NULL) return 0;

    if (build_strategy_context(env, pid, &context) < 0 ||
        pipe(input_pipe) < 0 || pipe(output_pipe) < 0) return 0;
    env->log.strategy_decisions += 1;
    child = fork();
    if (child == 0) {
        const char* python = getenv("NMMO3_STRATEGY_PYTHON");
        const char* script = getenv("NMMO3_STRATEGY_SCRIPT");
        if (python == NULL || python[0] == '\0') python = "python3";
        if (script == NULL || script[0] == '\0') {
            script = "ocean/nmmo3/ollama_strategy.py";
        }
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execlp(python, python, script, (char*)NULL);
        _exit(127);
    }
    if (child < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        return 0;
    }
    close(input_pipe[0]);
    close(output_pipe[1]);
    (void)write(input_pipe[1], context.text, strlen(context.text));
    close(input_pipe[1]);
    (void)fcntl(output_pipe[0], F_SETFL, fcntl(output_pipe[0], F_GETFL) | O_NONBLOCK);
    env->strategy_pid = child;
    strategy_active_env = env;
    env->strategy_output_fd = output_pipe[0];
    env->strategy_response_bytes = 0;
    return 0;
}

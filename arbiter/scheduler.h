#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <semaphore.h>
#include <pthread.h>
#include <cstdlib>
#include <cstdio>
#include "shared/shared_state.h"
#include "shared/inventory.h"
#include "shared/artifacts.h"
#include "arbiter/init.h"

using namespace std;

// Append a message to the circular action log.
// Caller MUST hold state_mutex.
inline void log_action(SharedState* state, const char* msg) {
    strncpy(state->action_log[state->action_log_head], msg, 127);
    state->action_log[state->action_log_head][127] = '\0';
    state->action_log_head =
        (state->action_log_head + 1) % ACTION_LOG_SIZE;
}

inline bool npc_weapon_active(SharedState* state) {
    return state->npc_weapon_damage_bonus > 0;
}

inline int strike_damage_for(SharedState* state, Entity* actor) {
    if (!actor->is_player && npc_weapon_active(state))
        return actor->damage + state->npc_weapon_damage_bonus;
    return actor->damage;
}

// After a turn, offer the Eclipse Relic to the current actor if it's free
inline void offer_eclipse_relic(SharedState* state, int actor_idx) {
    if (!state->artifacts.eclipse_relic_exists) return;
    if (!state->artifacts.eclipse_relic_free)   return;

    Entity* actor = &state->entities[actor_idx];
    if (!actor->is_alive) return;

    if (actor->is_player) {
        // Set flag — HIP will show a Y/N prompt, same as weapon drop
        state->awaiting_eclipse_response = true;
        state->eclipse_accept            = false;

        // Wait for renderer to resolve the prompt
        while (state->awaiting_eclipse_response &&
               state->game_status == GAME_RUNNING) {
            usleep(10000);
        }

        if (state->game_status != GAME_RUNNING) return;

        if (state->eclipse_accept) {
            acquire_artifact(state, actor_idx, ARTIFACT_ECLIPSE_RELIC);
            cout << "[ECLIPSE] " << actor->name
                 << " picked up the Eclipse Relic!" << endl;
            char buf[128];
            snprintf(buf, sizeof(buf), "%s picked up the Eclipse Relic!",
                     actor->name);
            pthread_mutex_lock(&state->state_mutex);
            log_action(state, buf);
            pthread_mutex_unlock(&state->state_mutex);
        }
    } else {
        // NPC always picks it up
        acquire_artifact(state, actor_idx, ARTIFACT_ECLIPSE_RELIC);
        cout << "[ECLIPSE] " << actor->name
             << " (NPC) grabbed the Eclipse Relic!" << endl;
        char buf[128];
        snprintf(buf, sizeof(buf), "%s (NPC) grabbed the Eclipse Relic!",
                 actor->name);
        pthread_mutex_lock(&state->state_mutex);
        log_action(state, buf);
        pthread_mutex_unlock(&state->state_mutex);
    }
}

// Sabse pehle full stamina wala entity dhundho — fair round-robin
// when multiple entities are tied at max stamina.
inline int find_next_actor(SharedState* state) {
    // Collect ALL entities that are at max stamina
    int candidates[MAX_ENTITIES];
    int count = 0;

    for (int i = 0; i < state->total_entities; i++) {
        Entity* e = &state->entities[i];
        if (e->is_alive && !e->is_stunned &&
            e->stamina >= e->max_stamina) {
            candidates[count++] = i;
        }
    }

    if (count == 0) return -1;
    if (count == 1) return candidates[0];

    // Multiple entities ready — pick the one that comes
    // AFTER last_actor_index in a circular order.
    // This guarantees round-robin when speeds are equal.
    int last = state->last_actor_index;

    // Find the candidate with the smallest index that is
    // strictly greater than last. If none, wrap around
    // to the smallest index overall.
    int best = -1;
    for (int i = 0; i < count; i++) {
        if (candidates[i] > last) {
            if (best == -1 || candidates[i] < candidates[best])
                best = i;
        }
    }
    if (best == -1) best = 0; // wrap: pick lowest index

    return candidates[best];
}

// Ek tick mein sab entities ki stamina badhao
inline void tick_stamina(SharedState* state) {
    pthread_mutex_lock(&state->state_mutex);
    for (int i = 0; i < state->total_entities; i++) {
        Entity* e = &state->entities[i];
        if (e->is_alive && !e->is_stunned) {
            e->stamina += e->speed;
            if (e->stamina > e->max_stamina)
                e->stamina = e->max_stamina;
        }
    }
    pthread_mutex_unlock(&state->state_mutex);
}

// Action apply karo jo HIP ya ASP ne submit ki
inline void apply_action(SharedState* state, ActionSlot* action) {
    Entity* actor  = &state->entities[action->actor_index];
    Entity* target = (action->target_index >= 0)
                     ? &state->entities[action->target_index]
                     : nullptr;

    switch (action->action) {
        case ACTION_ATTACK_STRIKE:
            if (target && target->is_alive) {
                int strike_damage = strike_damage_for(state, actor);
                target->hp -= strike_damage;
                state->anim_attacker_idx = action->actor_index;
                state->anim_target_idx   = action->target_index;
                state->anim_is_kill      = (target->hp <= 0);
                state->anim_pending      = true;
                if (target->hp <= 0) {
                    target->hp       = 0;
                    target->is_alive = false;
                    release_all_artifacts(state, action->target_index);
                    if (!target->is_player) {
                        state->enemies_killed++;
                        // First kill always drops; subsequent kills 65%
                        bool do_drop = false;
                        if (!state->first_kill_done) {
                            do_drop = true;
                            state->first_kill_done = true;
                        } else {
                            do_drop = (rand() % 100 < 65);
                        }
                        if (do_drop) {
                            state->pending_drop_weapon_id = rand() % 8;
                            // Ensure we don't drop the same weapon twice
                            while (state->pending_drop_weapon_id == state->last_dropped_weapon) {
                                state->pending_drop_weapon_id = rand() % 8;
                            }
                            state->last_dropped_weapon = state->pending_drop_weapon_id;
                            cout << "[DROP] " << target->name
                                 << " dropped a weapon!" << endl;
                        }
                        // Spawn replacement if alive NPC count < initial
                        // and hard cap not reached
                        if (state->player_count < 4 &&
                            state->total_npcs_spawned < MAX_TOTAL_NPCS) {
                            int alive_npcs = 0;
                            for (int ii = state->player_count;
                                 ii < state->total_entities; ii++)
                                if (state->entities[ii].is_alive) alive_npcs++;
                            if (alive_npcs < state->npc_count) {
                                state->spawn_pending_count++;
                                spawn_npc_entity(state);
                            }
                        }
                    }
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%s killed %s!",
                             actor->name, target->name);
                    log_action(state, buf);
                    cout << "[ACTION] " << buf << endl;
                } else {
                    char buf[128];
                    if (!actor->is_player && npc_weapon_active(state)) {
                        snprintf(buf, sizeof(buf),
                                 "%s dealt %d damage to %s (%d base + %d weapons). HP: %d",
                                 actor->name,
                                 strike_damage,
                                 target->name,
                                 actor->damage,
                                 state->npc_weapon_damage_bonus,
                                 target->hp);
                    } else {
                        snprintf(buf, sizeof(buf),
                                 "%s dealt %d damage to %s. HP: %d",
                                 actor->name, strike_damage,
                                 target->name, target->hp);
                    }
                    log_action(state, buf);
                    cout << "[ACTION] " << buf << endl;
                }
            }
            actor->stamina = 0;
            break;

        case ACTION_ATTACK_EXHAUST:
            if (target && target->is_alive) {
                state->anim_attacker_idx = action->actor_index;
                state->anim_target_idx   = action->target_index;
                state->anim_is_kill      = false;
                state->anim_pending      = true;
                int exhaust_dmg = actor->damage * 2;
                target->stamina -= exhaust_dmg;
                if (target->stamina < 0) target->stamina = 0;
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "%s reduced %s's stamina by %d. Stamina: %d",
                         actor->name, target->name,
                         exhaust_dmg, target->stamina);
                log_action(state, buf);
                cout << "[ACTION] " << actor->name
                     << " reduced " << target->name
                     << "'s stamina by " << exhaust_dmg
                     << ". Stamina: " << target->stamina << endl;
            }
            actor->stamina = 0;
            break;

        case ACTION_STUN:
            // Stun signal target process ko bhejna
            if (target && target->is_alive) {
                state->anim_attacker_idx = action->actor_index;
                state->anim_target_idx   = action->target_index;
                state->anim_is_kill      = false;
                state->anim_pending      = true;
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "%s stunned %s for 3 seconds!",
                         actor->name, target->name);
                log_action(state, buf);
                cout << "[ACTION] " << buf << endl;
                state->stun_target_index = action->target_index;  // store BEFORE signal
                target->is_stunned = true;

                // Signal the correct process
                pid_t target_pid = target->is_player
                                   ? state->hip_pid
                                   : state->asp_pid;
                kill(target_pid, SIGUSR1);
            }
            actor->stamina = 0;
            break;

        case ACTION_ULTIMATE: {
            char buf[128];
            if (!check_ultimate_eligibility(state, action->actor_index)) {
                snprintf(buf, sizeof(buf),
                         "%s lacks Solar Core + Lunar Blade — Ultimate rejected.",
                         actor->name);
                log_action(state, buf);
                cout << "[ACTION] " << buf << endl;
                actor->stamina = (int)(actor->max_stamina * 0.5);
            } else {
                snprintf(buf, sizeof(buf),
                         "%s triggered Ultimate Ability! ASP frozen for 10 seconds.",
                         actor->name);
                log_action(state, buf);
                cout << "[ACTION] " << buf << endl;
                kill(state->asp_pid, SIGSTOP);
                alarm(10);
                actor->stamina = 0;
            }
            break;
        }

        case ACTION_USE_WEAPON: {
            if (target && target->is_alive &&
                action->weapon_id >= 0 &&
                action->weapon_id < (int)(sizeof(WEAPON_TABLE)/sizeof(WEAPON_TABLE[0]))) {
                int dmg = WEAPON_TABLE[action->weapon_id].damage;
                target->hp -= dmg;
                state->anim_attacker_idx = action->actor_index;
                state->anim_target_idx   = action->target_index;
                state->anim_is_kill      = (target->hp <= 0);
                state->anim_pending      = true;
                if (target->hp <= 0) {
                    target->hp       = 0;
                    target->is_alive = false;
                    release_all_artifacts(state, action->target_index);
                    if (!target->is_player) {
                        state->enemies_killed++;
                        // First kill always drops; subsequent kills 65%
                        bool do_drop = false;
                        if (!state->first_kill_done) {
                            do_drop = true;
                            state->first_kill_done = true;
                        } else {
                            do_drop = (rand() % 100 < 65);
                        }
                        if (do_drop) {
                            state->pending_drop_weapon_id = rand() % 8;
                            // Ensure we don't drop the same weapon twice
                            while (state->pending_drop_weapon_id == state->last_dropped_weapon) {
                                state->pending_drop_weapon_id = rand() % 8;
                            }
                            state->last_dropped_weapon = state->pending_drop_weapon_id;
                            cout << "[DROP] " << target->name
                                 << " dropped a weapon!" << endl;
                        }
                        if (state->player_count < 4 &&
                            state->total_npcs_spawned < MAX_TOTAL_NPCS) {
                            int alive_npcs = 0;
                            for (int ii = state->player_count;
                                 ii < state->total_entities; ii++)
                                if (state->entities[ii].is_alive) alive_npcs++;
                            if (alive_npcs < state->npc_count) {
                                state->spawn_pending_count++;
                                spawn_npc_entity(state);
                            }
                        }
                    }
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "%s (weapon) killed %s!",
                             actor->name, target->name);
                    log_action(state, buf);
                    cout << "[ACTION] " << buf << endl;
                } else {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "%s used %s for %d damage on %s. HP: %d",
                             actor->name,
                             WEAPON_TABLE[action->weapon_id].name,
                             dmg, target->name, target->hp);
                    log_action(state, buf);
                    cout << "[ACTION] " << buf << endl;
                }
            }
            actor->stamina = 0;
            break;
        }

        case ACTION_SWAP_IN: {
            swap_in(actor, action->weapon_id);
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "%s swapped in weapon from LTS slot %d.",
                     actor->name, action->weapon_id);
            log_action(state, buf);
            cout << "[ACTION] " << buf << endl;
            actor->stamina = 0;
            break;
        }

        case ACTION_HEAL: {
            actor->hp += (int)(actor->max_hp * 0.1);
            if (actor->hp > actor->max_hp)
                actor->hp = actor->max_hp;
            char buf[128];
            snprintf(buf, sizeof(buf), "%s healed. HP: %d",
                     actor->name, actor->hp);
            log_action(state, buf);
            cout << "[ACTION] " << buf << endl;
            actor->stamina = 0;
            break;
        }

        case ACTION_SKIP: {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s skipped their turn.",
                     actor->name);
            log_action(state, buf);
            cout << "[ACTION] " << buf << endl;
            actor->stamina = (int)(actor->max_stamina * 0.5);
            break;
        }

        case ACTION_QUIT: {
            char buf[128];
            snprintf(buf, sizeof(buf), "Player quit the game.");
            log_action(state, buf);
            cout << "[ACTION] " << buf << endl;
            state->game_status = GAME_QUIT;
            actor->stamina     = 0;
            break;
        }

        default:
            cout << "[ACTION] Unknown action — assuming skip." << endl;
            actor->stamina = (int)(actor->max_stamina * 0.5);
            break;
    }
}

// Win/lose/quit conditions check karo
inline void check_game_status(SharedState* state) {
    if (state->game_status == GAME_QUIT) return;

    if (state->enemies_killed >= 10) {
        state->game_status = GAME_WIN;
        cout << "[GAME] Players win! 10 enemies killed." << endl;
        return;
    }

    bool any_player_alive = false;
    for (int i = 0; i < state->player_count; i++) {
        if (state->entities[i].is_alive) {
            any_player_alive = true;
            break;
        }
    }

    if (!any_player_alive) {
        state->game_status = GAME_LOSE;
        cout << "[GAME] All players dead. Game over." << endl;
    }
}

// Player ya NPC turn k liye action slot ka wait karo
inline bool wait_for_action(SharedState* state, int actor_idx) {
    bool is_player = state->entities[actor_idx].is_player;

    if (is_player) {
        while (true) {
            pthread_mutex_lock(&state->action_mutex);
            bool ready = state->action_slot.ready;
            pthread_mutex_unlock(&state->action_mutex);
            if (ready) return true;
            usleep(10000);
        }
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 3;

        while (true) {
            pthread_mutex_lock(&state->action_mutex);
            bool ready = state->action_slot.ready;
            pthread_mutex_unlock(&state->action_mutex);
            if (ready) return true;

            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec >= ts.tv_sec) return false;

            usleep(10000);
        }
    }
}

// Main scheduling loop — game tab tak chalta hai jab tak koi condition trigger na ho
inline void run_scheduler(SharedState* state) {
    cout << "[SCHEDULER] Scheduling loop started." << endl;

    while (state->game_status == GAME_RUNNING) {
        usleep(100000);
        tick_stamina(state);

        int actor_idx = find_next_actor(state);
        if (actor_idx == -1)
            continue;

        state->last_actor_index = actor_idx;

        Entity* actor = &state->entities[actor_idx];
        cout << "[SCHEDULER] " << actor->name << "'s turn." << endl;

        pthread_mutex_lock(&state->action_mutex);
        state->action_slot.ready = false;
        state->current_turn      = actor_idx;
        pthread_mutex_unlock(&state->action_mutex);

        if (actor->is_player)
            sem_post(&state->player_turn_sem);
        else
            sem_post(&state->npc_turn_sem);

        bool action_received = wait_for_action(state, actor_idx);

        if (!action_received) {
            cout << "[SCHEDULER] " << actor->name
                 << " timed out — assuming skip." << endl;
            pthread_mutex_lock(&state->action_mutex);
            state->action_slot.ready        = true;
            state->action_slot.actor_index  = actor_idx;
            state->action_slot.target_index = -1;
            state->action_slot.action       = ACTION_SKIP;
            pthread_mutex_unlock(&state->action_mutex);
        }

        pthread_mutex_lock(&state->state_mutex);
        apply_action(state, &state->action_slot);
        check_game_status(state);
        // At 5+ kills: unlock reinforcements and spawn Eclipse Relic.
        if (state->enemies_killed >= 5) {
            if (!state->spawning_unlocked) {
                state->spawning_unlocked = true;
                cout << "[SPAWN] Enemy reinforcements incoming!" << endl;
            }
            if (!state->artifacts.eclipse_relic_exists) {
                pthread_mutex_unlock(&state->state_mutex);
                spawn_eclipse_relic(state);
                // re-lock not needed, eclipse spawn handles it
            } else {
                pthread_mutex_unlock(&state->state_mutex);
            }
        } else {
            pthread_mutex_unlock(&state->state_mutex);
        }

        // Offer Eclipse Relic to current actor if it just became free
        if (state->game_status == GAME_RUNNING)
            offer_eclipse_relic(state, actor_idx);
    }

    cout << "[SCHEDULER] Game ended. Status: " << state->game_status << endl;
}

#endif

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <semaphore.h>
#include <pthread.h>
#include <cstdlib>
#include "shared/shared_state.h"
#include "shared/inventory.h"
#include "shared/artifacts.h"

using namespace std;

// Sabse pehle full stamina wala entity dhundho
inline int find_next_actor(SharedState* state) {
    int best_idx   = -1;
    int best_stamp = -1;

    for (int i = 0; i < state->total_entities; i++) {
        Entity* e = &state->entities[i];
        if (e->is_alive && !e->is_stunned &&
            e->stamina >= e->max_stamina) {
            if (e->stamina > best_stamp) {
                best_stamp = e->stamina;
                best_idx   = i;
            }
        }
    }
    return best_idx;
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
                target->hp -= actor->damage;
                if (target->hp <= 0) {
                    target->hp       = 0;
                    target->is_alive = false;
                    release_all_artifacts(state, action->target_index);
                    if (!target->is_player) {
                        state->enemies_killed++;
                        // 40% chance weapon drop
                        if (rand() % 100 < 40) {
                            state->pending_drop_weapon_id = rand() % 8;
                            cout << "[DROP] " << target->name
                                 << " dropped a weapon!" << endl;
                        }
                    }
                    cout << "[ACTION] " << actor->name
                         << " killed " << target->name << "!" << endl;
                } else {
                    cout << "[ACTION] " << actor->name
                         << " dealt " << actor->damage
                         << " damage to " << target->name
                         << ". HP: " << target->hp << endl;
                }
            }
            actor->stamina = 0;
            break;

        case ACTION_ATTACK_EXHAUST:
            if (target && target->is_alive) {
                target->stamina -= actor->damage;
                if (target->stamina < 0) target->stamina = 0;
                cout << "[ACTION] " << actor->name
                     << " reduced " << target->name
                     << "'s stamina by " << actor->damage
                     << ". Stamina: " << target->stamina << endl;
            }
            actor->stamina = 0;
            break;

        case ACTION_STUN:
            // Stun signal target process ko bhejna
            if (target && target->is_alive) {
                cout << "[ACTION] " << actor->name
                     << " stunned " << target->name
                     << " for 3 seconds!" << endl;
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

        case ACTION_ULTIMATE:
            if (!check_ultimate_eligibility(state, action->actor_index)) {
                cout << "[ACTION] " << actor->name
                     << " lacks Solar Core + Lunar Blade — Ultimate rejected." << endl;
                actor->stamina = (int)(actor->max_stamina * 0.5);
            } else {
                cout << "[ACTION] " << actor->name
                     << " triggered Ultimate Ability! ASP frozen for 10 seconds." << endl;
                kill(state->asp_pid, SIGSTOP);
                alarm(10);
                actor->stamina = 0;
            }
            break;

        case ACTION_USE_WEAPON: {
            if (target && target->is_alive &&
                action->weapon_id >= 0 &&
                action->weapon_id < (int)(sizeof(WEAPON_TABLE)/sizeof(WEAPON_TABLE[0]))) {
                int dmg = WEAPON_TABLE[action->weapon_id].damage;
                target->hp -= dmg;
                if (target->hp <= 0) {
                    target->hp       = 0;
                    target->is_alive = false;
                    release_all_artifacts(state, action->target_index);
                    if (!target->is_player) {
                        state->enemies_killed++;
                        if (rand() % 100 < 40) {
                            state->pending_drop_weapon_id = rand() % 8;
                            cout << "[DROP] " << target->name
                                 << " dropped a weapon!" << endl;
                        }
                    }
                    cout << "[ACTION] " << actor->name
                         << " (weapon) killed " << target->name << "!" << endl;
                } else {
                    cout << "[ACTION] " << actor->name
                         << " used " << WEAPON_TABLE[action->weapon_id].name
                         << " for " << dmg
                         << " damage on " << target->name
                         << ". HP: " << target->hp << endl;
                }
            }
            actor->stamina = 0;
            break;
        }

        case ACTION_SWAP_IN:
            swap_in(actor, action->weapon_id);
            cout << "[ACTION] " << actor->name
                 << " swapped in weapon from LTS slot "
                 << action->weapon_id << "." << endl;
            actor->stamina = 0;
            break;

        case ACTION_HEAL:
            actor->hp += (int)(actor->max_hp * 0.1);
            if (actor->hp > actor->max_hp)
                actor->hp = actor->max_hp;
            cout << "[ACTION] " << actor->name
                 << " healed. HP: " << actor->hp << endl;
            actor->stamina = 0;
            break;

        case ACTION_SKIP:
            cout << "[ACTION] " << actor->name
                 << " skipped their turn." << endl;
            actor->stamina = (int)(actor->max_stamina * 0.5);
            break;

        case ACTION_QUIT:
            cout << "[ACTION] Player quit the game." << endl;
            state->game_status = GAME_QUIT;
            actor->stamina     = 0;
            break;

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
        // Spawn Eclipse Relic at exactly 5 kills
        if (state->enemies_killed == 5 && !state->artifacts.eclipse_relic_exists) {
            pthread_mutex_unlock(&state->state_mutex);
            spawn_eclipse_relic(state);
        } else {
            pthread_mutex_unlock(&state->state_mutex);
        }
    }

    cout << "[SCHEDULER] Game ended. Status: " << state->game_status << endl;
}

#endif
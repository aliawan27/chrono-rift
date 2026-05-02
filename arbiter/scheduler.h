#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <semaphore.h>
#include <pthread.h>
#include "shared/shared_state.h"

using namespace std;

// Sabse pehle full stamina wala entity dhundho
inline int find_next_actor(SharedState* state) {
    for (int i = 0; i < state->total_entities; i++) {
        Entity* e = &state->entities[i];
        if (e->is_alive && !e->is_stunned &&
            e->stamina >= e->max_stamina) {
            return i;
        }
    }
    return -1;
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
                    target->hp = 0;
                    target->is_alive = false;
                    if (!target->is_player)
                        state->enemies_killed++;
                    cout << "[ACTION] " << actor->name
                         << " ne " << target->name
                         << " ko kill kar diya!" << endl;
                } else {
                    cout << "[ACTION] " << actor->name
                         << " ne " << target->name
                         << " par " << actor->damage
                         << " damage kiya. HP: " << target->hp << endl;
                }
            }
            actor->stamina = 0;
            break;

        case ACTION_ATTACK_EXHAUST:
            if (target && target->is_alive) {
                target->stamina -= actor->damage;
                if (target->stamina < 0) target->stamina = 0;
                cout << "[ACTION] " << actor->name
                     << " ne " << target->name
                     << " ki stamina " << actor->damage
                     << " se ghata di." << endl;
            }
            actor->stamina = 0;
            break;

        case ACTION_HEAL:
            actor->hp += actor->max_hp * 0.1;
            if (actor->hp > actor->max_hp)
                actor->hp = actor->max_hp;
            cout << "[ACTION] " << actor->name
                 << " ne heal kiya. HP: " << actor->hp << endl;
            actor->stamina = 0;
            break;

        case ACTION_SKIP:
            cout << "[ACTION] " << actor->name
                 << " ne turn skip kiya." << endl;
            actor->stamina = actor->max_stamina * 0.5;
            break;

        case ACTION_QUIT:
            cout << "[ACTION] Player ne quit kiya." << endl;
            state->game_status = GAME_QUIT;
            actor->stamina = 0;
            break;

        default:
            cout << "[ACTION] Unknown action — skip assume kar raha hai." << endl;
            actor->stamina = actor->max_stamina * 0.5;
            break;
    }
}

// Win/lose/quit conditions check karo
inline void check_game_status(SharedState* state) {
    if (state->game_status == GAME_QUIT) return;

    if (state->enemies_killed >= 10) {
        state->game_status = GAME_WIN;
        cout << "[GAME] Players jeet gaye! 10 enemies kill ho gaye." << endl;
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
        cout << "[GAME] Saare players mar gaye. Game over." << endl;
    }
}

// Main scheduling loop — game tab tak chalta hai jab tak koi condition trigger na ho
inline void run_scheduler(SharedState* state) {
    cout << "[SCHEDULER] Scheduling loop started." << endl;

    while (state->game_status == GAME_RUNNING) {

        // Stamina tick karo
        tick_stamina(state);

        // Dekho koi act kar sakta hai
        int actor_idx = find_next_actor(state);
        if (actor_idx == -1) {
            usleep(100000); // 100ms wait
            continue;
        }

        Entity* actor = &state->entities[actor_idx];
        cout << "[SCHEDULER] " << actor->name << "'s turn." << endl;

        // Action slot clear karo
        pthread_mutex_lock(&state->action_mutex);
        state->action_slot.ready = false;
        state->current_turn = actor_idx;
        pthread_mutex_unlock(&state->action_mutex);

        // HIP ya ASP ko signal do ke unki baari hai
        sem_post(&state->turn_sem);

        // 3 second wait karo action k liye (NPC timeout)
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 3;

        bool action_received = false;
        while (!action_received) {
            pthread_mutex_lock(&state->action_mutex);
            if (state->action_slot.ready) {
                action_received = true;
            }
            pthread_mutex_unlock(&state->action_mutex);

            // Timeout check
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec >= ts.tv_sec) {
                cout << "[SCHEDULER] " << actor->name
                     << " did not give action in 3 sec, so assumed skip." << endl;
                pthread_mutex_lock(&state->action_mutex);
                state->action_slot.ready       = true;
                state->action_slot.actor_index  = actor_idx;
                state->action_slot.target_index = -1;
                state->action_slot.action       = ACTION_SKIP;
                pthread_mutex_unlock(&state->action_mutex);
                action_received = true;
            }

            if (!action_received) usleep(10000); // 10ms poll
        }

        // Action apply karo
        pthread_mutex_lock(&state->state_mutex);
        apply_action(state, &state->action_slot);
        check_game_status(state);
        pthread_mutex_unlock(&state->state_mutex);
    }

    cout << "[SCHEDULER] Game ended. Status: " << state->game_status << endl;
}

#endif
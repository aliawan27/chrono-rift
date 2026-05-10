#ifndef INPUT_H
#define INPUT_H

#include <unistd.h>
#include <csignal>
#include <pthread.h>
#include "shared/shared_state.h"
#include "shared/inventory.h"

// Player se action lo via shared memory — renderer (Arbiter) keyboard
// events fill kar k pending=true set karta hai. Yahan se cin nahi hota.
inline void get_player_action(int player_idx, SharedState* state) {

    Entity* player = &state->entities[player_idx];

    // Handle weapon drop first if one is pending
    if (state->pending_drop_weapon_id != -1) {
        int dropped_weapon_id = state->pending_drop_weapon_id;

        // Signal renderer to show drop prompt
        state->awaiting_drop_response = true;

        // Wait for renderer to clear awaiting_drop_response after Y/N
        while (state->awaiting_drop_response &&
               state->game_status == GAME_RUNNING) {
            usleep(10000);
        }

        if (state->game_status != GAME_RUNNING) return;

        if (state->player_input.drop_accept) {
            pthread_mutex_lock(&state->state_mutex);
            bool ok = allocate_weapon(player, dropped_weapon_id);
            if (!ok) {
                state->npc_weapon_id = dropped_weapon_id;
                state->npc_weapon_damage_bonus += WEAPON_TABLE[dropped_weapon_id].damage;
                state->npc_should_pickup = true;
            }
            pthread_mutex_unlock(&state->state_mutex);
        } else {
            pthread_mutex_lock(&state->state_mutex);
            state->npc_weapon_id = dropped_weapon_id;
            state->npc_weapon_damage_bonus += WEAPON_TABLE[dropped_weapon_id].damage;
            state->npc_should_pickup = true;
            pthread_mutex_unlock(&state->state_mutex);
        }
        state->pending_drop_weapon_id = -1;
    }

    // Signal renderer that this player needs an action choice
    pthread_mutex_lock(&state->player_input.mutex);
    state->awaiting_player_input = true;
    state->awaiting_player_idx   = player_idx;
    state->player_input.pending  = false;
    pthread_mutex_unlock(&state->player_input.mutex);

    // Wait for renderer to fill player_input
    bool got = false;
    while (state->game_status == GAME_RUNNING) {
        usleep(10000);
        pthread_mutex_lock(&state->player_input.mutex);
        got = state->player_input.pending;
        pthread_mutex_unlock(&state->player_input.mutex);
        if (got) break;
    }

    if (!got) return;

    // Build and submit action from player_input
    ActionSlot action;
    action.actor_index  = player_idx;
    action.target_index = state->player_input.target_index;
    action.weapon_id    = state->player_input.weapon_id;
    action.ready        = true;

    switch (state->player_input.choice) {
        case 1:  action.action = ACTION_ATTACK_STRIKE;  break;
        case 2:  action.action = ACTION_ATTACK_EXHAUST; break;
        case 3:  action.action = ACTION_HEAL;           break;
        case 4:  action.action = ACTION_SKIP;           break;
        case 5:  action.action = ACTION_USE_WEAPON;     break;
        case 6:  action.action = ACTION_SWAP_IN;        break;
        case 7:  action.action = ACTION_STUN;           break;
        case 8:  action.action = ACTION_ULTIMATE;       break;
        case 9:
            action.action = ACTION_QUIT;
            kill(state->arbiter_pid, SIGTERM);
            break;
        default: action.action = ACTION_SKIP;           break;
    }

    pthread_mutex_lock(&state->action_mutex);
    state->action_slot = action;
    pthread_mutex_unlock(&state->action_mutex);

    // Clear input state
    pthread_mutex_lock(&state->player_input.mutex);
    state->awaiting_player_input = false;
    state->player_input.pending  = false;
    pthread_mutex_unlock(&state->player_input.mutex);
}

#endif

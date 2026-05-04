#ifndef INPUT_H
#define INPUT_H

#include <iostream>
#include <string>
#include <csignal>
#include "shared/shared_state.h"

using namespace std;

// Player se action input lene aur parse karne k liye
inline void print_turn_menu(Entity* player, SharedState* state) {
    cout << "\n============================" << endl;
    cout << "Your turn: " << player->name << endl;
    cout << "HP: " << player->hp << "/" << player->max_hp
         << " | Stamina: " << player->stamina
         << "/" << player->max_stamina << endl;
    cout << "----------------------------" << endl;
    cout << "Actions:" << endl;
    cout << "  1. Attack (Strike)  — deal " << player->damage << " HP damage" << endl;
    cout << "  2. Attack (Exhaust) — deal " << player->damage << " stamina damage" << endl;
    cout << "  3. Heal             — restore 10% HP" << endl;
    cout << "  4. Skip             — skip turn" << endl;
    cout << "  5. Stun             — stun target for 3 seconds" << endl;
    cout << "  6. Ultimate         — freeze all NPCs for 10 seconds" << endl;
    cout << "  7. Quit             — quit game" << endl;
    cout << "----------------------------" << endl;

    cout << "Enemies:" << endl;
    for (int i = state->player_count; i < state->total_entities; i++) {
        Entity* e = &state->entities[i];
        if (e->is_alive) {
            cout << "  [" << (i - state->player_count)
                 << "] " << e->name
                 << " | HP: " << e->hp
                 << " | Stamina: " << e->stamina;
            if (e->is_stunned) cout << " [STUNNED]";
            cout << endl;
        }
    }
    cout << "============================" << endl;
}

inline int pick_target(SharedState* state) {
    int target = -1;
    while (target < 0) {
        cout << "Pick target index: ";
        cin >> target;

        int actual = state->player_count + target;
        if (actual < state->player_count ||
            actual >= state->total_entities ||
            !state->entities[actual].is_alive) {
            cout << "Invalid target. Try again." << endl;
            target = -1;
        } else {
            target = actual;
        }
    }
    return target;
}

// Player se action lo aur action slot fill karo
inline void get_player_action(int player_idx, SharedState* state) {
    Entity* player = &state->entities[player_idx];

    print_turn_menu(player, state);

    int choice = 0;
    while (choice < 1 || choice > 7) {
        cout << "Enter choice (1-7): ";
        cin >> choice;
    }

    ActionSlot action;
    action.actor_index  = player_idx;
    action.target_index = -1;
    action.weapon_id    = WEAPON_NONE;
    action.ready        = true;

    switch (choice) {
        case 1:
            action.action       = ACTION_ATTACK_STRIKE;
            action.target_index = pick_target(state);
            break;
        case 2:
            action.action       = ACTION_ATTACK_EXHAUST;
            action.target_index = pick_target(state);
            break;
        case 3:
            action.action = ACTION_HEAL;
            break;
        case 4:
            action.action = ACTION_SKIP;
            break;
        case 5:
            action.action       = ACTION_STUN;
            action.target_index = pick_target(state);
            break;
        case 6:
            action.action = ACTION_ULTIMATE;
            break;
        case 7:
            action.action = ACTION_QUIT;
            // Send SIGTERM to Arbiter
            kill(state->arbiter_pid, SIGTERM);
            break;
        default:
            action.action = ACTION_SKIP;
            break;
    }

    pthread_mutex_lock(&state->action_mutex);
    state->action_slot = action;
    pthread_mutex_unlock(&state->action_mutex);
}

#endif
#ifndef INPUT_H
#define INPUT_H

#include <iostream>
#include <string>
#include <csignal>
#include "shared/shared_state.h"
#include "shared/inventory.h"

using namespace std;

// Player se action input lene aur parse karne k liye
inline void print_turn_menu(Entity* player, SharedState* state) {
    // Weapon drop pickup prompt
    if (state->pending_drop_weapon_id != -1) {
        int drop_id = state->pending_drop_weapon_id;
        cout << "\n[DROP] A weapon dropped: "
             << WEAPON_TABLE[drop_id].name
             << " (" << WEAPON_TABLE[drop_id].damage << " dmg, "
             << WEAPON_TABLE[drop_id].slot_size << " slots). Pick up? (y/n): ";
        char yn = 'n';
        cin >> yn;
        if (yn == 'y' || yn == 'Y') {
            pthread_mutex_lock(&state->state_mutex);
            bool ok = allocate_weapon(player, drop_id);
            pthread_mutex_unlock(&state->state_mutex);
            if (ok) {
                cout << "[DROP] Picked up " << WEAPON_TABLE[drop_id].name << "." << endl;
            } else {
                cout << "[DROP] Inventory full — could not pick up." << endl;
                state->npc_should_pickup = true;
            }
            state->pending_drop_weapon_id = -1;
        } else {
            state->pending_drop_weapon_id = -1;
            state->npc_should_pickup = true;
        }
    }

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
    cout << "  5. Use Weapon       — use a weapon from primary inventory" << endl;
    cout << "  6. Swap In          — bring a weapon from long-term storage" << endl;
    cout << "  7. Stun             — stun target for 3 seconds" << endl;
    cout << "  8. Ultimate         — freeze all NPCs for 10 seconds" << endl;
    cout << "  9. Quit             — quit game" << endl;
    cout << "----------------------------" << endl;

    // Show primary inventory
    cout << "Primary Inventory:" << endl;
    int i = 0;
    bool any = false;
    while (i < INVENTORY_SIZE) {
        int w = player->inventory[i];
        if (w != -1) {
            while (i < INVENTORY_SIZE && player->inventory[i] == w) i++;
            cout << "  [id=" << w << "] " << WEAPON_TABLE[w].name
                 << " (" << WEAPON_TABLE[w].damage << " dmg)" << endl;
            any = true;
        } else { i++; }
    }
    if (!any) cout << "  (empty)" << endl;

    // Show long term storage
    if (player->lts_count > 0) {
        cout << "Long Term Storage:" << endl;
        for (int j = 0; j < player->lts_count; j++)
            cout << "  [" << j << "] " << player->long_term_storage[j].name
                 << " (" << player->long_term_storage[j].damage << " dmg)" << endl;
    }
    cout << "----------------------------" << endl;

    cout << "Enemies:" << endl;
    for (int ei = state->player_count; ei < state->total_entities; ei++) {
        Entity* e = &state->entities[ei];
        if (e->is_alive) {
            cout << "  [" << (ei - state->player_count)
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
    while (choice < 1 || choice > 9) {
        cout << "Enter choice (1-9): ";
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
        case 5: {
            // Use Weapon: pick weapon id from primary inventory
            cout << "Enter weapon id to use: ";
            int wid = -1;
            cin >> wid;
            action.action       = ACTION_USE_WEAPON;
            action.weapon_id    = wid;
            action.target_index = pick_target(state);
            break;
        }
        case 6: {
            // Swap In: pick LTS index
            if (player->lts_count == 0) {
                cout << "Long term storage is empty. Skipping turn." << endl;
                action.action = ACTION_SKIP;
            } else {
                cout << "Enter LTS index to swap in: ";
                int lts_idx = 0;
                cin >> lts_idx;
                action.action    = ACTION_SWAP_IN;
                action.weapon_id = lts_idx;
            }
            break;
        }
        case 7:
            action.action       = ACTION_STUN;
            action.target_index = pick_target(state);
            break;
        case 8:
            action.action = ACTION_ULTIMATE;
            break;
        case 9:
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
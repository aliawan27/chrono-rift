#ifndef INIT_H
#define INIT_H

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include "shared/shared_state.h"

using namespace std;

// Entities ko roll number seed se initialize karna

inline void init_entities(SharedState* state, int player_count, int npc_count) {
    srand(ALI_ROLL);

    state->player_count  = player_count;
    state->npc_count     = npc_count;
    state->total_entities = player_count + npc_count;

    // Players initialize karo
    for (int i = 0; i < player_count; i++) {
        Entity* e = &state->entities[i];
        snprintf(e->name, sizeof(e->name), "Player %d", i + 1);

        // HP = roll number + random between 100 and 1000
        e->max_hp    = ALI_ROLL + (rand() % 901 + 100);
        e->hp        = e->max_hp;
        e->max_stamina = MAX_STAMINA_PLAYER;
        e->stamina   = 0;

        // Speed = 100 / number of players
        e->speed     = 100 / player_count;

        // Damage = last digit of roll + 10
        e->damage    = ROLL_LAST_DIGIT + 10;

        e->is_player = true;
        e->is_alive  = true;
        e->is_stunned = false;
        e->lts_count = 0;

        for (int j = 0; j < INVENTORY_SIZE; j++)
            e->inventory[j] = -1;

        cout << "[INIT] " << e->name
             << " | HP: " << e->hp
             << " | Speed: " << e->speed
             << " | Damage: " << e->damage << endl;
    }

    // NPCs initialize karo
    for (int i = 0; i < npc_count; i++) {
        Entity* e = &state->entities[player_count + i];
        snprintf(e->name, sizeof(e->name), "Enemy %d", i + 1);

        // HP = last 2 digits of roll + random between 50 and 200
        int last_two = ALI_ROLL % 100;
        e->max_hp    = last_two + (rand() % 151 + 50);
        e->hp        = e->max_hp;
        e->max_stamina = MAX_STAMINA_NPC;
        e->stamina   = 0;

        // Speed = random between 10 and 30
        e->speed     = rand() % 21 + 10;

        // Damage = second last digit of roll + 10
        e->damage    = ROLL_SECOND_LAST + 10;

        e->is_player = false;
        e->is_alive  = true;
        e->is_stunned = false;
        e->lts_count = 0;

        for (int j = 0; j < INVENTORY_SIZE; j++)
            e->inventory[j] = -1;

        cout << "[INIT] " << e->name
             << " | HP: " << e->hp
             << " | Speed: " << e->speed
             << " | Damage: " << e->damage << endl;
    }

    // Artifact table initialize karo
    state->artifacts.solar_core_free        = true;
    state->artifacts.solar_core_holder      = -1;
    state->artifacts.solar_core_wanted_by   = -1;
    state->artifacts.lunar_blade_free       = true;
    state->artifacts.lunar_blade_holder     = -1;
    state->artifacts.lunar_blade_wanted_by  = -1;
    state->artifacts.eclipse_relic_exists   = false;
    state->artifacts.eclipse_relic_free     = true;
    state->artifacts.eclipse_relic_holder   = -1;
    state->artifacts.eclipse_relic_wanted_by = -1;
}

#endif
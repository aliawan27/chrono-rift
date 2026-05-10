#include <iostream>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <pthread.h>
#include "shared/shared_memory.h"
#include "shared/inventory.h"

using namespace std;

// ASP — ek thread per NPC, sirf active thread action submit karta hai
static bool         running = true;
static SharedState* state   = nullptr;

struct NpcThreadData {
    int npc_index;
};

// Stun signal handler — 3 second pause, then clear the specific target
void handle_sigusr1(int) {
    sleep(3);
    if (state) {
        int idx = state->stun_target_index;
        if (idx >= 0 && idx < MAX_ENTITIES)
            state->entities[idx].is_stunned = false;
    }
}

void handle_sigterm(int) {
    running = false;
    if (state) sem_post(&state->npc_turn_sem);
}

// Random alive player ka index dhundho. Koi alive nahi to -1.
static int pick_random_alive_player(SharedState* s) {
    int alive[MAX_PLAYERS];
    int count = 0;
    for (int i = 0; i < s->player_count; i++) {
        if (s->entities[i].is_alive)
            alive[count++] = i;
    }
    if (count == 0) return -1;
    return alive[rand() % count];
}

void* npc_thread(void* arg) {
    NpcThreadData* data     = (NpcThreadData*)arg;
    int            my_index = data->npc_index;

    cout << "[ASP] Thread started for "
         << state->entities[my_index].name << endl;

    while (running &&
           state->game_status == GAME_RUNNING &&
           state->entities[my_index].is_alive) {

        sem_wait(&state->npc_turn_sem);

        if (!running ||
            state->game_status != GAME_RUNNING ||
            !state->entities[my_index].is_alive) {
            sem_post(&state->npc_turn_sem);
            break;
        }

        pthread_mutex_lock(&state->action_mutex);
        int current = state->current_turn;
        pthread_mutex_unlock(&state->action_mutex);

        if (current != my_index) {
            sem_post(&state->npc_turn_sem);
            usleep(10000);
            continue;
        }

        // Check if a dropped weapon is waiting for an NPC to pick up
        pthread_mutex_lock(&state->state_mutex);
        if (state->npc_should_pickup &&
            state->npc_weapon_id >= 0 &&
            state->npc_weapon_id < (int)(sizeof(WEAPON_TABLE)/sizeof(WEAPON_TABLE[0]))) {
            int wid = state->npc_weapon_id;
            state->npc_should_pickup = false;
            cout << "[ASP] " << state->entities[my_index].name
                 << " picked up " << WEAPON_TABLE[wid].name
                 << ". All enemies now have +"
                 << state->npc_weapon_damage_bonus
                 << " total weapon damage." << endl;
        }
        pthread_mutex_unlock(&state->state_mutex);

        // Decide action: prefer Strike if any player is alive, else Skip
        int target = pick_random_alive_player(state);

        pthread_mutex_lock(&state->action_mutex);
        state->action_slot.actor_index = my_index;
        if (target >= 0) {
            state->action_slot.action       = ACTION_ATTACK_STRIKE;
            state->action_slot.target_index = target;
            cout << "[ASP] " << state->entities[my_index].name
                 << " strikes " << state->entities[target].name << "." << endl;
        } else {
            state->action_slot.action       = ACTION_SKIP;
            state->action_slot.target_index = -1;
            cout << "[ASP] " << state->entities[my_index].name
                 << " skips (no targets)." << endl;
        }
        state->action_slot.ready = true;
        pthread_mutex_unlock(&state->action_mutex);

        // Wait until Arbiter resets ready flag before looping
        while (state->action_slot.ready &&
               state->game_status == GAME_RUNNING)
            usleep(5000);
    }

    cout << "[ASP] Thread for "
         << state->entities[my_index].name << " exiting." << endl;
    return nullptr;
}

int main() {
    signal(SIGUSR1, handle_sigusr1);
    signal(SIGTERM, handle_sigterm);

    srand((unsigned)time(nullptr) ^ (unsigned)getpid());

    state = attach_shared_memory();
    if (!state) return 1;

    cout << "[ASP] Attached. Spawning "
         << state->npc_count << " NPC thread(s)..." << endl;

    pthread_t      threads[MAX_ENTITIES];
    NpcThreadData  thread_data[MAX_ENTITIES];

    int total_threads = state->npc_count;

    for (int i = 0; i < state->npc_count; i++) {
        thread_data[i].npc_index = state->player_count + i;
        pthread_create(&threads[i], nullptr, npc_thread, &thread_data[i]);
    }

    // Spawn loop runs concurrently with initial NPC threads
    while (running && state->game_status == GAME_RUNNING) {
        usleep(200000);

        pthread_mutex_lock(&state->state_mutex);
        int pending = state->spawn_pending_count;
        if (pending > 0)
            state->spawn_pending_count = 0;
        pthread_mutex_unlock(&state->state_mutex);

        for (int i = 0; i < pending && total_threads < MAX_ENTITIES; i++) {
            thread_data[total_threads].npc_index =
                state->player_count + total_threads;
            pthread_create(&threads[total_threads], nullptr,
                           npc_thread, &thread_data[total_threads]);
            total_threads++;
        }
    }

    // Join all threads — initial and spawned
    for (int i = 0; i < total_threads; i++)
        pthread_join(threads[i], nullptr);

    munmap(state, sizeof(SharedState));
    return 0;
}

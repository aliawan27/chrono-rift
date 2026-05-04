#include <iostream>
#include <unistd.h>
#include <csignal>
#include "shared/shared_memory.h"

using namespace std;

// ASP stub — NPC ki baari pe Skip submit karta hai abhi k liye
static bool         running = true;
static SharedState* state   = nullptr;

void handle_sigterm(int) {
    running = false;
    sem_post(&state->npc_turn_sem);
}

int main() {
    signal(SIGTERM, handle_sigterm);

    state = attach_shared_memory();
    if (!state) return 1;

    cout << "[ASP] Ready. Waiting for turns..." << endl;

    while (running && state->game_status == GAME_RUNNING) {
        sem_wait(&state->npc_turn_sem);
        if (!running || state->game_status != GAME_RUNNING) break;

        pthread_mutex_lock(&state->action_mutex);
        int idx = state->current_turn;
        pthread_mutex_unlock(&state->action_mutex);

        if (idx >= state->player_count) {
            cout << "[ASP] " << state->entities[idx].name
                 << "'s turn — submitting Skip." << endl;

            pthread_mutex_lock(&state->action_mutex);
            state->action_slot.ready        = true;
            state->action_slot.actor_index  = idx;
            state->action_slot.target_index = -1;
            state->action_slot.action       = ACTION_SKIP;
            pthread_mutex_unlock(&state->action_mutex);
        }
    }

    munmap(state, sizeof(SharedState));
    return 0;
}
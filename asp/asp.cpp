#include <iostream>
#include <unistd.h>
#include <csignal>
#include "shared/shared_memory.h"

using namespace std;

// ASP stub — NPC ki baari pe Skip submit karta hai, SIGUSR1 se stun handle karta hai
static bool         running = true;
static SharedState* state   = nullptr;

// Stun signal handler — 3 second pause karo
void handle_sigusr1(int) {
    cout << "[ASP] Stun signal received! Pausing for 3 seconds..." << endl;
    sleep(3);

    // Clear stun flag for the stunned NPC
    if (state) {
        pthread_mutex_lock(&state->state_mutex);
        for (int i = state->player_count; i < state->total_entities; i++) {
            if (state->entities[i].is_stunned) {
                state->entities[i].is_stunned = false;
                cout << "[ASP] " << state->entities[i].name
                     << " stun ended." << endl;
            }
        }
        pthread_mutex_unlock(&state->state_mutex);
    }
}

void handle_sigterm(int) {
    running = false;
    sem_post(&state->npc_turn_sem);
}

int main() {
    signal(SIGUSR1, handle_sigusr1);
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
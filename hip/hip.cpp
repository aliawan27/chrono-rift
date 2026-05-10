#include <iostream>
#include <unistd.h>
#include <csignal>
#include <pthread.h>
#include "shared/shared_memory.h"
#include "hip/input.h"

using namespace std;

// HIP — ek thread per player, sirf active thread input leta hai
static bool         running = true;
static SharedState* state   = nullptr;

struct PlayerThreadData {
    int player_index;
};

void handle_sigterm(int) {
    running = false;
    sem_post(&state->player_turn_sem);
}

// SIGUSR1 handler — only purpose is to interrupt sem_wait() via EINTR.
// The actual stun logic runs in the thread loop using per-entity is_stunned.
void handle_sigusr1_hip(int) {
    // intentionally empty — EINTR on sem_wait is sufficient
}

void* player_thread(void* arg) {
    PlayerThreadData* data     = (PlayerThreadData*)arg;
    int               my_index = data->player_index;

    while (running && state->game_status == GAME_RUNNING) {
        sem_wait(&state->player_turn_sem);
        // Note: sem_wait may return due to EINTR (from SIGUSR1).
        // In that case no token was consumed, so we must NOT sem_post below
        // unless we explicitly decide to pass on the turn.

        if (!running || state->game_status != GAME_RUNNING) {
            sem_post(&state->player_turn_sem);
            break;
        }

        // Check stun BEFORE checking current_turn.
        // The Arbiter sets is_stunned=true on the shared entity before
        // sending SIGUSR1, so this flag is always accurate regardless of
        // which thread caught the signal.
        if (state->entities[my_index].is_stunned) {
            cout << "[HIP] " << state->entities[my_index].name
                 << " is stunned! Waiting 3 seconds..." << endl;
            sleep(3);   // safe: this is normal thread code, not a signal handler

            pthread_mutex_lock(&state->state_mutex);
            state->entities[my_index].is_stunned = false;
            pthread_mutex_unlock(&state->state_mutex);

            // Submit a skip action so the Arbiter can advance the turn
            pthread_mutex_lock(&state->action_mutex);
            state->action_slot.ready        = true;
            state->action_slot.actor_index  = my_index;
            state->action_slot.target_index = -1;
            state->action_slot.action       = ACTION_SKIP;
            pthread_mutex_unlock(&state->action_mutex);
            continue;   // do NOT sem_post — we consumed the token on this stun turn
        }

        pthread_mutex_lock(&state->action_mutex);
        int current = state->current_turn;
        pthread_mutex_unlock(&state->action_mutex);

        if (current != my_index) {
            sem_post(&state->player_turn_sem);
            usleep(10000);
            continue;
        }

        get_player_action(my_index, state);
    }
    return nullptr;
}

int main() {
    signal(SIGUSR1, handle_sigusr1_hip);
    signal(SIGTERM, handle_sigterm);

    state = attach_shared_memory();
    if (!state) return 1;

    cout << "[HIP] Attached. Spawning "
         << state->player_count << " player thread(s)..." << endl;

    pthread_t        threads[MAX_PLAYERS];
    PlayerThreadData thread_data[MAX_PLAYERS];

    for (int i = 0; i < state->player_count; i++) {
        thread_data[i].player_index = i;
        pthread_create(&threads[i], nullptr, player_thread, &thread_data[i]);
    }

    for (int i = 0; i < state->player_count; i++)
        pthread_join(threads[i], nullptr);

    munmap(state, sizeof(SharedState));
    return 0;
}
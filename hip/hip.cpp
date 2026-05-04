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

void* player_thread(void* arg) {
    PlayerThreadData* data     = (PlayerThreadData*)arg;
    int               my_index = data->player_index;

    cout << "[HIP] Thread started for "
         << state->entities[my_index].name << endl;

    while (running && state->game_status == GAME_RUNNING) {
        sem_wait(&state->player_turn_sem);
        if (!running || state->game_status != GAME_RUNNING) {
            sem_post(&state->player_turn_sem);
            break;
        }

        pthread_mutex_lock(&state->action_mutex);
        int current = state->current_turn;
        pthread_mutex_unlock(&state->action_mutex);

        if (current != my_index) {
            sem_post(&state->player_turn_sem);
            usleep(10000);
            continue;
        }

        cout << "[HIP] " << state->entities[my_index].name
             << "'s thread is active." << endl;

        get_player_action(my_index, state);
    }

    cout << "[HIP] Thread for "
         << state->entities[my_index].name << " exiting." << endl;
    return nullptr;
}

int main() {
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
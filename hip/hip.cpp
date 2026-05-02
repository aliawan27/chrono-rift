#include <iostream>
#include <unistd.h>
#include "shared/shared_memory.h"

using namespace std;

// Shared memory se attach ho, Arbiter ka likha hua parho
int main() {
    sleep(1);

    cout << "HIP: Shared memory se attach ho raha hai..." << std::endl;

    SharedState* state = attach_shared_memory();
    if (!state) {
        std::cerr << "HIP: Attach nahi hua." << std::endl;
        return 1;
    }

    pthread_mutex_lock(&state->state_mutex);
    cout << "HIP: player_count = " << state->player_count << std::endl;
    cout << "HIP: npc_count    = " << state->npc_count    << std::endl;
    cout << "HIP: game_status  = " << state->game_status  << std::endl;
    pthread_mutex_unlock(&state->state_mutex);

    munmap(state, sizeof(SharedState));
    return 0;
}
#include <iostream>
#include <unistd.h>
#include "shared/shared_memory.h"

using namespace std;

// Shared memory se attach ho, Arbiter ka likha hua parho
int main() {
    sleep(1);

    cout << "ASP: Shared memory se attach ho raha hai..." << std::endl;

    SharedState* state = attach_shared_memory();
    if (!state) {
        std::cerr << "ASP: Attach nahi hua." << std::endl;
        return 1;
    }

    pthread_mutex_lock(&state->state_mutex);
    cout << "ASP: player_count = " << state->player_count << std::endl;
    cout << "ASP: npc_count    = " << state->npc_count    << std::endl;
    cout << "ASP: game_status  = " << state->game_status  << std::endl;
    pthread_mutex_unlock(&state->state_mutex);

    munmap(state, sizeof(SharedState));
    return 0;
}
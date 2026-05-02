#include <iostream>
#include <unistd.h>
#include "shared/shared_memory.h"

using namespace std;

// Shared memory banao, test data likho, HIP/ASP ko padhne do, phir destroy karo
int main() {
    cout << "Arbiter: Shared memory bana raha hai..." << std::endl;

    SharedState* state = create_shared_memory();
    if (!state) {
        std::cerr << "Arbiter: Shared memory nahi bani." << std::endl;
        return 1;
    }

    pthread_mutex_lock(&state->state_mutex);
    state->player_count   = 2;
    state->npc_count      = 3;
    state->enemies_killed = 0;
    state->game_status    = GAME_RUNNING;
    pthread_mutex_unlock(&state->state_mutex);

    cout << "Arbiter: player_count=2, npc_count=3 likh diya." << std::endl;
    cout << "Arbiter: 3 second ruk raha hai..." << std::endl;

    sleep(3);

    cout << "Arbiter: Shared memory destroy kar raha hai." << std::endl;
    destroy_shared_memory(state);
    return 0;
}
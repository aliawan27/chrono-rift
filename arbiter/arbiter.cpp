#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include "shared/shared_memory.h"
#include "arbiter/init.h"
#include "arbiter/scheduler.h"

using namespace std;

// Arbiter — game ka brain. Sab kuch yahan se start hota hai.
int main() {
    // Player count lo
    int player_count = 0;
    while (player_count < 1 || player_count > 4) {
        cout << "Kitne players hain? (1-4): ";
        cin >> player_count;
    }

    // NPC count randomly decide karo (2-9) using roll number seed
    srand(ALI_ROLL);
    int npc_count = rand() % 8 + 2;
    cout << "[ARBITER] Players: " << player_count
         << " | NPCs: " << npc_count << endl;

    // Shared memory banao
    SharedState* state = create_shared_memory();
    if (!state) return 1;

    // Entities initialize karo
    init_entities(state, player_count, npc_count);

    // HIP process fork karo
    pid_t hip_pid = fork();
    if (hip_pid == 0) {
        execl("./hip_exe", "./hip_exe", nullptr);
        perror("HIP exec failed");
        exit(1);
    }

    // ASP process fork karo
    pid_t asp_pid = fork();
    if (asp_pid == 0) {
        execl("./asp_exe", "./asp_exe", nullptr);
        perror("ASP exec failed");
        exit(1);
    }

    cout << "[ARBITER] HIP PID: " << hip_pid
         << " | ASP PID: " << asp_pid << endl;

    // Thoda wait karo taake HIP aur ASP attach ho jayein
    sleep(1);

    // Scheduling loop chalao
    run_scheduler(state);

    // Game khatam — child processes band karo
    kill(hip_pid, SIGTERM);
    kill(asp_pid, SIGTERM);
    waitpid(hip_pid, nullptr, 0);
    waitpid(asp_pid, nullptr, 0);

    destroy_shared_memory(state);
    return 0;
}
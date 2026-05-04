#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <cstdlib>
#include "shared/shared_memory.h"
#include "arbiter/init.h"
#include "arbiter/scheduler.h"

using namespace std;

// Arbiter — game ka brain, sab kuch yahan se start hota hai
static SharedState* g_state = nullptr;
static pid_t        g_hip_pid = -1;
static pid_t        g_asp_pid = -1;

// SIGTERM handler — player ne quit kiya
void handle_sigterm(int) {
    cout << "\n[ARBITER] SIGTERM received. Shutting down..." << endl;
    if (g_state)
        g_state->game_status = GAME_QUIT;
}

// SIGALRM handler — Ultimate Ability 10 second window khatam
void handle_sigalrm(int) {
    cout << "[ARBITER] Ultimate window ended. Resuming ASP." << endl;
    if (g_asp_pid > 0)
        kill(g_asp_pid, SIGCONT);
}

int main() {
    int player_count = 0;
    while (player_count < 1 || player_count > 4) {
        cout << "How many players? (1-4): ";
        cin >> player_count;
    }

    srand(ALI_ROLL);
    int npc_count = rand() % 8 + 2;
    cout << "[ARBITER] Players: " << player_count
         << " | NPCs: " << npc_count << endl;

    SharedState* state = create_shared_memory();
    if (!state) return 1;
    g_state = state;

    state->arbiter_pid = getpid();

    init_entities(state, player_count, npc_count);

    signal(SIGTERM, handle_sigterm);
    signal(SIGALRM, handle_sigalrm);

    pid_t hip_pid = fork();
    if (hip_pid == 0) {
        execl("./hip_exe", "./hip_exe", nullptr);
        perror("HIP exec failed");
        exit(1);
    }
    g_hip_pid = hip_pid;

    pid_t asp_pid = fork();
    if (asp_pid == 0) {
        execl("./asp_exe", "./asp_exe", nullptr);
        perror("ASP exec failed");
        exit(1);
    }
    g_asp_pid = asp_pid;

    state->hip_pid = hip_pid;
    state->asp_pid = asp_pid;

    cout << "[ARBITER] HIP PID: " << hip_pid
         << " | ASP PID: " << asp_pid << endl;

    sleep(1);

    run_scheduler(state);

    kill(hip_pid, SIGTERM);
    kill(asp_pid, SIGTERM);
    waitpid(hip_pid, nullptr, 0);
    waitpid(asp_pid, nullptr, 0);

    destroy_shared_memory(state);
    return 0;
}
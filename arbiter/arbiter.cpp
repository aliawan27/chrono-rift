#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include "shared/shared_memory.h"
#include "arbiter/init.h"
#include "arbiter/scheduler.h"
#include "shared/artifacts.h"
#include "arbiter/renderer.h"

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
    srand((unsigned)time(nullptr) ^ (unsigned)getpid());

    SharedState* state = create_shared_memory();
    if (!state) return 1;
    g_state = state;

    state->arbiter_pid = getpid();

    signal(SIGTERM, handle_sigterm);
    signal(SIGALRM, handle_sigalrm);

    // Start render thread — it will display welcome screen and get player count
    pthread_t render_th;
    pthread_create(&render_th, nullptr, render_thread, state);

    sleep(1);

    // Wait for player count to be set by render thread
    cout << "[ARBITER] Waiting for player count from render thread..." << endl;
    while (state->player_count == 0 && state->game_status == GAME_RUNNING) {
        usleep(100000);  // 0.1 second
    }

    if (state->player_count < 1 || state->player_count > 4) {
        cout << "[ARBITER] Invalid player count. Quitting." << endl;
        state->game_status = GAME_QUIT;
        pthread_join(render_th, nullptr);
        return 1;
    }

    int player_count = state->player_count;
    // Spec Section 10: "decided randomly on each run, between 2 and 9"
    int npc_count = 2 + (rand() % 8);   // produces 2, 3, 4, 5, 6, 7, 8, or 9
    cout << "[ARBITER] Players: " << player_count
         << " | NPCs: " << npc_count << endl;

    init_entities(state, player_count, npc_count);

    // Fork HIP and ASP only after player/NPC counts and entities are ready.
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

    pthread_t monitor_thread;
    pthread_create(&monitor_thread, nullptr, deadlock_monitor, state);

    run_scheduler(state);

    pthread_cancel(monitor_thread);
    pthread_join(monitor_thread, nullptr);

    pthread_join(render_th, nullptr);

    kill(hip_pid, SIGTERM);
    kill(asp_pid, SIGTERM);
    waitpid(hip_pid, nullptr, 0);
    waitpid(asp_pid, nullptr, 0);

    destroy_shared_memory(state);
    return 0;
}

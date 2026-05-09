#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <cstdlib>
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
    int player_count = prompt_player_count_window();
    if (player_count < 1 || player_count > 4) {
        cout << "[ARBITER] Setup cancelled. Exiting." << endl;
        return 0;
    }

    int npc_min, npc_max;
    if      (player_count == 1) { npc_min = 2; npc_max = 5; }
    else if (player_count == 2) { npc_min = 3; npc_max = 6; }
    else if (player_count == 3) { npc_min = 4; npc_max = 7; }
    else                        { npc_min = 5; npc_max = 9; }
    int npc_count = npc_min + rand() % (npc_max - npc_min + 1);
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

    pthread_t monitor_thread;
    pthread_create(&monitor_thread, nullptr, deadlock_monitor, state);

    pthread_t render_th;
    pthread_create(&render_th, nullptr, render_thread, state);

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
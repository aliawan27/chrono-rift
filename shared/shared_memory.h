#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include "shared/shared_state.h"

// Shared memory banana — sirf Arbiter call karta hai startup pe
inline SharedState* create_shared_memory() {
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open failed");
        return NULL;
    }

    if (ftruncate(fd, sizeof(SharedState)) == -1) {
        perror("ftruncate failed");
        close(fd);
        return NULL;
    }

    SharedState* state = (SharedState*)mmap(
        NULL,
        sizeof(SharedState),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if (state == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return NULL;
    }

    close(fd);
    memset(state, 0, sizeof(SharedState));

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&state->state_mutex,            &mutex_attr);
    pthread_mutex_init(&state->action_mutex,           &mutex_attr);
    pthread_mutex_init(&state->artifacts.table_mutex,  &mutex_attr);

    pthread_mutexattr_destroy(&mutex_attr);

    sem_init(&state->player_turn_sem, 1, 0);
    sem_init(&state->npc_turn_sem,    1, 0);

    state->game_status             = GAME_RUNNING;
    state->enemies_killed          = 0;
    state->action_slot.ready       = false;
    state->pending_drop_weapon_id  = -1;
    state->npc_should_pickup       = false;

    for (int i = 0; i < MAX_ENTITIES; i++) {
        for (int j = 0; j < INVENTORY_SIZE; j++)
            state->entities[i].inventory[j] = -1;
        state->entities[i].lts_count = 0;
    }

    printf("Shared memory created.\n");
    return state;
}

// Pehle se bani shared memory se connect karna — HIP aur ASP call karte hain
inline SharedState* attach_shared_memory() {
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open attach failed");
        return NULL;
    }

    SharedState* state = (SharedState*)mmap(
        NULL,
        sizeof(SharedState),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if (state == MAP_FAILED) {
        perror("mmap attach failed");
        close(fd);
        return NULL;
    }

    close(fd);
    printf("Shared memory attached.\n");
    return state;
}

// Shared memory khatam karna — sirf Arbiter call karta hai end pe
inline void destroy_shared_memory(SharedState* state) {
    if (state == NULL) return;

    pthread_mutex_destroy(&state->state_mutex);
    pthread_mutex_destroy(&state->action_mutex);
    pthread_mutex_destroy(&state->artifacts.table_mutex);
    sem_destroy(&state->player_turn_sem);
    sem_destroy(&state->npc_turn_sem);

    munmap(state, sizeof(SharedState));
    shm_unlink(SHM_NAME);

    printf("Shared memory destroyed.\n");
}

#endif
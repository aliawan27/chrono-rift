#ifndef ARTIFACTS_H
#define ARTIFACTS_H

#include <iostream>
#include <unistd.h>
#include <pthread.h>
#include "shared/shared_state.h"

using namespace std;

// Artifact IDs
#define ARTIFACT_SOLAR_CORE    0
#define ARTIFACT_LUNAR_BLADE   1
#define ARTIFACT_ECLIPSE_RELIC 2

// ── internal helpers: caller must already hold table_mutex ───────────────────

static inline bool*  _artifact_free(ArtifactTable* t, int id) {
    if (id == ARTIFACT_SOLAR_CORE)    return &t->solar_core_free;
    if (id == ARTIFACT_LUNAR_BLADE)   return &t->lunar_blade_free;
    return &t->eclipse_relic_free;
}
static inline int* _artifact_holder(ArtifactTable* t, int id) {
    if (id == ARTIFACT_SOLAR_CORE)    return &t->solar_core_holder;
    if (id == ARTIFACT_LUNAR_BLADE)   return &t->lunar_blade_holder;
    return &t->eclipse_relic_holder;
}
static inline int* _artifact_wanted(ArtifactTable* t, int id) {
    if (id == ARTIFACT_SOLAR_CORE)    return &t->solar_core_wanted_by;
    if (id == ARTIFACT_LUNAR_BLADE)   return &t->lunar_blade_wanted_by;
    return &t->eclipse_relic_wanted_by;
}
static inline const char* artifact_name(int id) {
    if (id == ARTIFACT_SOLAR_CORE)    return "Solar Core";
    if (id == ARTIFACT_LUNAR_BLADE)   return "Lunar Blade";
    if (id == ARTIFACT_ECLIPSE_RELIC) return "Eclipse Relic";
    return "Unknown";
}

// ── public API ────────────────────────────────────────────────────────────────

// 1. Try to acquire artifact_id for entity_idx.
//    Returns true if acquired, false if already held (wanted_by is set).
static inline bool acquire_artifact(SharedState* state, int entity_idx, int artifact_id) {
    ArtifactTable* t = &state->artifacts;
    pthread_mutex_lock(&t->table_mutex);

    bool* free_flag = _artifact_free(t, artifact_id);
    int*  holder    = _artifact_holder(t, artifact_id);
    int*  wanted    = _artifact_wanted(t, artifact_id);

    if (*free_flag) {
        *free_flag = false;
        *holder    = entity_idx;
        *wanted    = -1;
        pthread_mutex_unlock(&t->table_mutex);
        cout << "[ARTIFACT] " << state->entities[entity_idx].name
             << " acquired " << artifact_name(artifact_id) << "." << endl;
        return true;
    }

    *wanted = entity_idx;
    pthread_mutex_unlock(&t->table_mutex);
    cout << "[ARTIFACT] " << state->entities[entity_idx].name
         << " waiting for " << artifact_name(artifact_id) << "." << endl;
    return false;
}

// 2. Release artifact_id held by entity_idx.
static inline void release_artifact(SharedState* state, int entity_idx, int artifact_id) {
    ArtifactTable* t = &state->artifacts;
    pthread_mutex_lock(&t->table_mutex);

    int* holder = _artifact_holder(t, artifact_id);
    if (*holder != entity_idx) {
        pthread_mutex_unlock(&t->table_mutex);
        return;
    }

    *_artifact_free(t, artifact_id)   = true;
    *holder                            = -1;
    *_artifact_wanted(t, artifact_id) = -1;

    pthread_mutex_unlock(&t->table_mutex);
    cout << "[ARTIFACT] " << state->entities[entity_idx].name
         << " released " << artifact_name(artifact_id) << "." << endl;
}

// 3. Check if entity holds both Solar Core AND Lunar Blade in primary inventory.
static inline bool check_ultimate_eligibility(SharedState* state, int entity_idx) {
    Entity* e = &state->entities[entity_idx];
    bool has_solar = false, has_lunar = false;
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        if (e->inventory[i] == WEAPON_SOLAR_CORE)  has_solar = true;
        if (e->inventory[i] == WEAPON_LUNAR_BLADE) has_lunar = true;
    }
    return has_solar && has_lunar;
}

// 4. Spawn the Eclipse Relic into the artifact table.
//    Call when state->enemies_killed == 5.
static inline void spawn_eclipse_relic(SharedState* state) {
    ArtifactTable* t = &state->artifacts;
    pthread_mutex_lock(&t->table_mutex);
    t->eclipse_relic_exists   = true;
    t->eclipse_relic_free     = true;
    t->eclipse_relic_holder   = -1;
    t->eclipse_relic_wanted_by = -1;
    pthread_mutex_unlock(&t->table_mutex);
    cout << "[ARTIFACT] Eclipse Relic has appeared!" << endl;
}

// ── release all artifacts held by a dying entity ─────────────────────────────
// Call this (without holding state_mutex) when an entity dies.
static inline void release_all_artifacts(SharedState* state, int entity_idx) {
    ArtifactTable* t = &state->artifacts;
    pthread_mutex_lock(&t->table_mutex);
    if (t->solar_core_holder   == entity_idx) {
        t->solar_core_free       = true;
        t->solar_core_holder     = -1;
        t->solar_core_wanted_by  = -1;
        cout << "[ARTIFACT] Solar Core released (owner died)." << endl;
    }
    if (t->lunar_blade_holder  == entity_idx) {
        t->lunar_blade_free      = true;
        t->lunar_blade_holder    = -1;
        t->lunar_blade_wanted_by = -1;
        cout << "[ARTIFACT] Lunar Blade released (owner died)." << endl;
    }
    if (t->eclipse_relic_holder == entity_idx) {
        t->eclipse_relic_free      = true;
        t->eclipse_relic_holder    = -1;
        t->eclipse_relic_wanted_by = -1;
        cout << "[ARTIFACT] Eclipse Relic released (owner died)." << endl;
    }
    pthread_mutex_unlock(&t->table_mutex);
}

// ── deadlock monitor thread ───────────────────────────────────────────────────
// Spawned by Arbiter. Detects circular waits every 2 seconds and resolves them.
void* deadlock_monitor(void* arg) {
    SharedState* state = (SharedState*)arg;

    while (state->game_status == GAME_RUNNING) {
        sleep(2);
        if (state->game_status != GAME_RUNNING) break;

        ArtifactTable* t = &state->artifacts;
        pthread_mutex_lock(&t->table_mutex);

        // Gather per-artifact (holder, wanted_by) for the three artifacts
        int holder[3], wanted[3];
        holder[0] = t->solar_core_holder;     wanted[0] = t->solar_core_wanted_by;
        holder[1] = t->lunar_blade_holder;    wanted[1] = t->lunar_blade_wanted_by;
        holder[2] = t->eclipse_relic_holder;  wanted[2] = t->eclipse_relic_wanted_by;

        // Check every pair of distinct artifacts for a circular wait:
        //   Entity A holds X, wants Y  AND  Entity B holds Y, wants X
        int victim = -1, victim_artifact = -1;
        for (int x = 0; x < 3 && victim == -1; x++) {
            for (int y = x + 1; y < 3 && victim == -1; y++) {
                int a = holder[x], b = holder[y];
                if (a < 0 || b < 0 || a == b) continue;
                if (wanted[x] == b && wanted[y] == a) {
                    // Circular wait between a (holds x, wants y)
                    //                  and b (holds y, wants x)
                    cout << "[DEADLOCK] Circular wait detected: "
                         << state->entities[a].name << " <-> "
                         << state->entities[b].name << endl;
                    // Resolution: prefer to evict NPC; if both same type,
                    // evict the higher index (lower priority)
                    bool a_player = state->entities[a].is_player;
                    bool b_player = state->entities[b].is_player;
                    if (!a_player && b_player)      { victim = a; victim_artifact = x; }
                    else if (a_player && !b_player) { victim = b; victim_artifact = y; }
                    else                            { victim = (a > b ? a : b);
                                                      victim_artifact = (a > b ? x : y); }
                }
            }
        }

        pthread_mutex_unlock(&t->table_mutex);

        if (victim >= 0) {
            cout << "[DEADLOCK] Forcing " << state->entities[victim].name
                 << " to release " << artifact_name(victim_artifact) << "." << endl;
            release_artifact(state, victim, victim_artifact);
        }
    }

    cout << "[DEADLOCK MONITOR] Exiting." << endl;
    return nullptr;
}

#endif

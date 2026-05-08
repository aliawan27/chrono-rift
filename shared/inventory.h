#ifndef INVENTORY_H
#define INVENTORY_H

#include <iostream>
#include <cstring>
#include "shared/shared_state.h"

using namespace std;

// Forward declarations
static inline bool allocate_weapon(Entity* e, int weapon_id);
static inline bool evict_and_place(Entity* e, int weapon_id);

// ── helpers ──────────────────────────────────────────────────────────────────

// How many slots does weapon_id need?
static inline int weapon_slots(int weapon_id) {
    if (weapon_id < 0 || weapon_id >= (int)(sizeof(WEAPON_TABLE)/sizeof(WEAPON_TABLE[0])))
        return 0;
    return WEAPON_TABLE[weapon_id].slot_size;
}

// Name string for weapon_id (-1 → "empty")
static inline const char* weapon_name(int weapon_id) {
    if (weapon_id == WEAPON_NONE) return "empty";
    if (weapon_id < 0 || weapon_id >= (int)(sizeof(WEAPON_TABLE)/sizeof(WEAPON_TABLE[0])))
        return "unknown";
    return WEAPON_TABLE[weapon_id].name;
}

// ── core allocator ────────────────────────────────────────────────────────────

// Find the start index of the first contiguous run of `need` free slots.
// Returns -1 if none found.
static inline int find_free_run(Entity* e, int need) {
    int run_start = -1;
    int run_len   = 0;
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        if (e->inventory[i] == -1) {
            if (run_start == -1) run_start = i;
            if (++run_len >= need) return run_start;
        } else {
            run_start = -1;
            run_len   = 0;
        }
    }
    return -1;
}

// Fill slots [start, start+need) with weapon_id.
static inline void fill_slots(Entity* e, int start, int need, int weapon_id) {
    for (int i = start; i < start + need; i++)
        e->inventory[i] = weapon_id;
}

// Remove all occurrences of weapon_id from primary inventory.
static inline void clear_weapon_from_inventory(Entity* e, int weapon_id) {
    for (int i = 0; i < INVENTORY_SIZE; i++)
        if (e->inventory[i] == weapon_id)
            e->inventory[i] = -1;
}

// Push a weapon into long_term_storage (caller guarantees room exists).
static inline void push_to_lts(Entity* e, int weapon_id) {
    if (e->lts_count >= MAX_WEAPONS) return;
    Weapon& w = e->long_term_storage[e->lts_count];
    strncpy(w.name, weapon_name(weapon_id), sizeof(w.name) - 1);
    w.name[sizeof(w.name) - 1] = '\0';
    w.slot_size = weapon_slots(weapon_id);
    w.damage    = WEAPON_TABLE[weapon_id].damage;
    w.occupied  = true;
    e->lts_count++;
}

// ── public API ────────────────────────────────────────────────────────────────

// Find first contiguous run of length `need` in a given int array.
// Returns start index, or -1 if not found.
static inline int find_run_in(const int* arr, int len, int need) {
    int run_start = -1, run_len = 0;
    for (int k = 0; k < len; k++) {
        if (arr[k] == -1) {
            if (run_start == -1) run_start = k;
            if (++run_len >= need) return run_start;
        } else { run_start = -1; run_len = 0; }
    }
    return -1;
}

// 1. Try to place weapon_id in primary inventory.
//    Returns true on success, false if impossible (e.g. both artifacts held).
static inline bool allocate_weapon(Entity* e, int weapon_id) {
    int need = weapon_slots(weapon_id);
    if (need <= 0) return false;

    int start = find_free_run(e, need);
    if (start >= 0) {
        fill_slots(e, start, need, weapon_id);
        return true;
    }

    // No free run — attempt eviction
    return evict_and_place(e, weapon_id);
}

// 2. Evict the minimum set of weapons to make room, then place weapon_id.
static inline bool evict_and_place(Entity* e, int weapon_id) {
    int need = weapon_slots(weapon_id);

    // Collect distinct weapon IDs currently in inventory (excluding target)
    int present[MAX_WEAPONS];
    int present_count = 0;
    for (int i = 0; i < INVENTORY_SIZE; i++) {
        int w = e->inventory[i];
        if (w == -1 || w == weapon_id) continue;
        bool found = false;
        for (int j = 0; j < present_count; j++)
            if (present[j] == w) { found = true; break; }
        if (!found) present[present_count++] = w;
    }

    // Iterate subsets from smallest to largest (fewest evictions first).
    // For up to ~8 distinct weapons a bitmask scan is fine.
    for (int mask = 1; mask < (1 << present_count); mask++) {
        // Simulate: remove selected weapons, check if free run >= need exists
        int sim[INVENTORY_SIZE];
        memcpy(sim, e->inventory, sizeof(int) * INVENTORY_SIZE);

        for (int j = 0; j < present_count; j++) {
            if (!(mask & (1 << j))) continue;
            for (int k = 0; k < INVENTORY_SIZE; k++)
                if (sim[k] == present[j]) sim[k] = -1;
        }

        int run_start = find_run_in(sim, INVENTORY_SIZE, need);
        if (run_start < 0) continue;

        // Commit: move evicted weapons to LTS, apply simulated layout
        for (int j = 0; j < present_count; j++) {
            if (!(mask & (1 << j))) continue;
            if (e->lts_count < MAX_WEAPONS) {
                push_to_lts(e, present[j]);
                clear_weapon_from_inventory(e, present[j]);
            }
        }
        memcpy(e->inventory, sim, sizeof(int) * INVENTORY_SIZE);
        fill_slots(e, run_start, need, weapon_id);
        return true;
    }

    // No combination works (e.g. Solar Core + Lunar Blade both held)
    cout << "[INVENTORY] No room for " << weapon_name(weapon_id)
         << " — eviction impossible." << endl;
    return false;
}

// 3. Swap a weapon from long_term_storage back to primary inventory.
static inline bool swap_in(Entity* e, int lts_index) {
    if (lts_index < 0 || lts_index >= e->lts_count) return false;

    // Identify the weapon by matching its name against WEAPON_TABLE
    int weapon_id = WEAPON_NONE;
    for (int i = 0; i < (int)(sizeof(WEAPON_TABLE)/sizeof(WEAPON_TABLE[0])); i++) {
        if (strncmp(e->long_term_storage[lts_index].name,
                    WEAPON_TABLE[i].name, 31) == 0) {
            weapon_id = i;
            break;
        }
    }
    if (weapon_id == WEAPON_NONE) return false;

    // Remove from LTS first, then try to place
    // Shift entries down
    for (int i = lts_index; i < e->lts_count - 1; i++)
        e->long_term_storage[i] = e->long_term_storage[i + 1];
    e->lts_count--;

    bool ok = allocate_weapon(e, weapon_id);
    if (!ok) {
        // Re-insert at end if placement failed
        if (e->lts_count < MAX_WEAPONS) {
            push_to_lts(e, weapon_id);
        }
    }
    return ok;
}

// 4. Print primary inventory and long term storage.
static inline void print_inventory(Entity* e) {
    cout << "[INVENTORY] " << e->name << " — Primary Inventory:" << endl;
    int i = 0;
    while (i < INVENTORY_SIZE) {
        int w = e->inventory[i];
        if (w == -1) {
            cout << "  [" << i << "] empty" << endl;
            i++;
        } else {
            int start = i;
            while (i < INVENTORY_SIZE && e->inventory[i] == w) i++;
            cout << "  [" << start << "-" << (i-1) << "] "
                 << weapon_name(w)
                 << " (id=" << w << ", dmg="
                 << WEAPON_TABLE[w].damage << ")" << endl;
        }
    }
    cout << "[INVENTORY] Long Term Storage (" << e->lts_count << " items):" << endl;
    for (int j = 0; j < e->lts_count; j++) {
        cout << "  [" << j << "] " << e->long_term_storage[j].name
             << " (dmg=" << e->long_term_storage[j].damage << ")" << endl;
    }
}

#endif

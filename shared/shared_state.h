#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <semaphore.h>
#include <pthread.h>
#include <sys/types.h>

// Saare constants aur structs jo shared memory mein jaate hain

#define MAX_PLAYERS         4
#define MAX_NPCS            9
#define MAX_TOTAL_NPCS      20
#define MAX_ENTITIES        (MAX_PLAYERS + MAX_TOTAL_NPCS)
#define MAX_STAMINA_PLAYER  100
#define MAX_STAMINA_NPC     150
#define INVENTORY_SIZE      20
#define MAX_WEAPONS         10
#define ACTION_LOG_SIZE     8
#define SHM_NAME            "/chrono_rift_shm"

#define ALI_ROLL            2565
#define AHMED_ROLL          644
#define ROLL_LAST_DIGIT     5
#define ROLL_SECOND_LAST    6

// Har weapon ki info
typedef struct {
    char name[32];
    int  slot_size;
    int  damage;
    bool occupied;
} Weapon;

#define WEAPON_NONE           -1
#define WEAPON_SOLAR_CORE      0
#define WEAPON_LUNAR_BLADE     1
#define WEAPON_IRON_HALBERD    2
#define WEAPON_VENOM_DAGGER    3
#define WEAPON_THUNDERSTAFF    4
#define WEAPON_OBSIDIAN_AXE    5
#define WEAPON_FROSTBOW        6
#define WEAPON_SPLINTER_STICK  7

// Game mein available weapons ka master table
struct WeaponDefinition {
    const char* name;
    int slot_size;
    int damage;
};

static const WeaponDefinition WEAPON_TABLE[] = {
    {"Solar Core",      10, 95},
    {"Lunar Blade",     10, 90},
    {"Iron Halberd",     7, 55},
    {"Venom Dagger",     4, 30},
    {"Thunderstaff",     6, 50},
    {"Obsidian Axe",     5, 45},
    {"Frostbow",         6, 48},
    {"Splinter Stick",   2, 12}
};

// Player ya NPC ki sari info
typedef struct {
    char    name[32];
    int     hp;
    int     max_hp;
    int     stamina;
    int     max_stamina;
    int     speed;
    int     damage;
    bool    is_player;
    bool    is_alive;
    bool    is_stunned;
    pid_t   pid;
    int     thread_id;
    int     inventory[INVENTORY_SIZE];
    Weapon  long_term_storage[MAX_WEAPONS];
    int     lts_count;
} Entity;

// Entity k possible actions
typedef enum {
    ACTION_NONE = 0,
    ACTION_ATTACK_STRIKE,
    ACTION_ATTACK_EXHAUST,
    ACTION_USE_WEAPON,
    ACTION_SWAP_IN,
    ACTION_HEAL,
    ACTION_SKIP,
    ACTION_ULTIMATE,
    ACTION_QUIT,
    ACTION_STUN
} ActionType;

// HIP ya ASP ka chosen action Arbiter ko bhejne k liye
typedef struct {
    bool        ready;
    int         actor_index;
    int         target_index;
    ActionType  action;
    int         weapon_id;
} ActionSlot;

// Artifacts kis k paas hain
typedef struct {
    bool    solar_core_free;
    int     solar_core_holder;
    int     solar_core_wanted_by;
    bool    lunar_blade_free;
    int     lunar_blade_holder;
    int     lunar_blade_wanted_by;
    bool    eclipse_relic_exists;
    bool    eclipse_relic_free;
    int     eclipse_relic_holder;
    int     eclipse_relic_wanted_by;
    pthread_mutex_t table_mutex;
} ArtifactTable;

// Game ka overall status
typedef enum {
    GAME_RUNNING = 0,
    GAME_WIN,
    GAME_LOSE,
    GAME_QUIT
} GameStatus;

// Renderer -> HIP input handoff (filled by render thread on key press)
typedef struct {
    bool    pending;        // renderer sets true when input is ready
    int     choice;         // 1-9 action menu choice
    int     target_index;   // absolute entity index (-1 if N/A)
    int     weapon_id;      // weapon id or LTS index (-1 if N/A)
    bool    drop_accept;    // true=Y false=N for drop prompt
    pthread_mutex_t mutex;
} PlayerInput;

// Poora shared memory ka main struct
typedef struct {
    Entity          entities[MAX_ENTITIES];
    int             player_count;
    int             npc_count;
    int             total_entities;
    int             current_turn;
    ActionSlot      action_slot;
    ArtifactTable   artifacts;
    int             enemies_killed;
    GameStatus      game_status;
    pthread_mutex_t state_mutex;
    pthread_mutex_t action_mutex;
    sem_t           player_turn_sem;
    sem_t           npc_turn_sem;
    pid_t           asp_pid;
    pid_t           hip_pid;
    pid_t           arbiter_pid;
    int             stun_target_index;
    int             pending_drop_weapon_id;
    int             last_dropped_weapon;
    int             npc_weapon_id;
    int             npc_weapon_damage_bonus;
    bool            npc_should_pickup;
    int             spawn_pending_count;
    int             total_npcs_spawned;
    char            action_log[ACTION_LOG_SIZE][128];
    int             action_log_head;
    bool            first_kill_done;
    int             last_actor_index;
    bool            spawning_unlocked;
    int             anim_attacker_idx;   // entity index that just acted
    int             anim_target_idx;     // entity index that was hit
    bool            anim_is_kill;        // true if target just died
    bool            anim_pending;        // renderer reads this to trigger anim
    PlayerInput     player_input;
    bool            awaiting_player_input;  // HIP sets true when waiting
    int             awaiting_player_idx;    // which player index is waiting
    bool            awaiting_drop_response; // HIP sets true when drop prompt needed

    // Eclipse Relic pickup prompt
    bool            awaiting_eclipse_response;  // scheduler sets true, renderer clears
    bool            eclipse_accept;             // renderer fills: true=Y false=N
} SharedState;

#endif

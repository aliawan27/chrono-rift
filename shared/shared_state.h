#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <semaphore.h>
#include <pthread.h>
#include <sys/types.h>

// Saare constants aur structs jo shared memory mein jaate hain

#define MAX_PLAYERS         4
#define MAX_NPCS            9
#define MAX_ENTITIES        (MAX_PLAYERS + MAX_NPCS)
#define MAX_STAMINA_PLAYER  100
#define MAX_STAMINA_NPC     150
#define INVENTORY_SIZE      20
#define MAX_WEAPONS         10
#define SHM_NAME            "/chrono_rift_shm"

#define ALI_ROLL    2565
#define AHMED_ROLL  0644

#define ROLL_LAST_DIGIT      5
#define ROLL_SECOND_LAST     6

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
    {"Solar Core",     10, 95},
    {"Lunar Blade",    10, 90},
    {"Iron Halberd",    7, 55},
    {"Venom Dagger",    4, 30},
    {"Thunderstaff",    6, 50},
    {"Obsidian Axe",    5, 45},
    {"Frostbow",        6, 48},
    {"Splinter Stick",  2, 12}
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
    ACTION_QUIT
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
    sem_t           turn_sem;
} SharedState;

#endif
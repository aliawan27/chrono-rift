\# Chrono Rift



A multi-process, multi-threaded tactical RPG built as a CS 2006 Operating Systems project. The game demonstrates core OS concepts including inter-process communication via shared memory, POSIX threads, signals, semaphores, deadlock detection, and real-time scheduling — all wrapped in an SFML graphical interface.



\---



\## Table of Contents



\- \[Project Overview](#project-overview)

\- \[Architecture](#architecture)

\- \[Directory Structure](#directory-structure)

\- \[Dependencies](#dependencies)

\- \[Building the Project](#building-the-project)

\- \[Running the Game](#running-the-game)

\- \[Gameplay](#gameplay)

\- \[OS Concepts Demonstrated](#os-concepts-demonstrated)

\- \[Configuration \& Seeding](#configuration--seeding)

\- \[Team](#team)



\---



\## Project Overview



Chrono Rift is a turn-based combat game where a human player party battles computer-controlled enemies. Combat is driven by a \*\*temporal stamina scheduler\*\*: each entity accumulates stamina over time at a rate equal to its speed. The first entity to reach full stamina earns the right to act, after which its stamina resets to zero and the cycle continues.



The game ends when:

\- All player characters die → \*\*Lose\*\*

\- Players collectively kill 10 enemies → \*\*Win\*\*

\- The player sends a quit signal → \*\*Graceful Exit\*\*



\---



\## Architecture



The game runs as three separate OS processes that communicate exclusively through a POSIX shared memory segment (`/chrono\_rift\_shm`). No pipes are used.



```

┌─────────────────────────────────────┐

│           ARBITER PROCESS           │

│  - Owns shared memory               │

│  - Runs the stamina scheduler       │

│  - Manages deadlock monitor thread  │

│  - Runs SFML render thread          │

│  - Handles SIGTERM / SIGALRM        │

└────────────┬──────────┬─────────────┘

&#x20;            │ shm      │ shm

&#x20;  ┌──────────▼──┐  ┌───▼──────────────┐

&#x20;  │  HIP Process│  │   ASP Process    │

&#x20;  │  (Human     │  │  (Automated      │

&#x20;  │  Interfacing│  │  Strategic       │

&#x20;  │  Process)   │  │  Process)        │

&#x20;  │             │  │                  │

&#x20;  │ 1 thread    │  │ 1 thread per NPC │

&#x20;  │ per player  │  │ (concurrent)     │

&#x20;  └─────────────┘  └──────────────────┘

```



\### Process Responsibilities



| Process | Role |

|---|---|

| \*\*Arbiter\*\* | Owns shared memory, runs scheduler, render thread, deadlock monitor, enforces all rules |

| \*\*HIP\*\* | Captures keyboard input, one `pthread` per player character, writes actions to shared memory |

| \*\*ASP\*\* | Houses one dedicated `pthread` per NPC for independent AI decision-making |



\---



\## Directory Structure



```

chrono-rift/

├── arbiter/

│   ├── arbiter.cpp        # Main Arbiter process entry point

│   ├── init.h             # Entity initialization (seeded stats)

│   ├── scheduler.h        # Stamina-based turn scheduler + deadlock monitor

│   └── renderer.h         # SFML render thread (UI, stamina bars, action log)

├── asp/

│   └── asp.cpp            # Automated Strategic Process (NPC threads)

├── hip/

│   ├── hip.cpp            # Human Interfacing Process (player input threads)

│   └── input.h            # Input reading and action encoding

├── shared/

│   ├── shared\_memory.h    # shm\_open / mmap create \& destroy helpers

│   ├── shared\_state.h     # SharedState struct, Entity, constants, weapon table

│   ├── inventory.h        # Space allocator (contiguous-slot first-fit + LTS swap)

│   └── artifacts.h        # Global artifact table + locking logic

├── assets/

│   ├── backgrounds/       # Battle background images

│   ├── fonts/             # TTF fonts (main, mono, RobotoMono)

│   ├── sprites/           # Player/enemy sprites

│   │   └── weapons/       # Weapon artwork

│   └── ui/                # Solar Core / Lunar Blade icons

├── Dockerfile             # Ubuntu 22.04 image with all build dependencies

├── Makefile               # Builds arbiter\_exe, hip\_exe, asp\_exe

├── requirements.txt       # Extra apt packages installed by Dockerfile

└── chrono\_rift\_report.docx

```



\---



\## Dependencies



All dependencies are provided by the Docker environment (see \[Building the Project](#building-the-project)).



| Library / Tool | Purpose |

|---|---|

| `g++` (C++17) | Compiler |

| `libsfml-dev` | SFML 2.x — graphical UI, window, audio |

| `libpthread` | POSIX threads (bundled with glibc) |

| `librt` | POSIX shared memory (`shm\_open`, `mmap`) |

| `build-essential` | Core build tools (make, gcc, etc.) |

| `cmake` | Optional CMake support |

| `gdb` | Debugging |

| `libsdl2-dev` | SDL2 (available, optional alternative UI) |

| `libglfw3-dev` | GLFW (available, optional alternative UI) |

| `libncurses-dev` | ncurses (available, optional TUI) |



\---



\## Building the Project



\### Option A — Docker (Required for submission)



```bash

\# 1. Build the Docker image

docker build -t chrono-rift .



\# 2. Run an interactive container, mounting your project directory

docker run -it --rm \\

&#x20; -v "$(pwd)":/app \\

&#x20; -e DISPLAY=$DISPLAY \\

&#x20; -v /tmp/.X11-unix:/tmp/.X11-unix \\

&#x20; chrono-rift



\# 3. Inside the container, build all three executables

cd /app

make

```



> \*\*Note:\*\* GUI forwarding requires an X server on your host. On Windows use VcXsrv or WSLg; on macOS use XQuartz and run `xhost +localhost` first.



\### Option B — Native Linux (development only)



```bash

sudo apt-get install build-essential libsfml-dev librt-dev

make

```



\### Makefile Targets



| Target | Description |

|---|---|

| `make` / `make all` | Clean and build all three executables |

| `make arbiter\_exe` | Build only the Arbiter |

| `make hip\_exe` | Build only the HIP |

| `make asp\_exe` | Build only the ASP |

| `make clean` | Remove compiled binaries |



\---



\## Running the Game



The Arbiter process is the entry point. It will `fork` + `exec` HIP and ASP automatically.



```bash

\# From the project root (all three executables must be present)

./arbiter\_exe

```



\*\*Do not launch `hip\_exe` or `asp\_exe` manually.\*\* The Arbiter manages their lifecycle.



To quit mid-game, press the \*\*Quit\*\* button in the UI (sends `SIGTERM` to the Arbiter).



\---



\## Gameplay



\### Setup

On launch you are prompted to choose your party size (1–4 characters). The number of enemies is randomised between 2 and 9 each run.



\### Turn Order

Stamina accumulates every second. Each entity gains `speed` stamina per second. The first to reach their maximum stamina (`100` for players, `150` for enemies) takes their turn, then resets to `0`.



\### Player Actions



| Action | Effect | Stamina After |

|---|---|---|

| \*\*Strike\*\* | Deal damage to selected enemy | → 0 |

| \*\*Exhaust\*\* | Drain enemy stamina by your damage stat | → 0 |

| \*\*Use Weapon\*\* | Attack with an inventory weapon | → 0 |

| \*\*Swap In\*\* | Retrieve a weapon from Long-Term Storage | → 0 (weapon unusable this turn) |

| \*\*Heal\*\* | Restore 10% of max HP | → 0 |

| \*\*Skip\*\* | Pass turn | → 50% |



\### Enemy Actions

Enemies choose between \*\*Strike\*\* (deal damage) and \*\*Skip\*\* (stamina → 50%).



\### Inventory System

Each player has a 20-slot linear inventory. Weapons occupy a fixed number of contiguous slots. If a new weapon does not fit, the space allocator swaps the minimum number of existing weapons to Long-Term Storage (LTS) to make room. Weapons in LTS can be retrieved with the \*\*Swap In\*\* action.



\### Artifacts \& Ultimate Ability

Two exclusive artifacts exist — the \*\*Solar Core\*\* (10 slots, 95 dmg) and the \*\*Lunar Blade\*\* (10 slots, 90 dmg). A player holding both simultaneously may trigger the \*\*Ultimate Ability\*\*, which suspends all NPC activity for 10 seconds (implemented via `SIGSTOP` / `SIGCONT` + `SIGALRM`).



A third artifact, the \*\*Eclipse Relic\*\*, may appear dynamically during the run and joins the global artifact pool under the same locking rules.



\### Stun Mechanic

Certain attacks stun the target for exactly \*\*3 seconds\*\* via a signal. The stunned entity's turn is skipped; it resumes at its previous stamina after recovery.



\---



\## OS Concepts Demonstrated



| Concept | Where |

|---|---|

| `fork` + `exec` | Arbiter spawns HIP and ASP |

| POSIX Shared Memory (`shm\_open`, `mmap`) | All IPC — no pipes used |

| `pthread\_mutex\_t` in shared memory | Protects `SharedState` from race conditions |

| POSIX Threads (`pthread\_create`) | One thread per player (HIP), one per NPC (ASP), render thread, deadlock monitor |

| Signals (`SIGTERM`, `SIGALRM`, `SIGSTOP`, `SIGCONT`, `SIGUSR1`) | Quit, Ultimate Ability timer, Stun mechanic |

| Deadlock Detection | Arbiter background thread checks circular waits in the artifact resource table; forces a release if detected |

| Stamina Scheduler | Arrival-time scheduling analogous to a real-time OS scheduler |

| NPC Turn Timeout | Arbiter treats a 3-second non-response from ASP as "Skip" |



\---



\## Configuration \& Seeding



Stats are seeded with the team's roll numbers, defined in `shared/shared\_state.h`:



```cpp

\#define ALI\_ROLL          2565   // Full roll number  → player HP base

\#define AHMED\_ROLL        644

\#define ROLL\_LAST\_DIGIT   5      // → player damage base (+10)

\#define ROLL\_SECOND\_LAST  6      // → enemy damage base (+10)

```



These constants drive every randomised stat at startup, ensuring reproducible results per the project specification.



\---



\## Team



| Name | Roll Number | Section |

|---|---|---|

| Muhammad Ali | 23i-2565 | BCS-C |

| Ahmed Ijaz | 23i-0644 | BCS-C |



> \*\*Submission format:\*\* `BCS A\_24i\_XXXX\_FullName` as required by the project guidelines.



\---


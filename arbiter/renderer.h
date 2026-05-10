#ifndef RENDERER_H
#define RENDERER_H

#include <SFML/Graphics.hpp>
#include <pthread.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <iostream>
#include <vector>
#include <string>
#include "shared/shared_state.h"

// ======================================================================
// Chrono Rift — SFML render thread
// All SFML usage stays inside this file and is only invoked from the
// pthread spawned by render_thread(). State is read via short-lived
// snapshots taken under state_mutex.
// ======================================================================

// ----------------------------------------------------------------------
// Snapshot structures (rendering reads only from these)
// ----------------------------------------------------------------------
struct EntitySnapshot {
    char name[32];
    int  hp, max_hp;
    int  stamina, max_stamina;
    bool is_player;
    bool is_alive;
    bool is_stunned;
};

struct ArtifactSnapshot {
    bool solar_free;        int solar_holder;        bool solar_wanted;
    bool lunar_free;        int lunar_holder;        bool lunar_wanted;
    bool eclipse_exists;
    bool eclipse_free;      int eclipse_holder;      bool eclipse_wanted;
};

struct GameSnapshot {
    EntitySnapshot   entities[MAX_ENTITIES];
    int              player_count;
    int              total_entities;
    int              current_turn;
    int              enemies_killed;
    GameStatus       game_status;
    ArtifactSnapshot artifacts;
    char             action_log[ACTION_LOG_SIZE][128];
    int              action_log_head;

    int              anim_attacker_idx;
    int              anim_target_idx;
    bool             anim_is_kill;
    bool             anim_pending;
};

// Floating combat-text animation
struct AnimEvent {
    sf::Text     text;
    sf::Vector2f pos;
    float        alpha;
    float        timer;
};

// Turn announcement banner
struct TurnAnnounce {
    std::string name;
    bool        is_player;
    float       timer;     // counts down from 1.0
};

// Sprite-animation states for battle entities
enum SpriteAnim {
    ANIM_IDLE,
    ANIM_ATTACK,
    ANIM_HIT,
    ANIM_DIE,
    ANIM_DEAD
};

// Per-entity visual state (lives on render thread only)
struct EntityVisual {
    SpriteAnim   anim          = ANIM_IDLE;
    float        anim_timer    = 0.f;   // total state duration, counts down
    int          current_frame = 0;     // sprite-sheet frame index
    float        frame_timer   = 0.f;   // counts down to 0; advances frame
    sf::Vector2f battle_pos;             // position in battle field
    bool         was_alive     = true;
};

// Keyboard input state machine for the SFML window
enum InputState {
    INPUT_WELCOME_SCREEN,   // welcome screen showing, waiting for player count
    INPUT_IDLE,
    INPUT_SHOW_MENU,        // waiting for 1-9 action choice
    INPUT_SHOW_TARGETS,     // waiting for target digit
    INPUT_SHOW_WEAPONS,     // waiting for weapon digit from inventory
    INPUT_SHOW_LTS,         // waiting for LTS index digit
    INPUT_DROP_PROMPT       // waiting for Y or N
};

// ----------------------------------------------------------------------
// Render context — lives entirely on the render thread
// ----------------------------------------------------------------------
struct RenderContext {
    sf::RenderWindow window;
    sf::Font         main_font;
    sf::Font         mono_font;
    bool             main_font_ok = false;
    bool             mono_font_ok = false;

    sf::Texture player_tex,  enemy_tex;
    sf::Texture bg_tex,      logo_tex;
    sf::Texture welcome_bg_tex;  // welcome screen background
    sf::Texture solar_tex,   lunar_tex,   eclipse_tex;
    sf::Texture victory_tex, defeat_tex;
    bool player_tex_ok=false, enemy_tex_ok=false;
    bool bg_tex_ok=false,     logo_tex_ok=false;
    bool welcome_bg_tex_ok=false;
    bool solar_tex_ok=false,  lunar_tex_ok=false, eclipse_tex_ok=false;
    bool victory_tex_ok=false,defeat_tex_ok=false;

    // Battle sprite textures (larger than panel sprites)
    sf::Texture player_idle_tex,   player_attack_tex, player_die_tex;
    sf::Texture enemy_idle_tex,    enemy_attack_tex,  enemy_die_tex;
    bool player_idle_ok=false,  player_attack_ok=false, player_die_ok=false;
    bool enemy_idle_ok=false,   enemy_attack_ok=false,  enemy_die_ok=false;

    // Sprite-sheet frame configuration. Frame size is auto-detected from
    // each texture (square frames; see frame_info_for()), so only the
    // playback rate is configurable here.
    float frame_duration = 0.12f;  // seconds per frame (~8 fps)

    // Weapon icons displayed in the bottom pane. Indexed by WEAPON_TABLE
    // entry. Loaded from assets/sprites/weapons/weapon<id+1>.jpg.
    static constexpr int kWeaponCount = 8;
    sf::Texture weapon_tex[kWeaponCount];
    bool        weapon_tex_ok[kWeaponCount] = { false };

    EntityVisual entity_visuals[MAX_ENTITIES];

    std::vector<AnimEvent> anims;
    TurnAnnounce           announce { "", false, 0.f };

    // Change-detection state
    int  prev_log_head      = 0;
    int  prev_current_turn  = -1;
    int  prev_enemies_killed= 0;
    bool first_frame        = true;
    float game_over_timer   = -1.f;  // counts up after game ends

    // Keyboard input state
    InputState  input_state    = INPUT_WELCOME_SCREEN;  // start at welcome
    int         pending_choice = 0;   // action chosen, waiting for target
    // Tracks typed digits for multi-digit target entry (enemies 0-9+)
    std::string digit_buffer;

    // Welcome screen state
    float       welcome_timer  = 0.f;  // counts up; after 2s show player count prompt
    int         welcome_player_count = 0;  // selected player count (1-4)
    bool        welcome_shown_prompt = false;  // true after 2s timer expired

    // Right side panel toggle (Tab to flip).
    //   true  -> action prompt occupies the side panel
    //   false -> enemy info occupies the side panel
    // When no prompt is active the enemy panel is shown regardless.
    bool side_show_prompt = true;

    // Left player info pane toggle (P to show/hide).
    // Starts hidden and only appears when keyboard-triggered.
    bool show_player_panel = false;
};

// ----------------------------------------------------------------------
// Color helpers (hex -> sf::Color)
// ----------------------------------------------------------------------
inline sf::Color rgb(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return sf::Color((sf::Uint8)r, (sf::Uint8)g, (sf::Uint8)b, (sf::Uint8)a);
}

// Forward declarations for later helpers used earlier in the file.
inline void draw_bar(RenderContext& rc, float x, float y, float w, float h,
                     int cur, int max, sf::Color fill);

// ----------------------------------------------------------------------
// Asset loading (graceful fallback)
// ----------------------------------------------------------------------
inline void load_assets(RenderContext& rc) {
    rc.main_font_ok = rc.main_font.loadFromFile("assets/fonts/main.ttf");
    rc.mono_font_ok = rc.mono_font.loadFromFile("assets/fonts/mono.ttf");

    auto load_if_present = [](sf::Texture& tex, const char* path) -> bool {
        return access(path, R_OK) == 0 && tex.loadFromFile(path);
    };

    rc.player_tex_ok  = load_if_present(rc.player_tex, "assets/sprites/player.png") ||
                        load_if_present(rc.player_tex, "assets/sprites/player_idle.png");
    rc.enemy_tex_ok   = load_if_present(rc.enemy_tex, "assets/sprites/enemy.png") ||
                        load_if_present(rc.enemy_tex, "assets/sprites/enemy_idle.png");
    rc.bg_tex_ok      = load_if_present(rc.bg_tex, "assets/backgrounds/battle_bg.png") ||
                        load_if_present(rc.bg_tex, "assets/backgrounds/battle_bg.jpg");
    rc.welcome_bg_tex_ok = load_if_present(rc.welcome_bg_tex, "assets/backgrounds/bg.png");
    rc.logo_tex_ok    = load_if_present(rc.logo_tex, "assets/ui/logo.png");
    rc.solar_tex_ok   = load_if_present(rc.solar_tex, "assets/ui/solar_core_icon.png") ||
                        load_if_present(rc.solar_tex, "assets/ui/solar_core_icon.jpg");
    rc.lunar_tex_ok   = load_if_present(rc.lunar_tex, "assets/ui/lunar_blade_icon.png") ||
                        load_if_present(rc.lunar_tex, "assets/ui/lunar_blade_icon.jpg");
    rc.eclipse_tex_ok = load_if_present(rc.eclipse_tex, "assets/ui/eclipse_relic_icon.png") ||
                        load_if_present(rc.eclipse_tex, "assets/ui/eclipse_relic_icon.jpg");
    rc.victory_tex_ok = load_if_present(rc.victory_tex, "assets/ui/victory.png");
    rc.defeat_tex_ok  = load_if_present(rc.defeat_tex, "assets/ui/defeat.png");

    // Weapon icons for the bottom pane (one PNG per WEAPON_TABLE entry).
    for (int i = 0; i < RenderContext::kWeaponCount; i++) {
        char path[96];
        snprintf(path, sizeof(path),
                 "assets/sprites/weapons/weapon%d.jpg", i + 1);
        rc.weapon_tex_ok[i] = load_if_present(rc.weapon_tex[i], path);
    }

    // Battle-field sprites (idle / attack / die per side)
    rc.player_idle_ok   = load_if_present(rc.player_idle_tex,
                          "assets/sprites/player_idle.png");
    rc.player_attack_ok = load_if_present(rc.player_attack_tex,
                          "assets/sprites/player_attack.png");
    rc.player_die_ok    = load_if_present(rc.player_die_tex,
                          "assets/sprites/player_die.png");
    rc.enemy_idle_ok    = load_if_present(rc.enemy_idle_tex,
                          "assets/sprites/enemy_idle.png");
    rc.enemy_attack_ok  = load_if_present(rc.enemy_attack_tex,
                          "assets/sprites/enemy_attack.png");
    rc.enemy_die_ok     = load_if_present(rc.enemy_die_tex,
                          "assets/sprites/enemy_die.png");

    // Bidirectional fallback within each side: if any one of
    // {idle, attack, die} loaded, fill the missing slots from it. Idle is
    // tried first as the canonical resting pose, then attack, then die.
    auto fill_missing = [](sf::Texture& dst, bool& dst_ok,
                           const sf::Texture& src, bool src_ok) {
        if (!dst_ok && src_ok) { dst = src; dst_ok = true; }
    };

    auto resolve_side = [&](sf::Texture& idle, bool& idle_ok,
                            sf::Texture& atk,  bool& atk_ok,
                            sf::Texture& die,  bool& die_ok)
    {
        // Idle missing? Try attack, then die.
        fill_missing(idle, idle_ok, atk, atk_ok);
        fill_missing(idle, idle_ok, die, die_ok);
        // Attack missing? Try idle, then die.
        fill_missing(atk,  atk_ok,  idle, idle_ok);
        fill_missing(atk,  atk_ok,  die,  die_ok);
        // Die missing? Try idle, then attack.
        fill_missing(die,  die_ok,  idle, idle_ok);
        fill_missing(die,  die_ok,  atk,  atk_ok);
    };

    resolve_side(rc.player_idle_tex,   rc.player_idle_ok,
                 rc.player_attack_tex, rc.player_attack_ok,
                 rc.player_die_tex,    rc.player_die_ok);
    resolve_side(rc.enemy_idle_tex,    rc.enemy_idle_ok,
                 rc.enemy_attack_tex,  rc.enemy_attack_ok,
                 rc.enemy_die_tex,     rc.enemy_die_ok);
}

// Safe text builder — only constructs an sf::Text when the font is OK.
// If font missing, draws a small grey rectangle in the same area instead.
inline void draw_text_safe(RenderContext& rc, const std::string& s,
                           float x, float y, unsigned size,
                           sf::Color color, bool bold = false,
                           bool use_mono = false)
{
    bool font_ok = use_mono ? rc.mono_font_ok : rc.main_font_ok;
    if (font_ok) {
        sf::Text t(s, use_mono ? rc.mono_font : rc.main_font, size);
        t.setFillColor(color);
        if (bold) t.setStyle(sf::Text::Bold);
        t.setPosition(x, y);
        rc.window.draw(t);
    } else {
        sf::RectangleShape r(sf::Vector2f(s.size() * size * 0.55f, size));
        r.setPosition(x, y);
        r.setFillColor(sf::Color(color.r, color.g, color.b, 80));
        rc.window.draw(r);
    }
}

// ----------------------------------------------------------------------
// State snapshot (called under state_mutex briefly)
// ----------------------------------------------------------------------
inline void take_snapshot(SharedState* state, GameSnapshot& snap) {
    pthread_mutex_lock(&state->state_mutex);

    snap.player_count    = state->player_count;
    snap.total_entities  = state->total_entities;
    snap.current_turn    = state->current_turn;
    snap.enemies_killed  = state->enemies_killed;
    snap.game_status     = state->game_status;
    snap.action_log_head = state->action_log_head;

    for (int i = 0; i < state->total_entities && i < MAX_ENTITIES; i++) {
        Entity* e = &state->entities[i];
        EntitySnapshot& s = snap.entities[i];
        strncpy(s.name, e->name, sizeof(s.name));
        s.name[sizeof(s.name) - 1] = '\0';
        s.hp           = e->hp;
        s.max_hp       = e->max_hp;
        s.stamina      = e->stamina;
        s.max_stamina  = e->max_stamina;
        s.is_player    = e->is_player;
        s.is_alive     = e->is_alive;
        s.is_stunned   = e->is_stunned;
    }

    snap.artifacts.solar_free   = state->artifacts.solar_core_free;
    snap.artifacts.solar_holder = state->artifacts.solar_core_holder;
    snap.artifacts.solar_wanted = state->artifacts.solar_core_wanted_by != -1;
    snap.artifacts.lunar_free   = state->artifacts.lunar_blade_free;
    snap.artifacts.lunar_holder = state->artifacts.lunar_blade_holder;
    snap.artifacts.lunar_wanted = state->artifacts.lunar_blade_wanted_by != -1;
    snap.artifacts.eclipse_exists = state->artifacts.eclipse_relic_exists;
    snap.artifacts.eclipse_free   = state->artifacts.eclipse_relic_free;
    snap.artifacts.eclipse_holder = state->artifacts.eclipse_relic_holder;
    snap.artifacts.eclipse_wanted = state->artifacts.eclipse_relic_wanted_by != -1;

    memcpy(snap.action_log, state->action_log, sizeof(snap.action_log));

    snap.anim_attacker_idx = state->anim_attacker_idx;
    snap.anim_target_idx   = state->anim_target_idx;
    snap.anim_is_kill      = state->anim_is_kill;
    snap.anim_pending      = state->anim_pending;
    // Clear the pending flag immediately after reading so each
    // animation triggers exactly once.
    if (state->anim_pending)
        state->anim_pending = false;

    pthread_mutex_unlock(&state->state_mutex);
}

// ----------------------------------------------------------------------
// Header bar
// ----------------------------------------------------------------------
inline void draw_header(RenderContext& rc, const GameSnapshot& snap) {
    sf::RectangleShape bg(sf::Vector2f(1280, 60));
    bg.setFillColor(rgb(0x1a, 0x1a, 0x2e));
    rc.window.draw(bg);

    draw_text_safe(rc, "CHRONO RIFT", 16, 12, 32, rgb(0xff, 0xd7, 0x00), true);

    char buf[64];
    snprintf(buf, sizeof(buf), "Enemies Killed: %d / 10", snap.enemies_killed);
    draw_text_safe(rc, buf, 540, 18, 22, sf::Color::White);

    if (snap.current_turn >= 0 && snap.current_turn < snap.total_entities) {
        const EntitySnapshot& e = snap.entities[snap.current_turn];
        char turn[96];
        snprintf(turn, sizeof(turn), "* %s's Turn", e.name);
        sf::Color c = e.is_player ? rgb(0x00, 0xff, 0xff) : rgb(0xff, 0x8c, 0x00);
        draw_text_safe(rc, turn, 920, 18, 22, c, true);
        draw_bar(rc, 900, 46, 280, 6,
                 e.stamina, e.max_stamina,
                 e.is_player ? rgb(0x34, 0x98, 0xdb)
                             : rgb(0xe6, 0x7e, 0x22));
    }
}

// ----------------------------------------------------------------------
// HP / stamina bar utility
// ----------------------------------------------------------------------
inline void draw_bar(RenderContext& rc, float x, float y, float w, float h,
                     int cur, int max, sf::Color fill)
{
    sf::RectangleShape track(sf::Vector2f(w, h));
    track.setPosition(x, y);
    track.setFillColor(rgb(40, 40, 50));
    rc.window.draw(track);

    float frac = (max > 0) ? (float)cur / (float)max : 0.f;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    sf::RectangleShape f(sf::Vector2f(w * frac, h));
    f.setPosition(x, y);
    f.setFillColor(fill);
    rc.window.draw(f);
}

// ----------------------------------------------------------------------
// Helper: alive enemy with the lowest HP (focus target). -1 if none.
// ----------------------------------------------------------------------
inline int find_lowest_hp_enemy(const GameSnapshot& snap) {
    int lowest_idx = -1;
    int lowest_hp  = INT_MAX;
    for (int i = snap.player_count; i < snap.total_entities; i++) {
        if (snap.entities[i].is_alive &&
            snap.entities[i].hp < lowest_hp) {
            lowest_hp  = snap.entities[i].hp;
            lowest_idx = i;
        }
    }
    return lowest_idx;
}

// ----------------------------------------------------------------------
// Single entity card (used by both panels)
// ----------------------------------------------------------------------
inline void draw_entity_card(RenderContext& rc, const EntitySnapshot& e,
                             int idx, int current_turn,
                             int focus_target,
                             float x, float y, float w, float h,
                             bool is_player_card)
{
    // Card background
    sf::RectangleShape card(sf::Vector2f(w, h));
    card.setPosition(x, y);
    card.setFillColor(rgb(0x14, 0x1f, 0x33));
    rc.window.draw(card);

    bool active = (idx == current_turn) && e.is_alive;
    if (active) {
        sf::RectangleShape border(sf::Vector2f(w, h));
        border.setPosition(x, y);
        border.setOutlineThickness(3.f);
        border.setOutlineColor(rgb(0xff, 0xd7, 0x00));
        border.setFillColor(sf::Color::Transparent);
        rc.window.draw(border);
    }

    // Focus target highlight (lowest HP enemy)
    if (!is_player_card && idx == focus_target && e.is_alive) {
        sf::RectangleShape focus(sf::Vector2f(w, h));
        focus.setPosition(x, y);
        focus.setOutlineThickness(2.f);
        focus.setOutlineColor(rgb(0xff, 0x45, 0x00));  // orange-red
        focus.setFillColor(sf::Color::Transparent);
        rc.window.draw(focus);
        draw_text_safe(rc, "FOCUS", x + w - 70, y + h - 20,
                       12, rgb(0xff, 0x45, 0x00), true);
    }

    // Sprite slot
    float sprite_size = 32.f;
    if (is_player_card && rc.player_tex_ok) {
        sf::Sprite s(rc.player_tex);
        s.setPosition(x + 8, y + 8);
        s.setScale(sprite_size / s.getLocalBounds().width,
                   sprite_size / s.getLocalBounds().height);
        rc.window.draw(s);
    } else if (!is_player_card && rc.enemy_tex_ok) {
        sf::Sprite s(rc.enemy_tex);
        s.setPosition(x + 8, y + 8);
        s.setScale(sprite_size / s.getLocalBounds().width,
                   sprite_size / s.getLocalBounds().height);
        rc.window.draw(s);
    } else {
        sf::RectangleShape r(sf::Vector2f(sprite_size, sprite_size));
        r.setPosition(x + 8, y + 8);
        r.setFillColor(is_player_card ? rgb(0x4a, 0x90, 0xd9)
                                      : rgb(0xc0, 0x39, 0x2b));
        rc.window.draw(r);
        draw_text_safe(rc, is_player_card ? "P" : "E",
                       x + 18, y + 10, 20, sf::Color::White, true);
    }

    // Name
    draw_text_safe(rc, e.name, x + 50, y + 4, 13, sf::Color::White, true);

    // Active badge
    if (active) {
        draw_text_safe(rc, "> ACT", x + w - 46, y + 4, 11,
                       rgb(0xff, 0xd7, 0x00), true);
    }

    // HP bar — always shown
    float bar_y = y + 22;
    draw_bar(rc, x + 50, bar_y, w - 60, 8,
             e.hp, e.max_hp,
             is_player_card ? rgb(0x2e,0xcc,0x71)
                            : rgb(0xe7,0x4c,0x3c));
    char hpbuf[32];
    snprintf(hpbuf, sizeof(hpbuf), "%d/%d", e.hp, e.max_hp);
    draw_text_safe(rc, hpbuf, x + w - 52, y + 18, 11,
                   sf::Color::White);

    // Stamina bar — only shown when card tall enough
    if (h >= 60) {
        draw_bar(rc, x + 50, bar_y + 16, w - 60, 6,
                 e.stamina, e.max_stamina,
                 is_player_card ? rgb(0x34,0x98,0xdb)
                                : rgb(0xe6,0x7e,0x22));
        char stbuf[32];
        snprintf(stbuf, sizeof(stbuf), "ST%d", e.stamina);
        draw_text_safe(rc, stbuf, x + w - 52, y + 34, 10,
                       sf::Color::White);
    }

    // Status tags
    if (e.is_stunned) {
        sf::RectangleShape tint(sf::Vector2f(w, h));
        tint.setPosition(x, y);
        tint.setFillColor(sf::Color(155, 89, 182, 60));
        rc.window.draw(tint);
        draw_text_safe(rc, "[STUNNED]", x + 50, y + 64, 13,
                       rgb(0xe7, 0x4c, 0x3c), true);
    }

    if (!e.is_alive) {
        // Dark overlay — entity dims out, no cross
        sf::RectangleShape dim(sf::Vector2f(w, h));
        dim.setPosition(x, y);
        dim.setFillColor(sf::Color(0, 0, 0, 200));
        rc.window.draw(dim);
        draw_text_safe(rc, "FALLEN", x + w/2 - 28,
                       y + h/2 - 8, 12,
                       rgb(0x88, 0x88, 0x88), true);
    }
}

// ----------------------------------------------------------------------
// Player panel (narrower: 240 wide, height unchanged)
// ----------------------------------------------------------------------
inline void draw_player_panel(RenderContext& rc, const GameSnapshot& snap) {
    float x = 0, y = 60, w = 240, h = 500;
    sf::RectangleShape bg(sf::Vector2f(w, h));
    bg.setPosition(x, y);
    bg.setFillColor(rgb(0x0d, 0x1b, 0x2a));
    rc.window.draw(bg);

    draw_text_safe(rc, "PARTY", x + 85, y + 8, 18,
                   rgb(0xff, 0xd7, 0x00), true);

    float card_h = 63, gap = 6;
    float cy = y + 38;
    for (int i = 0; i < snap.player_count && i < MAX_PLAYERS; i++) {
        draw_entity_card(rc, snap.entities[i], i, snap.current_turn,
                         -1,            // no focus highlight for players
                         x + 8, cy, w - 16, card_h, true);
        cy += card_h + gap;
    }
}

// ----------------------------------------------------------------------
// Enemy panel (narrower: 240 wide at right edge, height unchanged)
// ----------------------------------------------------------------------
inline void draw_enemy_panel(RenderContext& rc, const GameSnapshot& snap) {
    float x = 1040, y = 60, w = 240, h = 500;
    sf::RectangleShape bg(sf::Vector2f(w, h));
    bg.setPosition(x, y);
    bg.setFillColor(rgb(0x1a, 0x0a, 0x0a));
    rc.window.draw(bg);

    draw_text_safe(rc, "ENEMIES", x + 75, y + 8, 18,
                   rgb(0xe7, 0x4c, 0x3c), true);

    int focus = find_lowest_hp_enemy(snap);

    float card_h = 48, gap = 3;
    float cy = y + 38;
    int shown = 0;
    for (int i = snap.player_count;
         i < snap.total_entities && shown < 9; i++) {
        if (!snap.entities[i].is_alive) continue;
        draw_entity_card(rc, snap.entities[i], i, snap.current_turn,
                         focus,
                         x + 8, cy, w - 16, card_h, false);
        cy += card_h + gap;
        shown++;
    }
}

// ----------------------------------------------------------------------
// Battle field background + grid
// ----------------------------------------------------------------------
inline void draw_battle_bg(RenderContext& rc) {
    // Background now fills the whole window between the header (y=60)
    // and the bottom pane (y=650, 70-tall). Side and bottom panes are
    // drawn over this so the bg shows in any uncovered region.
    float x = 0, y = 60, w = 1280, h = 590;

    if (rc.bg_tex_ok) {
        sf::Sprite s(rc.bg_tex);
        s.setPosition(x, y);
        s.setScale(w / s.getLocalBounds().width,
                   h / s.getLocalBounds().height);
        rc.window.draw(s);
    } else {
        // Vertical gradient covering the full bg area
        for (int i = 0; i < 20; i++) {
            float t = i / 19.f;
            sf::Color c(
                (sf::Uint8)(0x0f + (0x1a - 0x0f) * t),
                (sf::Uint8)(0x0f + (0x1a - 0x0f) * t),
                (sf::Uint8)(0x23 + (0x0f - 0x23) * t)
            );
            sf::RectangleShape r(sf::Vector2f(w, h / 20.f + 1));
            r.setPosition(x, y + i * (h / 20.f));
            r.setFillColor(c);
            rc.window.draw(r);
        }
    }
}

// ----------------------------------------------------------------------
// Battle positions: vertical stack on the left for players, 3x3 grid on
// the right for enemies. Updated each frame so newly spawned entities
// get a slot the moment they appear.
// ----------------------------------------------------------------------
inline void update_battle_positions(RenderContext& rc,
                                    const GameSnapshot& snap)
{
    // Player positions — hug the left edge with minimal margin.
    float px = 10.f;
    float py_start = 30.f;
    float py_gap   = 135.f;
    for (int i = 0; i < snap.player_count && i < MAX_PLAYERS; i++) {
        rc.entity_visuals[i].battle_pos =
            sf::Vector2f(px, py_start + i * py_gap);
    }

    // Two-column zigzag — column A (left) and column B (right)
    // Even-index enemies go in column A, odd in column B.
    // Column B is offset 50px lower than column A to create
    // the stagger effect.
    float col_a_x  = 650.f;
    float col_b_x  = 820.f;
    float y_start  = 60.f;
    float y_step   = 80.f;   // vertical gap between same-column entries
    float stagger  = 45.f;   // column B offset downward

    int shown = 0;
    for (int i = snap.player_count;
         i < snap.total_entities && shown < 10; i++) {
        int col = shown % 2;    // 0 = left column, 1 = right column
        int row = shown / 2;
        float ex = (col == 0) ? col_a_x : col_b_x;
        float ey = y_start + row * y_step + (col == 1 ? stagger : 0.f);
        rc.entity_visuals[i].battle_pos = sf::Vector2f(ex, ey);
        shown++;
    }
}

// ----------------------------------------------------------------------
// Sprite-sheet helpers
// ----------------------------------------------------------------------

// Pick the texture matching (anim, side). Returns nullptr if not loaded.
struct AnimTexture { const sf::Texture* tex; bool ok; };

inline AnimTexture get_anim_texture(const RenderContext& rc,
                                    SpriteAnim a, bool is_player)
{
    if (is_player) {
        if (a == ANIM_ATTACK)
            return { &rc.player_attack_tex, rc.player_attack_ok };
        if (a == ANIM_DIE || a == ANIM_HIT)
            return { &rc.player_die_tex,    rc.player_die_ok    };
        return     { &rc.player_idle_tex,   rc.player_idle_ok   };
    }
    if (a == ANIM_ATTACK)
        return { &rc.enemy_attack_tex,  rc.enemy_attack_ok  };
    if (a == ANIM_DIE || a == ANIM_HIT)
        return { &rc.enemy_die_tex,     rc.enemy_die_ok     };
    return     { &rc.enemy_idle_tex,    rc.enemy_idle_ok    };
}

// Per-texture frame layout. Frames are assumed SQUARE — frame edge
// equals the texture's height — and laid out as a horizontal strip.
//   - Single-image PNG (e.g. 128x128)        -> 1 frame, full image.
//   - Horizontal strip   (e.g. 256x64, 4*64) -> 4 frames of 64x64.
// This avoids the previous bug where a fixed sheet_frame_w of 64 sliced
// a single-image 128x128 sprite to only its left quarter.
struct FrameInfo { int w; int h; int count; };

inline FrameInfo frame_info_for(const sf::Texture* tex) {
    FrameInfo fi { 1, 1, 1 };
    if (!tex) return fi;
    sf::Vector2u sz = tex->getSize();
    if (sz.x == 0 || sz.y == 0) return fi;
    fi.h = (int)sz.y;
    fi.w = (int)sz.y;                      // square frame
    fi.count = (int)sz.x / fi.w;
    if (fi.count < 1) fi.count = 1;
    return fi;
}

// Reset frame counters when an entity transitions to a new animation.
inline void start_anim(EntityVisual& ev, SpriteAnim a, float duration,
                       float frame_duration)
{
    ev.anim          = a;
    ev.anim_timer    = duration;
    ev.current_frame = 0;
    ev.frame_timer   = frame_duration;
}

// ----------------------------------------------------------------------
// Animation state machine: read fresh trigger from the snapshot,
// advance state + frame timers, detect respawns to flip dead-back-to-idle.
// ----------------------------------------------------------------------
inline void update_entity_visuals(RenderContext& rc,
                                  const GameSnapshot& snap,
                                  float dt)
{
    // Detect new animation trigger from snapshot
    if (snap.anim_pending && snap.anim_attacker_idx >= 0) {
        int atk = snap.anim_attacker_idx;
        int tgt = snap.anim_target_idx;

        if (atk >= 0 && atk < MAX_ENTITIES)
            start_anim(rc.entity_visuals[atk], ANIM_ATTACK,
                       0.5f, rc.frame_duration);

        if (tgt >= 0 && tgt < MAX_ENTITIES) {
            if (snap.anim_is_kill)
                start_anim(rc.entity_visuals[tgt], ANIM_DIE,
                           1.2f, rc.frame_duration);
            else
                start_anim(rc.entity_visuals[tgt], ANIM_HIT,
                           0.3f, rc.frame_duration);
        }
    }

    // Advance all timers
    for (int i = 0; i < MAX_ENTITIES; i++) {
        EntityVisual& ev = rc.entity_visuals[i];

        // Skip slots that don't correspond to a real entity
        bool is_player = (i < snap.player_count);
        if (i < snap.total_entities)
            is_player = snap.entities[i].is_player;

        // Advance frame timer for the CURRENT animation. Idle freezes on
        // frame 0 — we still slice a single frame in draw_battle_sprites,
        // we just never step it.
        if (ev.anim != ANIM_IDLE) {
            AnimTexture at = get_anim_texture(rc, ev.anim, is_player);
            FrameInfo   fi = frame_info_for(at.tex);
            int n_frames   = fi.count;

            ev.frame_timer -= dt;
            while (ev.frame_timer <= 0.f) {
                ev.frame_timer += rc.frame_duration;
                // One-shot non-idle: clamp at last frame
                if (ev.current_frame < n_frames - 1)
                    ev.current_frame++;
            }
        } else {
            // Keep the frame timer parked so the first non-idle tick
            // gets a full frame_duration before advancing.
            ev.frame_timer = rc.frame_duration;
            ev.current_frame = 0;
        }

        // Advance overall state timer (transitions HIT/ATTACK -> IDLE,
        // DIE -> DEAD)
        if (ev.anim_timer > 0) {
            ev.anim_timer -= dt;
            if (ev.anim_timer <= 0) {
                ev.anim_timer = 0;
                if (ev.anim == ANIM_DIE) {
                    ev.anim = ANIM_DEAD;
                } else {
                    // Return to idle: reset frame counters too
                    start_anim(ev, ANIM_IDLE, 0.f, rc.frame_duration);
                    ev.anim_timer = 0.f;   // idle is ambient, no timeout
                }
            }
        }

        // Detect entity coming back alive (respawned NPC slot)
        if (i < snap.total_entities) {
            bool alive = snap.entities[i].is_alive;
            if (!ev.was_alive && alive) {
                start_anim(ev, ANIM_IDLE, 0.f, rc.frame_duration);
                ev.anim_timer = 0.f;
            }
            ev.was_alive = alive;
        }
    }
}

// ----------------------------------------------------------------------
// Draw all battle-field sprites (players left, enemies right) with
// per-entity animation state, name tag, HP bar, and turn arrow.
// ----------------------------------------------------------------------
inline void draw_battle_sprites(RenderContext& rc,
                                const GameSnapshot& snap)
{
    // Increase player battle sprite size by 50%.
    float sprite_w = 135.f, sprite_h = 135.f;

    for (int i = 0; i < snap.total_entities; i++) {
        const EntitySnapshot& e  = snap.entities[i];
        EntityVisual&         ev = rc.entity_visuals[i];

        if (ev.anim == ANIM_DEAD) continue;

        sf::Vector2f pos = ev.battle_pos;
        bool is_player   = e.is_player;

        AnimTexture at = get_anim_texture(rc, ev.anim, is_player);

        if (at.ok && at.tex) {
            // Auto-detect frame layout from the texture (square frames).
            FrameInfo finfo = frame_info_for(at.tex);
            int fi = ev.current_frame;
            if (fi >= finfo.count) fi = finfo.count - 1;
            if (fi < 0) fi = 0;

            sf::Sprite s(*at.tex);
            s.setTextureRect(sf::IntRect(
                fi * finfo.w, 0,
                finfo.w,       finfo.h));

            // Scale based on the per-FRAME size so multi-frame strips
            // render correctly and single-image sprites are not cropped.
            s.setScale(sprite_w / (float)finfo.w,
                       sprite_h / (float)finfo.h);
            s.setPosition(pos);

            if (i == snap.current_turn && e.is_alive)
                s.setColor(rgb(0xff, 0xff, 0x80));      // active glow
            else if (ev.anim == ANIM_HIT)
                s.setColor(rgb(0xff, 0x80, 0x80));      // red tint on hit
            else
                s.setColor(sf::Color::White);

            // Dying entity fades out
            if (ev.anim == ANIM_DIE) {
                float alpha = (ev.anim_timer / 1.2f) * 255.f;
                if (alpha < 0.f) alpha = 0.f;
                sf::Color c = s.getColor();
                c.a = (sf::Uint8)alpha;
                s.setColor(c);
            }

            rc.window.draw(s);
        } else {
            sf::RectangleShape r(sf::Vector2f(sprite_w, sprite_h));
            r.setPosition(pos);
            r.setFillColor(is_player ? rgb(0x4a, 0x90, 0xd9)
                                     : rgb(0xc0, 0x39, 0x2b));
            rc.window.draw(r);
            draw_text_safe(rc, is_player ? "P" : "E",
                           pos.x + 38, pos.y + 28, 24,
                           sf::Color::White, true);
        }

        if (ev.anim != ANIM_DIE) {
            // Name tag below sprite — generous clearance so glyph ascenders
            // never visually clip into the sprite edge.
            float label_y = is_player
                            ? pos.y + sprite_h + 14
                            : pos.y + sprite_h + 14 - 60.f;
            draw_text_safe(rc, e.name,
                           pos.x - 8, label_y,
                           12, sf::Color::White);

            // HP bar below name, positioned independently to avoid panel overlap.
            // Enemy HP bars positioned higher (closer to sprite).
            float bar_y = is_player ? (pos.y + sprite_h + 8) : (pos.y + sprite_h - 25);
            draw_bar(rc, pos.x - 5, bar_y,
                     sprite_w + 10, 5,
                     e.hp, e.max_hp,
                     is_player ? rgb(0x2e, 0xcc, 0x71)
                               : rgb(0xe7, 0x4c, 0x3c));
        }

        // Active turn arrow above sprite.
        if (i == snap.current_turn && e.is_alive) {
            draw_text_safe(rc, "v", pos.x + 35, pos.y - 24,
                           20, rgb(0xff, 0xd7, 0x00), true);
        }
    }
}

// ----------------------------------------------------------------------
// Battle field — logo (first turn), animations, turn announcement, artifacts
// ----------------------------------------------------------------------
inline void draw_artifacts(RenderContext& rc, const GameSnapshot& snap) {
    // Artifact status row — sits just above the bottom pane (y=650)
    float bx = 240, by = 60, bw = 800, bh = 590;
    float row_y = by + bh - 40;
    float slot_w = bw / 3.f;

    auto draw_one = [&](int idx, const char* name,
                        sf::Texture* tex, bool tex_ok,
                        sf::Color fallback,
                        bool exists, bool free_, int holder, bool wanted)
    {
        float cx = bx + slot_w * idx + slot_w * 0.5f;

        // Icon
        if (tex_ok && tex) {
            sf::Sprite s(*tex);
            s.setPosition(cx - 16, row_y);
            s.setScale(32.f / s.getLocalBounds().width,
                       32.f / s.getLocalBounds().height);
            if (!exists) s.setColor(sf::Color(255,255,255,80));
            rc.window.draw(s);
        } else {
            sf::CircleShape c(16.f);
            c.setPosition(cx - 16, row_y);
            c.setFillColor(exists ? fallback
                                  : sf::Color(fallback.r, fallback.g,
                                              fallback.b, 80));
            rc.window.draw(c);
        }

        draw_text_safe(rc, name, cx - 50, row_y + 38, 14,
                       sf::Color::White, true);

        const char* status_str;
        sf::Color   status_col;
        char hbuf[64] = {0};
        if (!exists) { status_str = "N/A"; status_col = rgb(127,140,141); }
        else if (wanted) { status_str = "WANTED"; status_col = rgb(0xe7,0x4c,0x3c); }
        else if (!free_ && holder >= 0 && holder < snap.total_entities) {
            snprintf(hbuf, sizeof(hbuf), "HELD: %s",
                     snap.entities[holder].name);
            status_str = hbuf;
            status_col = rgb(0xff, 0x8c, 0x00);
        } else { status_str = "FREE"; status_col = rgb(0x2e, 0xcc, 0x71); }

        draw_text_safe(rc, status_str, cx - 60, row_y + 56, 12, status_col);
    };

    draw_one(0, "Solar Core",   &rc.solar_tex,   rc.solar_tex_ok,
             rgb(0xff, 0xd7, 0x00),
             true,
             snap.artifacts.solar_free,
             snap.artifacts.solar_holder,
             snap.artifacts.solar_wanted);
    draw_one(1, "Lunar Blade",  &rc.lunar_tex,   rc.lunar_tex_ok,
             rgb(0xc0, 0xc0, 0xc0),
             true,
             snap.artifacts.lunar_free,
             snap.artifacts.lunar_holder,
             snap.artifacts.lunar_wanted);
    draw_one(2, "Eclipse Relic",&rc.eclipse_tex, rc.eclipse_tex_ok,
             rgb(0x9b, 0x59, 0xb6),
             snap.artifacts.eclipse_exists,
             snap.artifacts.eclipse_free,
             snap.artifacts.eclipse_holder,
             snap.artifacts.eclipse_wanted);
}

inline void draw_logo_card(RenderContext& rc) {
    float bx = 300, by = 60, bw = 680;
    float cx = bx + bw * 0.5f, cy = by + 60;
    if (rc.logo_tex_ok) {
        sf::Sprite s(rc.logo_tex);
        sf::FloatRect lb = s.getLocalBounds();
        float scale = 320.f / lb.width;
        s.setScale(scale, scale);
        s.setPosition(cx - lb.width * scale * 0.5f, cy);
        rc.window.draw(s);
    } else {
        // Glow drop shadow + gold text
        draw_text_safe(rc, "CHRONO RIFT", cx - 158, cy + 2, 48,
                       rgb(60, 40, 0), true);
        draw_text_safe(rc, "CHRONO RIFT", cx - 160, cy, 48,
                       rgb(0xff, 0xd7, 0x00), true);
    }
}

// ----------------------------------------------------------------------
// Combat-text animations
// ----------------------------------------------------------------------
inline void update_anims(RenderContext& rc, float dt) {
    for (auto it = rc.anims.begin(); it != rc.anims.end(); ) {
        it->timer -= dt;
        it->pos.y -= 30.f * dt;
        it->alpha = (it->timer / 1.5f) * 255.f;
        if (it->alpha < 0) it->alpha = 0;
        if (it->timer <= 0) it = rc.anims.erase(it);
        else ++it;
    }
    if (rc.announce.timer > 0) rc.announce.timer -= dt;
}

inline void draw_anims(RenderContext& rc) {
    for (auto& a : rc.anims) {
        if (!rc.main_font_ok) continue;
        sf::Color c = a.text.getFillColor();
        c.a = (sf::Uint8)a.alpha;
        a.text.setFillColor(c);
        a.text.setPosition(a.pos);
        rc.window.draw(a.text);
    }

    if (rc.announce.timer > 0 && rc.main_font_ok) {
        float bx = 300, bw = 680;
        float alpha = (rc.announce.timer / 1.0f) * 255.f;
        if (alpha < 0) alpha = 0;
        sf::Color col = rc.announce.is_player ? rgb(0x00, 0xff, 0xff)
                                              : rgb(0xff, 0x8c, 0x00);
        col.a = (sf::Uint8)alpha;
        std::string s = rc.announce.name + "'s Turn!";
        sf::Text t(s, rc.main_font, 42);
        t.setFillColor(col);
        t.setStyle(sf::Text::Bold);
        sf::FloatRect lb = t.getLocalBounds();
        t.setPosition(bx + bw * 0.5f - lb.width * 0.5f, 300);
        rc.window.draw(t);
    }
}

inline sf::Color color_for_log_line(const std::string& s) {
    auto contains = [&](const char* k){ return s.find(k) != std::string::npos; };
    if (contains("killed") || contains("died"))     return rgb(0xe7, 0x4c, 0x3c);
    if (contains("healed"))                          return rgb(0x2e, 0xcc, 0x71);
    if (contains("stunned"))                         return rgb(0x9b, 0x59, 0xb6);
    if (contains("Ultimate"))                        return rgb(0xff, 0xd7, 0x00);
    if (contains("skipped"))                         return rgb(0x7f, 0x8c, 0x8d);
    return rgb(0xec, 0xf0, 0xf1);
}

inline void spawn_anim_for_log(RenderContext& rc, const std::string& msg) {
    if (!rc.main_font_ok) return;
    sf::Color c = color_for_log_line(msg);

    // Short summary for floating text
    std::string short_text = msg;
    if (short_text.size() > 32) short_text = short_text.substr(0, 32) + "...";

    sf::Text t(short_text, rc.main_font, 18);
    t.setFillColor(c);
    t.setStyle(sf::Text::Bold);

    AnimEvent ev;
    ev.text  = t;
    float bx = 300, bw = 680;
    ev.pos   = sf::Vector2f(bx + bw * 0.5f - 80 + (rc.anims.size() % 4) * 20,
                            350 - (rc.anims.size() % 3) * 20);
    ev.alpha = 255.f;
    ev.timer = 1.5f;
    rc.anims.push_back(ev);
}

// ----------------------------------------------------------------------
// Bottom pane — weapon roster, centered horizontally and vertically
// ----------------------------------------------------------------------
inline void draw_bottom_pane(RenderContext& rc,
                              const GameSnapshot& snap,
                              SharedState* state) {
    const float pane_y = 650.f;
    const float pane_h = 70.f;

    // Background
    sf::RectangleShape bg(sf::Vector2f(1280, pane_h));
    bg.setPosition(0, pane_y);
    bg.setFillColor(rgb(0x10, 0x0a, 0x18));
    rc.window.draw(bg);

    // Top gold border
    sf::RectangleShape border(sf::Vector2f(1280, 2));
    border.setPosition(0, pane_y);
    border.setFillColor(rgb(0xff, 0xd7, 0x00));
    rc.window.draw(border);

    // Vertical divider between halves
    sf::RectangleShape div(sf::Vector2f(2, pane_h));
    div.setPosition(640, pane_y);
    div.setFillColor(rgb(0x44, 0x33, 0x55));
    rc.window.draw(div);

    // ── LEFT: Artifact status ─────────────────────────
    draw_text_safe(rc, "ARTIFACTS", 12, pane_y + 6, 13,
                   rgb(0xff,0xd7,0x00), true);

    struct ArtSlot {
        const char* name;
        sf::Texture* tex;
        bool        tex_ok;
        sf::Color   color;
        bool        exists;
        bool        free_;
        int         holder;
        bool        wanted;
    } slots[3] = {
        { "Solar Core",    &rc.solar_tex,   rc.solar_tex_ok,
          rgb(0xff,0xd7,0x00),
          true,
          snap.artifacts.solar_free,
          snap.artifacts.solar_holder,
          snap.artifacts.solar_wanted },
        { "Lunar Blade",   &rc.lunar_tex,   rc.lunar_tex_ok,
          rgb(0xc0,0xc0,0xc0),
          true,
          snap.artifacts.lunar_free,
          snap.artifacts.lunar_holder,
          snap.artifacts.lunar_wanted },
        { "Eclipse Relic", &rc.eclipse_tex, rc.eclipse_tex_ok,
          rgb(0x9b,0x59,0xb6),
          snap.artifacts.eclipse_exists,
          snap.artifacts.eclipse_free,
          snap.artifacts.eclipse_holder,
          snap.artifacts.eclipse_wanted },
    };

    float slot_x = 110.f;
    for (int i = 0; i < 3; i++) {
        const ArtSlot& s = slots[i];
        sf::Color col = s.exists ? s.color
                                 : sf::Color(s.color.r,
                                             s.color.g,
                                             s.color.b, 60);

        if (s.tex_ok && s.tex) {
            sf::Sprite icon(*s.tex);
            sf::FloatRect b = icon.getLocalBounds();
            if (b.width > 0.f && b.height > 0.f) {
                icon.setScale(24.f / b.width, 24.f / b.height);
                icon.setPosition(slot_x, pane_y + 9);
                if (!s.exists)
                    icon.setColor(sf::Color(255, 255, 255, 60));
                rc.window.draw(icon);
            }
        } else {
            sf::CircleShape c(10.f);
            c.setPosition(slot_x + 2, pane_y + 11);
            c.setFillColor(col);
            rc.window.draw(c);
        }

        draw_text_safe(rc, s.name, slot_x + 32,
                       pane_y + 8, 12, col, true);

        const char* status;
        sf::Color   sc;
        char hbuf[48] = {};
        if (!s.exists)
            { status = "N/A";    sc = rgb(0x55,0x55,0x55); }
        else if (s.wanted)
            { status = "WANTED"; sc = rgb(0xe7,0x4c,0x3c); }
        else if (!s.free_ && s.holder >= 0 &&
                 s.holder < snap.total_entities) {
            snprintf(hbuf, sizeof(hbuf), "HELD: %s",
                     snap.entities[s.holder].name);
            status = hbuf;
            sc     = rgb(0xff,0x8c,0x00);
        } else
            { status = "FREE";   sc = rgb(0x2e,0xcc,0x71); }

        draw_text_safe(rc, status, slot_x + 32,
                       pane_y + 26, 11, sc);
        slot_x += 170.f;
    }

    // ── RIGHT: Active player's equipped weapons ────────
    draw_text_safe(rc, "EQUIPPED", 652, pane_y + 6, 13,
                   rgb(0xff,0xd7,0x00), true);

    int active_p = state->awaiting_player_idx;
    if (active_p < 0 || active_p >= snap.player_count)
        active_p = (snap.current_turn < snap.player_count)
                   ? snap.current_turn : 0;

    int inv[INVENTORY_SIZE];
    pthread_mutex_lock(&state->state_mutex);
    if (active_p >= 0 && active_p < state->total_entities)
        memcpy(inv, state->entities[active_p].inventory,
               sizeof(inv));
    else
        memset(inv, -1, sizeof(inv));
    pthread_mutex_unlock(&state->state_mutex);

    int wids[8]; int wcount = 0;
    for (int s = 0; s < INVENTORY_SIZE && wcount < 8; s++) {
        if (inv[s] == -1) continue;
        bool seen = false;
        for (int j = 0; j < wcount; j++)
            if (wids[j] == inv[s]) { seen = true; break; }
        if (!seen) wids[wcount++] = inv[s];
    }

    if (wcount == 0) {
        draw_text_safe(rc, "(no weapons equipped)",
                       660, pane_y + 26, 13,
                       rgb(0x55,0x55,0x55));
    } else {
        float wx = 660.f;
        const float icon_w = 40.f, icon_h = 40.f;
        const float gap    = 14.f;
        for (int i = 0; i < wcount && wx < 1260; i++) {
            int wid = wids[i];
            float wy = pane_y + (pane_h - icon_h) * 0.5f;

            if (rc.weapon_tex_ok[wid]) {
                sf::Sprite sp(rc.weapon_tex[wid]);
                sf::FloatRect lb = sp.getLocalBounds();
                sp.setScale(icon_w / lb.width, icon_h / lb.height);
                sp.setPosition(wx, wy);
                rc.window.draw(sp);
            } else {
                sf::RectangleShape pill(sf::Vector2f(icon_w + 60, 20));
                pill.setPosition(wx, wy + 10);
                pill.setFillColor(rgb(0x2e, 0x1f, 0x3f));
                pill.setOutlineThickness(1.f);
                pill.setOutlineColor(rgb(0x88,0x66,0xaa));
                rc.window.draw(pill);
                draw_text_safe(rc, WEAPON_TABLE[wid].name,
                               wx + 4, wy + 12, 12,
                               sf::Color::White);
                wx += icon_w + 60 + gap;
                continue;
            }

            draw_text_safe(rc, WEAPON_TABLE[wid].name,
                           wx, wy + icon_h + 2, 11,
                           sf::Color::White);
            wx += icon_w + gap;
        }
    }
}

// ----------------------------------------------------------------------
// Action log bar (no longer drawn — kept for reference)
// ----------------------------------------------------------------------
inline void draw_action_log(RenderContext& rc, const GameSnapshot& snap) {
    float x = 0, y = 720 - 160, w = 1280, h = 160;
    sf::RectangleShape bg(sf::Vector2f(w, h));
    bg.setPosition(x, y);
    bg.setFillColor(rgb(0x0a, 0x0a, 0x0a));
    rc.window.draw(bg);

    sf::RectangleShape top(sf::Vector2f(w, 2));
    top.setPosition(x, y);
    top.setFillColor(rgb(0x33, 0x33, 0x33));
    rc.window.draw(top);

    draw_text_safe(rc, "BATTLE LOG", x + 12, y + 6, 14,
                   rgb(0x7f, 0x8c, 0x8d), true);

    // Lines printed oldest-first, newest-last (head points to next slot)
    float line_y = y + 28;
    for (int i = 0; i < ACTION_LOG_SIZE; i++) {
        int idx = (snap.action_log_head + i) % ACTION_LOG_SIZE;
        const char* line = snap.action_log[idx];
        if (line[0] == '\0') { line_y += 16; continue; }
        sf::Color c = color_for_log_line(line);
        draw_text_safe(rc, line, x + 12, line_y, 14, c, false, true);
        line_y += 16;
    }
}

// ----------------------------------------------------------------------
// Game-over overlays
// ----------------------------------------------------------------------
inline void draw_game_over(RenderContext& rc, const GameSnapshot& snap) {
    if (snap.game_status == GAME_RUNNING) return;

    sf::Color overlay_c;
    const char* title;
    const char* subtitle;
    sf::Texture* tex = nullptr;
    bool tex_ok = false;
    sf::Color title_c;

    if (snap.game_status == GAME_WIN) {
        overlay_c = sf::Color(46, 204, 113, 180);
        title     = "VICTORY!";
        subtitle  = "10 enemies defeated.";
        title_c   = rgb(0xff, 0xd7, 0x00);
        tex       = &rc.victory_tex;
        tex_ok    = rc.victory_tex_ok;
    } else if (snap.game_status == GAME_LOSE) {
        overlay_c = sf::Color(180, 30, 30, 180);
        title     = "DEFEAT";
        subtitle  = "All players have fallen.";
        title_c   = rgb(120, 20, 20);
        tex       = &rc.defeat_tex;
        tex_ok    = rc.defeat_tex_ok;
    } else {
        return;  // GAME_QUIT — no overlay
    }

    sf::RectangleShape ov(sf::Vector2f(1280, 720));
    ov.setFillColor(overlay_c);
    rc.window.draw(ov);

    if (tex_ok && tex) {
        sf::Sprite s(*tex);
        sf::FloatRect lb = s.getLocalBounds();
        float scale = 480.f / lb.width;
        s.setScale(scale, scale);
        s.setPosition(640 - lb.width * scale * 0.5f,
                      200 - lb.height * scale * 0.5f);
        rc.window.draw(s);
    }

    draw_text_safe(rc, title, 540, 290, 64, title_c, true);
    draw_text_safe(rc, subtitle, 520, 380, 24, sf::Color::White);

    if (rc.game_over_timer >= 0) {
        int remaining = 5 - (int)rc.game_over_timer;
        if (remaining < 0) remaining = 0;
        char buf[64];
        snprintf(buf, sizeof(buf), "Game will close in %d seconds...", remaining);
        draw_text_safe(rc, buf, 520, 430, 18, sf::Color::White);
    }
}

// ----------------------------------------------------------------------
// Welcome screen — background with message and player count prompt
// ----------------------------------------------------------------------
inline void draw_welcome_screen(RenderContext& rc) {
    // Draw background
    if (rc.welcome_bg_tex_ok) {
        sf::Sprite bg(rc.welcome_bg_tex);
        sf::Vector2u tex_size = rc.welcome_bg_tex.getSize();
        if (tex_size.x > 0 && tex_size.y > 0) {
            bg.setScale(1280.f / (float)tex_size.x,
                        720.f / (float)tex_size.y);
        }
        rc.window.draw(bg);
    } else {
        // Fallback solid color
        sf::RectangleShape bg(sf::Vector2f(1280, 720));
        bg.setFillColor(rgb(0x1a, 0x1a, 0x2e));
        rc.window.draw(bg);
    }

    auto draw_centered = [&](const std::string& s, float cx, float cy,
                             unsigned size, sf::Color color,
                             bool bold = false) {
        if (rc.main_font_ok) {
            sf::Text t(s, rc.main_font, size);
            t.setFillColor(color);
            if (bold) t.setStyle(sf::Text::Bold);
            sf::FloatRect b = t.getLocalBounds();
            t.setOrigin(b.left + b.width * 0.5f,
                        b.top + b.height * 0.5f);
            t.setPosition(cx, cy);
            rc.window.draw(t);
        } else {
            float w = s.size() * size * 0.55f;
            sf::RectangleShape r(sf::Vector2f(w, (float)size));
            r.setOrigin(w * 0.5f, size * 0.5f);
            r.setPosition(cx, cy);
            r.setFillColor(sf::Color(color.r, color.g, color.b, 80));
            rc.window.draw(r);
        }
    };

    sf::RectangleShape text_patch(sf::Vector2f(760.f, 380.f));
    text_patch.setOrigin(380.f, 190.f);
    text_patch.setPosition(640.f, 390.f);
    text_patch.setFillColor(sf::Color(0, 0, 0, 180));
    text_patch.setOutlineThickness(2.f);
    text_patch.setOutlineColor(sf::Color(255, 255, 255, 45));
    rc.window.draw(text_patch);

    // Welcome title and message
    draw_centered("CHRONO RIFT", 640, 250, 64,
                  rgb(0xff, 0xd7, 0x00), true);
    draw_centered("Time distortion has fractured the timeline.", 640, 325, 20,
                  sf::Color::White, true);
    draw_centered("Unite your team and defeat the temporal anomalies.", 640, 355, 20,
                  sf::Color::White, true);

    // After 2 seconds, show player count prompt
    if (rc.welcome_shown_prompt) {
        draw_centered("How many players? (1-4):", 640, 430, 28,
                      rgb(0x2e, 0xcc, 0x71), true);
        if (rc.welcome_player_count > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "You selected: %d", rc.welcome_player_count);
            draw_centered(buf, 640, 485, 24,
                          rgb(0x2e, 0xcc, 0x71), true);
            draw_centered("Press ENTER to start...", 640, 535, 18,
                          rgb(0x7f, 0x8c, 0x8d), true);
        } else {
            draw_centered("Press 1, 2, 3, or 4", 640, 485, 22,
                          sf::Color::White, true);
        }
    } else {
        // Show timer message
        draw_centered("Get ready...", 640, 460, 20,
                      rgb(0x7f, 0x8c, 0x8d), true);
    }
}

// ----------------------------------------------------------------------
// Player-input overlay — vertical panel pinned to the right side of the
// screen at the same slot the enemy panel uses (x=980, y=60, 300x500).
// Toggle visibility via Tab (handled in the event loop): when the toggle
// is off, the enemy panel is drawn here instead.
// ----------------------------------------------------------------------
inline void draw_input_overlay(RenderContext& rc,
                               const GameSnapshot& snap,
                               SharedState* state)
{
    if (rc.input_state == INPUT_IDLE) return;

    int player_idx = state->awaiting_player_idx;
    if (player_idx < 0 || player_idx >= snap.total_entities) return;

    // Read player inventory under state_mutex briefly
    Entity local_player;
    pthread_mutex_lock(&state->state_mutex);
    local_player = state->entities[player_idx];
    int  drop_id = state->pending_drop_weapon_id;
    pthread_mutex_unlock(&state->state_mutex);
    Entity* player = &local_player;

    // Side panel geometry — matches draw_enemy_panel.
    const float bx = 1040, by = 60, bw = 240, bh = 500;

    // Panel background
    sf::RectangleShape box(sf::Vector2f(bw, bh));
    box.setPosition(bx, by);
    box.setFillColor(sf::Color(8, 12, 28, 235));
    box.setOutlineThickness(2.f);
    box.setOutlineColor(rgb(0xff, 0xd7, 0x00));
    rc.window.draw(box);

    // Title + Tab hint
    draw_text_safe(rc, "ACTION", bx + 10, by + 8, 22,
                   rgb(0xff, 0xd7, 0x00), true);
    draw_text_safe(rc, "[Tab] enemies", bx + bw - 112, by + 14, 13,
                   rgb(0x7f, 0x8c, 0x8d));

    // ── Drop prompt ─────────────────────────────────────
    if (rc.input_state == INPUT_DROP_PROMPT && drop_id >= 0) {
        draw_text_safe(rc, "WEAPON DROPPED!",
                   bx + 10, by + 44, 20,
                       rgb(0xff, 0xd7, 0x00), true);
        char wbuf[64];
        snprintf(wbuf, sizeof(wbuf), "%s",
                 WEAPON_TABLE[drop_id].name);
        draw_text_safe(rc, wbuf, bx + 10, by + 74, 17,
                       sf::Color::White, true);
        char sbuf[64];
        snprintf(sbuf, sizeof(sbuf), "%d dmg  %d slots",
                 WEAPON_TABLE[drop_id].damage,
                 WEAPON_TABLE[drop_id].slot_size);
        draw_text_safe(rc, sbuf, bx + 10, by + 98, 14,
                       rgb(0xd0, 0xd0, 0xd0));
        draw_text_safe(rc, "Y  pick up", bx + 10, by + 142, 17,
                       rgb(0x2e, 0xcc, 0x71));
        draw_text_safe(rc, "N  leave it", bx + 10, by + 166, 17,
                       rgb(0xe7, 0x4c, 0x3c));
        draw_text_safe(rc, "(enemy will grab it)",
                   bx + 10, by + 198, 13, rgb(0x7f, 0x8c, 0x8d));
        return;
    }

    // Player header (current turn)
    char header[64];
    snprintf(header, sizeof(header), "%s",
             snap.entities[player_idx].name);
    draw_text_safe(rc, header, bx + 12, by + 44, 17,
                   rgb(0x00, 0xff, 0xff), true);

    // ── Action menu ─────────────────────────────────────
    if (rc.input_state == INPUT_SHOW_MENU) {
        const char* actions[] = {
            "1 Strike", "2 Exhaust", "3 Heal",
            "4 Skip", "5 Weapon",
            "6 Swap", "7 Stun", "8 Ult", "9 Quit"
        };
        float ty = by + 72;
        // Single vertical list (no horizontal split)
        for (int i = 0; i < 9; i++) {
            draw_text_safe(rc, actions[i], bx + 12, ty, 16,
                           sf::Color::White);
            ty += 22;
        }

        // Inventory hint (compact)
        draw_text_safe(rc, "Weapons:", bx + 12, by + 280, 15,
                       rgb(0xff, 0xd7, 0x00), true);
        float wy = by + 304;
        int si = 0; bool any = false;
        while (si < INVENTORY_SIZE && wy < by + bh - 20) {
            int w = player->inventory[si];
            if (w != -1) {
                char tmp[40];
                snprintf(tmp, sizeof(tmp), "[%d] %s",
                         w, WEAPON_TABLE[w].name);
                draw_text_safe(rc, tmp, bx + 12, wy, 13,
                               sf::Color::White);
                wy += 16; any = true;
                while (si < INVENTORY_SIZE &&
                       player->inventory[si] == w) si++;
            } else si++;
        }
        if (!any) draw_text_safe(rc, "(none)", bx + 12, wy, 13,
                                 rgb(0x7f, 0x8c, 0x8d));
        return;
    }

    // ── Target selection ────────────────────────────────
    if (rc.input_state == INPUT_SHOW_TARGETS) {
        draw_text_safe(rc, "Pick target:", bx + 12, by + 68, 16,
                       rgb(0xff, 0xd7, 0x00), true);
        float ty = by + 96;
        int shown = 0;
        for (int i = snap.player_count;
             i < snap.total_entities && shown < 12 && ty < by + bh - 50; i++) {
            if (!snap.entities[i].is_alive) continue;
            char tbuf[80];
            snprintf(tbuf, sizeof(tbuf),
                     "[%d] %s HP:%d",
                     i - snap.player_count + 1,
                     snap.entities[i].name,
                     snap.entities[i].hp);
            draw_text_safe(rc, tbuf, bx + 12, ty, 13,
                           shown % 2 == 0 ? sf::Color::White
                                          : rgb(0xd0, 0xd0, 0xd0));
            ty += 18;
            shown++;
        }
        if (!rc.digit_buffer.empty()) {
            std::string dbuf = "> " + rc.digit_buffer + "_";
            draw_text_safe(rc, dbuf, bx + 12, by + bh - 48, 18,
                           rgb(0x2e, 0xcc, 0x71), true);
        }
        draw_text_safe(rc, "type num + Enter",
                       bx + 12, by + bh - 24, 13,
                       rgb(0x7f, 0x8c, 0x8d));
        draw_text_safe(rc, "Esc cancel",
                       bx + 12, by + bh - 10, 13,
                       rgb(0x7f, 0x8c, 0x8d));
        return;
    }

    // ── Weapon selection ────────────────────────────────
    if (rc.input_state == INPUT_SHOW_WEAPONS) {
        draw_text_safe(rc, "Pick weapon:", bx + 12, by + 68, 16,
                       rgb(0xff, 0xd7, 0x00), true);
        float ty = by + 96;
        int si = 0;
        while (si < INVENTORY_SIZE && ty < by + bh - 30) {
            int w = player->inventory[si];
            if (w != -1) {
                char wbuf[80];
                snprintf(wbuf, sizeof(wbuf),
                         "[%d] %s  dmg:%d",
                         w, WEAPON_TABLE[w].name,
                         WEAPON_TABLE[w].damage);
                draw_text_safe(rc, wbuf, bx + 12, ty, 13,
                               sf::Color::White);
                ty += 18;
                while (si < INVENTORY_SIZE &&
                       player->inventory[si] == w) si++;
            } else si++;
        }
        draw_text_safe(rc, "press ID, Esc cancel",
                       bx + 12, by + bh - 18, 13,
                       rgb(0x7f, 0x8c, 0x8d));
        return;
    }

    // ── LTS selection ───────────────────────────────────
    if (rc.input_state == INPUT_SHOW_LTS) {
        draw_text_safe(rc, "Storage:", bx + 12, by + 68, 16,
                       rgb(0xff, 0xd7, 0x00), true);
        float ty = by + 96;
        for (int i = 0; i < player->lts_count && ty < by + bh - 30; i++) {
            char wbuf[80];
            snprintf(wbuf, sizeof(wbuf), "[%d] %s dmg:%d",
                     i,
                     player->long_term_storage[i].name,
                     player->long_term_storage[i].damage);
            draw_text_safe(rc, wbuf, bx + 12, ty, 13,
                           sf::Color::White);
            ty += 18;
        }
        draw_text_safe(rc, "press index, Esc cancel",
                       bx + 12, by + bh - 18, 13,
                       rgb(0x7f, 0x8c, 0x8d));
        return;
    }
}

// ----------------------------------------------------------------------
// Change detection — spawns animations from snapshot diffs
// ----------------------------------------------------------------------
inline void detect_changes(RenderContext& rc, const GameSnapshot& snap) {
    // New action log entries: head moved forward
    if (!rc.first_frame && snap.action_log_head != rc.prev_log_head) {
        int prev = rc.prev_log_head;
        int cur  = snap.action_log_head;
        int n = (cur - prev + ACTION_LOG_SIZE) % ACTION_LOG_SIZE;
        if (n == 0) n = ACTION_LOG_SIZE;
        for (int i = 0; i < n; i++) {
            int idx = (prev + i) % ACTION_LOG_SIZE;
            std::string msg = snap.action_log[idx];
            if (!msg.empty()) spawn_anim_for_log(rc, msg);
        }
    }

    // Turn change
    if (!rc.first_frame && snap.current_turn != rc.prev_current_turn &&
        snap.current_turn >= 0 && snap.current_turn < snap.total_entities) {
        const EntitySnapshot& e = snap.entities[snap.current_turn];
        rc.announce.name      = e.name;
        rc.announce.is_player = e.is_player;
        rc.announce.timer     = 1.0f;
    }

    rc.prev_log_head       = snap.action_log_head;
    rc.prev_current_turn   = snap.current_turn;
    rc.prev_enemies_killed = snap.enemies_killed;
    rc.first_frame         = false;
}

// ----------------------------------------------------------------------
// Startup prompt — ask the user for player count via an SFML window.
// Blocks the calling thread until 1-4 is pressed (or window closed).
// Returns the chosen count, or -1 if the window was closed / unavailable.
// Must be called from the main thread BEFORE render_thread() is spawned.
// ----------------------------------------------------------------------
inline int prompt_player_count_window() {
    const char* disp = std::getenv("DISPLAY");
    if (!disp || disp[0] == '\0') {
        std::cerr << "[RENDER] No DISPLAY env var found. "
                     "Defaulting to 1 player (headless mode)." << std::endl;
        return 1;
    }

    sf::RenderWindow win(sf::VideoMode(640, 360), "Chrono Rift - Setup");
    if (!win.isOpen()) {
        std::cerr << "[RENDER] Failed to open setup window. "
                     "Defaulting to 1 player." << std::endl;
        return 1;
    }
    win.setFramerateLimit(60);

    sf::Font  font;
    bool font_ok = font.loadFromFile("assets/fonts/main.ttf");

    auto draw_label = [&](const std::string& s, float x, float y,
                          unsigned size, sf::Color c, bool bold = false) {
        if (font_ok) {
            sf::Text t(s, font, size);
            t.setFillColor(c);
            if (bold) t.setStyle(sf::Text::Bold);
            t.setPosition(x, y);
            win.draw(t);
        } else {
            sf::RectangleShape r(sf::Vector2f(s.size() * size * 0.55f, size));
            r.setPosition(x, y);
            r.setFillColor(sf::Color(c.r, c.g, c.b, 80));
            win.draw(r);
        }
    };

    int chosen = -1;
    while (win.isOpen() && chosen < 0) {
        sf::Event ev;
        while (win.pollEvent(ev)) {
            if (ev.type == sf::Event::Closed) {
                win.close();
                return -1;
            }
            if (ev.type == sf::Event::KeyPressed) {
                int d = -1;
                if (ev.key.code >= sf::Keyboard::Num1 &&
                    ev.key.code <= sf::Keyboard::Num4)
                    d = ev.key.code - sf::Keyboard::Num0;
                else if (ev.key.code >= sf::Keyboard::Numpad1 &&
                         ev.key.code <= sf::Keyboard::Numpad4)
                    d = ev.key.code - sf::Keyboard::Numpad0;
                if (d >= 1 && d <= 4) chosen = d;
            }
        }

        win.clear(rgb(0x0a, 0x0a, 0x14));

        // Title
        draw_label("CHRONO RIFT", 180, 50, 48, rgb(0xff, 0xd7, 0x00), true);
        draw_label("How many players?", 180, 130, 28,
                   rgb(0x00, 0xff, 0xff), true);

        // Options
        const char* opts[] = {
            "Press  1   - 1 player",
            "Press  2   - 2 players",
            "Press  3   - 3 players",
            "Press  4   - 4 players"
        };
        for (int i = 0; i < 4; i++)
            draw_label(opts[i], 200, 190 + i * 30, 20, sf::Color::White);

        draw_label("(Close window to cancel)", 200, 320, 14,
                   rgb(0x7f, 0x8c, 0x8d));

        win.display();
    }

    win.close();
    return chosen;
}

// ----------------------------------------------------------------------
// Render thread entry point
// ----------------------------------------------------------------------
inline void* render_thread(void* arg) {
    SharedState* state = (SharedState*)arg;

    // No X11 display available (e.g. headless Docker / WSL without WSLg).
    // Exit the render thread gracefully so the terminal game still runs.
    const char* disp = std::getenv("DISPLAY");
    if (!disp || disp[0] == '\0') {
        std::cerr << "[RENDER] No DISPLAY env var found. "
                     "Skipping SFML window — game will run in terminal only."
                  << std::endl;
        pthread_mutex_lock(&state->state_mutex);
        if (state->player_count == 0)
            state->player_count = 1;
        pthread_mutex_unlock(&state->state_mutex);
        return nullptr;
    }

    RenderContext rc;
    rc.window.create(sf::VideoMode(1280, 720), "Chrono Rift");
    if (!rc.window.isOpen()) {
        std::cerr << "[RENDER] Failed to open SFML window. "
                     "Game will run in terminal only." << std::endl;
        pthread_mutex_lock(&state->state_mutex);
        if (state->player_count == 0)
            state->player_count = 1;
        pthread_mutex_unlock(&state->state_mutex);
        return nullptr;
    }
    rc.window.setFramerateLimit(60);
    load_assets(rc);

    sf::Clock clock;
    const float frame_dt = 1.f / 60.f;
    float accumulator = 0.f;
    bool game_ended_seen = false;

    while (true) {
        // Drain events to keep window responsive
        sf::Event ev;
        while (rc.window.pollEvent(ev)) {

            if (ev.type == sf::Event::Closed) {
                pthread_mutex_lock(&state->state_mutex);
                if (state->game_status == GAME_RUNNING)
                    state->game_status = GAME_QUIT;
                pthread_mutex_unlock(&state->state_mutex);
            }

            if (ev.type != sf::Event::KeyPressed) continue;

            // -- Welcome screen handling first (before other global hotkeys) ---
            if (rc.input_state == INPUT_WELCOME_SCREEN) {
                if (rc.welcome_shown_prompt && ev.key.code == sf::Keyboard::Return) {
                    if (rc.welcome_player_count >= 1 && rc.welcome_player_count <= 4) {
                        // Signal arbiter that player count is ready
                        pthread_mutex_lock(&state->state_mutex);
                        state->player_count = rc.welcome_player_count;
                        state->spawning_unlocked = (rc.welcome_player_count < 4);
                        pthread_mutex_unlock(&state->state_mutex);
                        // Transition to main game
                        rc.input_state = INPUT_IDLE;
                    }
                } else {
                    int digit = -1;
                    if (ev.key.code >= sf::Keyboard::Num1 && ev.key.code <= sf::Keyboard::Num4)
                        digit = ev.key.code - sf::Keyboard::Num0;
                    else if (ev.key.code >= sf::Keyboard::Numpad1 && ev.key.code <= sf::Keyboard::Numpad4)
                        digit = ev.key.code - sf::Keyboard::Numpad0;
                    if (digit >= 1 && digit <= 4) {
                        rc.welcome_player_count = digit;
                    }
                }
                continue;
            }

            // -- Tab toggles the side panel between prompt/enemies -----
            if (ev.key.code == sf::Keyboard::Tab) {
                rc.side_show_prompt = !rc.side_show_prompt;
                continue;
            }

            // -- P toggles left player info pane ------------------------
            if (ev.key.code == sf::Keyboard::P) {
                rc.show_player_panel = !rc.show_player_panel;
                continue;
            }

            // -- Drop prompt --------------------------------------------
            if (state->awaiting_drop_response) {
                if (ev.key.code == sf::Keyboard::Y) {
                    pthread_mutex_lock(&state->player_input.mutex);
                    state->player_input.drop_accept = true;
                    pthread_mutex_unlock(&state->player_input.mutex);
                    state->awaiting_drop_response = false;
                    rc.input_state = INPUT_IDLE;
                } else if (ev.key.code == sf::Keyboard::N) {
                    pthread_mutex_lock(&state->player_input.mutex);
                    state->player_input.drop_accept = false;
                    pthread_mutex_unlock(&state->player_input.mutex);
                    state->awaiting_drop_response = false;
                    rc.input_state = INPUT_IDLE;
                }
                continue;
            }

            // -- Player turn input --------------------------------------
            if (!state->awaiting_player_input) continue;
            int player_idx = state->awaiting_player_idx;
            if (player_idx < 0 || player_idx >= MAX_ENTITIES) continue;
            Entity* player = &state->entities[player_idx];

            // Helper: key to digit (0-9), returns -1 if not a digit key
            auto key_digit = [](sf::Keyboard::Key k) -> int {
                if (k >= sf::Keyboard::Num0 && k <= sf::Keyboard::Num9)
                    return k - sf::Keyboard::Num0;
                if (k >= sf::Keyboard::Numpad0 && k <= sf::Keyboard::Numpad9)
                    return k - sf::Keyboard::Numpad0;
                return -1;
            };

            int digit = key_digit(ev.key.code);

            // Esc backs out of any sub-state to the main menu
            if (ev.key.code == sf::Keyboard::Escape &&
                (rc.input_state == INPUT_SHOW_TARGETS ||
                 rc.input_state == INPUT_SHOW_WEAPONS ||
                 rc.input_state == INPUT_SHOW_LTS)) {
                rc.input_state    = INPUT_SHOW_MENU;
                rc.pending_choice = 0;
                rc.digit_buffer   = "";
                pthread_mutex_lock(&state->player_input.mutex);
                state->player_input.weapon_id    = -1;
                state->player_input.target_index = -1;
                pthread_mutex_unlock(&state->player_input.mutex);
                continue;
            }

            switch (rc.input_state) {

                case INPUT_IDLE:
                case INPUT_SHOW_MENU:
                    if (digit >= 1 && digit <= 9) {
                        rc.pending_choice  = digit;
                        rc.digit_buffer    = "";

                        // Determine what comes next for this choice
                        if (digit == 1 || digit == 2 || digit == 7) {
                            rc.input_state = INPUT_SHOW_TARGETS;
                        } else if (digit == 5) {
                            // Empty inventory? Skip immediately
                            bool any = false;
                            for (int s = 0; s < INVENTORY_SIZE; s++)
                                if (player->inventory[s] != -1) { any = true; break; }
                            if (!any) {
                                pthread_mutex_lock(&state->player_input.mutex);
                                state->player_input.choice       = 4; // SKIP
                                state->player_input.target_index = -1;
                                state->player_input.weapon_id    = -1;
                                state->player_input.pending      = true;
                                pthread_mutex_unlock(&state->player_input.mutex);
                                rc.input_state = INPUT_IDLE;
                            } else {
                                rc.input_state = INPUT_SHOW_WEAPONS;
                            }
                        } else if (digit == 6) {
                            if (player->lts_count == 0) {
                                // No LTS — submit skip immediately
                                pthread_mutex_lock(&state->player_input.mutex);
                                state->player_input.choice       = 4; // SKIP
                                state->player_input.target_index = -1;
                                state->player_input.weapon_id    = -1;
                                state->player_input.pending      = true;
                                pthread_mutex_unlock(&state->player_input.mutex);
                                rc.input_state = INPUT_IDLE;
                            } else {
                                rc.input_state = INPUT_SHOW_LTS;
                            }
                        } else {
                            // Choices 3,4,8,9 need no further selection
                            pthread_mutex_lock(&state->player_input.mutex);
                            state->player_input.choice       = digit;
                            state->player_input.target_index = -1;
                            state->player_input.weapon_id    = -1;
                            state->player_input.pending      = true;
                            pthread_mutex_unlock(&state->player_input.mutex);
                            rc.input_state = INPUT_IDLE;
                        }
                    }
                    break;

                case INPUT_SHOW_TARGETS: {
                    // Allow multi-digit entry: press Enter/Return to confirm
                    if (digit >= 0) {
                        rc.digit_buffer += std::to_string(digit);
                    } else if (ev.key.code == sf::Keyboard::Enter ||
                               ev.key.code == sf::Keyboard::Return) {
                        if (!rc.digit_buffer.empty()) {
                            int raw = std::stoi(rc.digit_buffer);
                            int actual = state->player_count + raw - 1;
                            if (actual >= state->player_count &&
                                actual < state->total_entities &&
                                state->entities[actual].is_alive) {
                                pthread_mutex_lock(&state->player_input.mutex);
                                state->player_input.choice       = rc.pending_choice;
                                state->player_input.target_index = actual;
                                // weapon_id may have been set in SHOW_WEAPONS step
                                if (rc.pending_choice != 5)
                                    state->player_input.weapon_id = -1;
                                state->player_input.pending      = true;
                                pthread_mutex_unlock(&state->player_input.mutex);
                                rc.input_state  = INPUT_IDLE;
                                rc.digit_buffer = "";
                            } else {
                                rc.digit_buffer = ""; // invalid, clear and retry
                            }
                        }
                    } else if (ev.key.code == sf::Keyboard::BackSpace) {
                        if (!rc.digit_buffer.empty())
                            rc.digit_buffer.pop_back();
                    }
                    break;
                }

                case INPUT_SHOW_WEAPONS: {
                    // Show weapons in primary inventory
                    // Player presses digit matching weapon id shown on screen
                    // Then presses Enter to also pick a target
                    if (digit >= 0 && digit <= 7) {
                        // Check weapon exists in inventory
                        bool found = false;
                        for (int s = 0; s < INVENTORY_SIZE; s++) {
                            if (player->inventory[s] == digit) { found = true; break; }
                        }
                        if (found) {
                            pthread_mutex_lock(&state->player_input.mutex);
                            state->player_input.weapon_id = digit;
                            pthread_mutex_unlock(&state->player_input.mutex);
                            rc.pending_choice = 5;
                            rc.digit_buffer = "";
                            rc.input_state = INPUT_SHOW_TARGETS;
                        }
                    }
                    break;
                }

                case INPUT_SHOW_LTS: {
                    if (digit >= 0 && digit < player->lts_count) {
                        pthread_mutex_lock(&state->player_input.mutex);
                        state->player_input.choice       = 6; // SWAP_IN
                        state->player_input.target_index = -1;
                        state->player_input.weapon_id    = digit;
                        state->player_input.pending      = true;
                        pthread_mutex_unlock(&state->player_input.mutex);
                        rc.input_state = INPUT_IDLE;
                    }
                    break;
                }

                default: break;
            }
        }

        // Set input_state correctly each frame based on awaiting flags.
        // Leave the welcome screen alone until it records the player count.
        if (rc.input_state != INPUT_WELCOME_SCREEN) {
            if (state->awaiting_drop_response)
                rc.input_state = INPUT_DROP_PROMPT;
            else if (state->awaiting_player_input && rc.input_state == INPUT_IDLE)
                rc.input_state = INPUT_SHOW_MENU;
            else if (!state->awaiting_player_input)
                rc.input_state = INPUT_IDLE;
        }

        float dt = clock.restart().asSeconds();
        if (dt > 0.1f) dt = 0.1f;
        accumulator += dt;

        // Welcome screen timer
        if (rc.input_state == INPUT_WELCOME_SCREEN) {
            rc.welcome_timer += dt;
            if (rc.welcome_timer >= 2.0f && !rc.welcome_shown_prompt) {
                rc.welcome_shown_prompt = true;
            }
        }

        // Snapshot state (brief lock)
        GameSnapshot snap = {};
        take_snapshot(state, snap);

        // Detect what changed since last snapshot
        detect_changes(rc, snap);

        // Game-over countdown
        if (snap.game_status != GAME_RUNNING) {
            if (rc.game_over_timer < 0) rc.game_over_timer = 0;
            else                         rc.game_over_timer += dt;
            if (rc.game_over_timer >= 5.f && !game_ended_seen) {
                pthread_mutex_lock(&state->state_mutex);
                if (state->game_status == GAME_RUNNING)
                    state->game_status = GAME_QUIT;
                pthread_mutex_unlock(&state->state_mutex);
                game_ended_seen = true;
            }
        }

        // Update animations on fixed-step accumulator
        while (accumulator >= frame_dt) {
            update_anims(rc, frame_dt);
            accumulator -= frame_dt;
        }

        // Render
        rc.window.clear(rgb(0x0a, 0x0a, 0x14));

        // -- Welcome Screen --
        if (rc.input_state == INPUT_WELCOME_SCREEN) {
            draw_welcome_screen(rc);
            rc.window.display();
            continue;
        }

        // 1. Background
        draw_battle_bg(rc);

        // 2. Update positions and animations
        update_battle_positions(rc, snap);
        update_entity_visuals(rc, snap, dt);

        // 3. Draw battle sprites in center field
        draw_battle_sprites(rc, snap);

        // 4. Floating combat text on top of sprites
        draw_anims(rc);

        // 5. Side panels (stats). The right-hand slot is shared between
        // the enemy panel and the action prompt; Tab toggles which one
        // is visible. When no prompt is active, always show enemies.
        if (rc.show_player_panel)
            draw_player_panel(rc, snap);

        bool prompt_active = (rc.input_state != INPUT_IDLE);
        bool show_prompt   = prompt_active && rc.side_show_prompt;
        if (!show_prompt)
            draw_enemy_panel(rc, snap);

        // 6. Header bar
        draw_header(rc, snap);

        // 7. Action menu (right-side panel slot when toggle is on)
        if (show_prompt)
            draw_input_overlay(rc, snap, state);

        // 8. Bottom pane — merged artifacts and weapons
        draw_bottom_pane(rc, snap, state);

        // 9. Game over overlay (on top of everything)
        draw_game_over(rc, snap);

        rc.window.display();

        // Exit conditions
        if (!rc.window.isOpen()) break;
        if (snap.game_status != GAME_RUNNING && rc.game_over_timer >= 5.f)
            break;
    }

    rc.window.close();
    return nullptr;
}

#endif

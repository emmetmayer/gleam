#pragma once

// Simple Test Game
// Brings together major engine systems to make a very simple "game."

typedef struct physics_sandbox_t physics_sandbox_t;

typedef struct fs_t fs_t;
typedef struct heap_t heap_t;
typedef struct render_t render_t;
typedef struct wm_window_t wm_window_t;

// Create an instance of simple test game.
physics_sandbox_t* physics_sandbox_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render, int argc, const char** argv);

// Destroy an instance of simple test game.
void physics_sandbox_destroy(physics_sandbox_t* game);

// Per-frame update for our simple test game.
void physics_sandbox_update(physics_sandbox_t* game);
#pragma once

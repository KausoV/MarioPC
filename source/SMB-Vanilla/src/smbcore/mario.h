#ifndef MARIO_H
#define MARIO_H

#include "base_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_SMB1 0
#define GAME_SMB2J 1

struct SMB_buttons {
  bool u : 1;
  bool d : 1;
  bool l : 1;
  bool r : 1;
  bool a : 1;
  bool b : 1;
  bool select : 1;
  bool start : 1;
};

#define TILE_TYPE_SPRITE 0
#define TILE_TYPE_BG 1
// Absolute world tile coordinates in extra_bg, for optional extended view only.
#define TILE_TYPE_BG_WORLD 2

struct SMB_tile {
  int tileidx;
  int paletteidx;
  bool flip_horz;
  bool flip_vert;
  int x;
  int y;

  u8 extra_type;
  union {
  u8 extra_spriteidx;
  struct {
    u16 x;
    u16 y;
  } extra_bg;
  };
};

struct SMB_callbacks {
  void *userdata;
  bool (*read_rom_bytes)(void *userdata, u8 *buf, size_t size);
  bool (*seek_rom)(void *userdata, size_t offset);
  u8 (*smb2j_load_games_beaten)(void *userdata);
  bool (*smb2j_save_games_beaten)(void *userdata, u8 games_beaten);

  void (*update_pattern_tables)(void *userdata, const u8 *chrrom);
  void (*update_palette)(void *userdata, const u8 *palette_indices);
  void (*draw_tile)(void *userdata, const struct SMB_tile tile);

  void (*apu_write_register)(void *userdata, u16 addr, u8 data);
  void (*apu_end_frame)(void *userdata);

  void (*joy1)(void *userdata, struct SMB_buttons *buttons);
  void (*joy2)(void *userdata, struct SMB_buttons *buttons);
};

struct SMB_state;

size_t SMB_state_size(void);

// Portable, versioned SMB1 snapshots. The state must already have been initialized
// with the same ROM. Callbacks and native addresses are never serialized.
// Import validates the entire snapshot before changing any state or invoking a
// callback, then refreshes the existing renderer's pattern tables and palette.
size_t SMB_snapshot_size(void);
bool SMB_snapshot_export(const struct SMB_state *state, void *bytes, size_t size);
bool SMB_snapshot_import(struct SMB_state *state, const void *bytes, size_t size);
bool SMB_state_init(struct SMB_state *state, const struct SMB_callbacks *cb);
int SMB_which_game(const struct SMB_state *state);
void SMB_start_on_level(struct SMB_state *state, u8 world, u8 level);
// Bidirectional camera extension; disabled by default. History is exported
// separately so existing native snapshot bytes and compatibility stay unchanged.
void SMB_enable_backtracking(struct SMB_state *state, bool enabled);
size_t SMB_backtracking_size(const struct SMB_state *state); // NULL is accepted
bool SMB_backtracking_validate(const void *bytes, size_t size);
bool SMB_backtracking_matches_ram(const void *bytes, size_t size, const u8 *incoming_ram);
bool SMB_backtracking_export(const struct SMB_state *state, void *bytes, size_t size);
bool SMB_backtracking_import(struct SMB_state *state, const void *bytes, size_t size);
// PC wide view: world pixels at the original scale, never stretched. Default 256.
void SMB_set_viewport_width(struct SMB_state *state, int width);
int SMB_viewport_width(const struct SMB_state *state);
int SMB_viewport_camera_x(const struct SMB_state *state);
// Read-only inputs of the currently rendered frame (first 0x800 RAM bytes,
// including OAM and player animation flags). A legacy import uses native inputs
// until its first tick; its saved presentation image remains authoritative.
const u8 *SMB_presentation_ram(const struct SMB_state *state);
bool SMB_viewport_cell(const struct SMB_state *state, unsigned column, unsigned row,
                       u8 tiles[4], u8 *palette, u8 *collision);
// Redraw the latched visual frame without advancing gameplay or changing native RAM.
void SMB_redraw(struct SMB_state *state);
void SMB_tick(struct SMB_state *state);
u8 *SMB_ram(struct SMB_state *state);
u8 *SMB_ppuram(struct SMB_state *state);

// Syncs up internal data structures to match the contents of RAM.
// Call this after updating the RAM via SMB_ram().
void SMB_ram_finishwrite(struct SMB_state *state);

#ifdef __cplusplus
}
#endif

#endif

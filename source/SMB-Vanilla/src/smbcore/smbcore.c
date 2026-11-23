// The start of something magical! ✨

#include "mario.h"
#include "base.h"
#include "interface.h"
#include "vars.h"
#include "backtrack.h"
#include "wide_view.h"
#include <stdlib.h>

#include <string.h>
#include <stdint.h>

void smb1_Reset(void);
void smb1_NMI(void);

bool smb2j_load_file(struct SMB_state *state, const char *name);
void smb2j_Reset(void);
void smb2j_NMI(void);

void smb1_sync_data(void);
void smb2j_sync_data(void);

void smb1_set_world_and_level(u8 world, u8 level);
void smb2j_set_world_and_level(u8 world, u8 level);


struct sprite {
  struct SMB_tile tile;
  bool draw_behind;
};

static inline void transfer_sprite_data(struct sprite sprites[64], const u8 *data) {
  for (int i = 0; i < 64; i++) {
    sprites[i].tile.y = data[i * 4 + 0] + 1;
    sprites[i].tile.tileidx = data[i * 4 + 1];
    u8 attr = data[i * 4 + 2];
    sprites[i].tile.x = data[i * 4 + 3];
    if (SMB_STATE->viewport.latched_valid[i]) {
      const i32 origin = SMB_STATE->viewport.latched_origins[i];
      sprites[i].tile.x = (int)(origin + (i8)(data[i * 4 + 3] - origin));
    }

    sprites[i].tile.paletteidx = (attr & 0x03) + 4;
    sprites[i].tile.flip_horz = (attr & 0x40) != 0;
    sprites[i].tile.flip_vert = (attr & 0x80) != 0;

    sprites[i].tile.extra_type = TILE_TYPE_SPRITE;
    sprites[i].tile.extra_spriteidx = i;

    sprites[i].draw_behind = (attr & 0x20) != 0;
  }
}

static void draw_graphics(const struct sprite sprites[64]);
static void backtrack_before_tick(struct SMB_state *state);
static void backtrack_restore_ppu(struct SMB_state *state);
static void viewport_prepare(struct SMB_state *state);
static void viewport_seed_sprites(struct SMB_state *state);
void smb1_AreaParserTaskHandler(void);

#ifdef THREAD_LOCAL_SMBSTATE
thread_local struct SMB_state *SMB_STATE;
#else
struct SMB_state *SMB_STATE;
#endif

static bool load_smb1(struct SMB_state *state, size_t prg_offset, size_t chr_offset) {
  if (!seek_rom(state, prg_offset)) { return false; }
  if (!read_rom_bytes(state, state->rammem + 0x8000, 0x8000)) { return false; }
  if (!seek_rom(state, chr_offset)) { return false; }
  if (!read_rom_bytes(state, state->chrrom, 0x2000)) { return false; }
  state->which_game = GAME_SMB1;
  update_pattern_tables(state);
  return true;
}


struct fds_file {
  const char *name;
  size_t file_offset;
  u16 size;
  u16 org;
  int type;
};

#define TYPE_PRGRAM 0
#define TYPE_CHRRAM 1

#define SMB2J_FDS_FILES_COUNT 6

// hard-code some offsets for now
// the offsets are relative to after the 16-byte FDS header
static struct fds_file SMB2J_FDS_FILES[SMB2J_FDS_FILES_COUNT] = {
  {"SM2CHAR1", 0x013C, 0x2000, 0x0000, TYPE_CHRRAM},
  {"SM2CHAR2", 0x214D, 0x0040, 0x0760, TYPE_CHRRAM},
  {"SM2MAIN ", 0x219E, 0x8000, 0x6000, TYPE_PRGRAM},
  {"SM2DATA2", 0xA1AF, 0x0E2F, 0xC470, TYPE_PRGRAM},
  {"SM2DATA3", 0xAFEF, 0x0CCF, 0xC5D0, TYPE_PRGRAM},
  {"SM2DATA4", 0xBCCF, 0x0F4C, 0xC2B4, TYPE_PRGRAM},
};

bool smb2j_load_file(struct SMB_state *state, const char *name) {
  if (strncmp(name, "SM2SAVE ", 8) == 0) {
    // treat the save byte specially

    state->rammem[0xD29F] = smb2j_load_games_beaten();

    return true;
  }

  for (int i = 0; i < SMB2J_FDS_FILES_COUNT; i++) {
    const struct fds_file *a = &SMB2J_FDS_FILES[i];
    bool eq = strncmp(name, a->name, 8) == 0;
    if (eq) {
      // Found it!
      u8 *copy_to;
      if (a->type == TYPE_CHRRAM) {
        // Copy the bytes over to CHRRAM
        copy_to = state->chrrom + a->org;
      } else if (a->type == TYPE_PRGRAM) {
        // Copy the bytes over to RAM
        copy_to = state->rammem + a->org;
      }
      if (!seek_rom(state, state->smb2j_disk_offset + a->file_offset)) { return false; }
      if (!read_rom_bytes(state, copy_to, a->size)) { return false; }

      if (a->type == TYPE_CHRRAM) {
        update_pattern_tables(state);
      }

      return true;
    }
  }
  return false;
}

bool load_smb2j(struct SMB_state *state, size_t disk_offset) {
  // In this case, the 16 bit header is this:
  static char fds_disk_verification[16] = {0x01, '*', 'N', 'I', 'N', 'T', 'E', 'N', 'D', 'O', '-', 'H', 'V', 'C', '*', 0x01};
  u8 headerbuf[16];
  if (!seek_rom(state, disk_offset)) { return false; }
  if (!read_rom_bytes(state, headerbuf, 16)) { return false; }

  bool match = memcmp(headerbuf, fds_disk_verification, 16) == 0;
  if (!match) { return false; }
  state->which_game = GAME_SMB2J;
  state->smb2j_disk_offset = disk_offset;

  // Load these automatically
  smb2j_load_file(state, "SM2CHAR1");
  smb2j_load_file(state, "SM2MAIN ");
  smb2j_load_file(state, "SM2SAVE ");
  return true;
}


static bool detect_and_load_rom(struct SMB_state *state) {
  u8 headerbuf[16];
  if (!read_rom_bytes(state, headerbuf, 16)) {
    return false;
  }

  if (headerbuf[0] == 'N' && headerbuf[1] == 'E' && headerbuf[2] == 'S' && headerbuf[3] == 0x1A) {
    // An INES header. Probably SMB1.
    if (!load_smb1(state, 0x10, 0x8010)) {
      return false;
    }
  } else if (headerbuf[0] == 'F' && headerbuf[1] == 'D' && headerbuf[2] == 'S' && headerbuf[3] == 0x1A) {
    // An FDS header. Probably SMB2J.
    // TODO: there's an FDS version of SMB1. Maybe handle it?
    if (!load_smb2j(state, 0x10)) {
      return false;
    }
  } else {
    // There are some ROMs that don't include an FDS header, but just the single disk image.
    if (!load_smb2j(state, 0)) {
      return false;
    }
  }

  SMB_ram_finishwrite(state);

  return true;
}

size_t SMB_state_size(void) { return sizeof(struct SMB_state); }

bool SMB_state_init(struct SMB_state *state, const struct SMB_callbacks *cb) {
  SMB_STATE = state;

  memset(state, 0, sizeof(struct SMB_state));
  state->callbacks = *cb;
  state->start_on_world = 1;
  state->start_on_level = 1;
  state->viewport.width = 256;
  for (unsigned i = 0; i < SMB_VIEWPORT_COLUMNS; ++i) state->viewport.owners[i] = 0xffff;

  return detect_and_load_rom(state);
}

void SMB_start_on_level(struct SMB_state *state, u8 world, u8 level) {
  state->start_on_world = world;
  state->start_on_level = level;
}

int SMB_which_game(const struct SMB_state *state) { return state->which_game; }

u8 *SMB_ram(struct SMB_state *state) { return state->rammem; }

u8 *SMB_ppuram(struct SMB_state *state) { return state->ppuram; }

void SMB_ram_finishwrite(struct SMB_state *state) {
  // Set a global variable while accessing the RAM values
  SMB_STATE = state;

  if (state->which_game == GAME_SMB1) {
    smb1_sync_data();
  }
  if (state->which_game == GAME_SMB2J) {
    smb2j_sync_data();
  }
}

void SMB_tick(struct SMB_state *state) {
  // Set a global variable while it's being ticked
  SMB_STATE = state;

  backtrack_before_tick(state);
  state->viewport.render_camera = (u16)((unsigned)state->rammem[0x71a] * 256 + state->rammem[0x71c]);
  viewport_prepare(state);
  memcpy(state->viewport.latched_ram, state->rammem, 0x800);
  memcpy(state->viewport.latched_oam, state->rammem + 0x200, 256);
  memcpy(state->viewport.latched_origins, state->viewport.sprite_origins, sizeof(state->viewport.sprite_origins));
  memcpy(state->viewport.latched_valid, state->viewport.sprite_valid, sizeof(state->viewport.sprite_valid));
  state->viewport.latched = true;
  // A wide->classic resize still draws the old wide OAM once, with its real X
  // coordinates. The subsequent native frame already has ordinary clipping.
  if (SMB_viewport_width(state) <= 256)
    memset(state->viewport.sprite_valid, 0, sizeof(state->viewport.sprite_valid));
  state->viewport.origin_valid = false;
  memset(state->viewport.bbox_valid, 0, sizeof(state->viewport.bbox_valid));
  struct sprite sprites[64];

  if (state->which_game == GAME_SMB1) {
    if (!state->reset_occurred) {
      smb1_Reset();
    }

    // The NES wrote to OAM registers to initiate copying sprites
    // $2003 = 0
    // $4014 = 2
    transfer_sprite_data(&sprites[0], &Sprite_Data[0]);
    smb1_NMI();

    if (!state->reset_occurred) {
      smb1_set_world_and_level(state->start_on_world - 1, state->start_on_level - 1);
      state->reset_occurred = true;
    }
  }

  if (state->which_game == GAME_SMB2J) {
    if (!state->reset_occurred) {
      smb2j_Reset();
    }

    // The NES wrote to OAM registers to initiate copying sprites
    // $2003 = 0
    // $4014 = 2
    transfer_sprite_data(&sprites[0], &Sprite_Data[0]);
    smb2j_NMI();

    if (!state->reset_occurred) {
      smb2j_set_world_and_level(state->start_on_world - 1, state->start_on_level - 1);
      state->reset_occurred = true;
    }
  }

  backtrack_restore_ppu(state);
  draw_graphics(&sprites[0]);
}

static inline void draw_nametable_tile(int x, int y, u16 ppu_offset, int tilex,
                                       int tiley) {
  if (smb_viewport_active() && x >= 256) return;
  int j = tilex;
  int i = tiley;
  int off = i * 32 + j;
  int tileidx = PPURAM(ppu_offset + off) + 0x100;
  // Find which part of the pallete to use
  // Look it up in the attribute table

  u8 a = PPURAM(ppu_offset + 0x03c0 + (i / 4) * 8 + (j / 4));
  a >>= ((j / 2) % 2) * 2;
  a >>= ((i / 2) % 2) * 4;
  a &= 0x03;
  int paletteidx = a;

  struct SMB_tile tile;
  tile.x = x;
  tile.y = y;
  tile.tileidx = tileidx;
  tile.paletteidx = paletteidx;
  tile.flip_horz = false;
  tile.flip_vert = false;
  tile.extra_type = TILE_TYPE_BG;
  tile.extra_bg.x = tilex;
  tile.extra_bg.y = tilex;

  draw_tile(tile);
}

static inline void draw_nametable_rect(int x, int y, u16 ppu_offset, int fromx,
                                       int fromy, int tox, int toy) {
  for (int i = fromy; i < toy; i++) {
    for (int j = fromx; j < tox; j++) {
      draw_nametable_tile(x + (j - fromx) * 8, y + (i - fromy) * 8, ppu_offset, j, i);
    }
  }
}

void draw_graphics(const struct sprite sprites[64]) {
  // The palette can change even if the screen is off (the background color, namely)
  update_palette();

  if (!PPU_STATE.screen_on) {
    return;
  }
  if (!can_draw_tile()) {
    return;
  }

  for (int spriteidx = 64-1; spriteidx >= 0; spriteidx--) {
    if (sprites[spriteidx].draw_behind) {
      draw_tile(sprites[spriteidx].tile);
    }
  }

  const u16 scroll_x = PPU_STATE.scroll_x;

  // Status bar
  draw_nametable_rect(0, 0, 0x2000, 0, 0, 32, 4);

  int scroll_off = -(scroll_x % 256);
  int scroll_nametable = scroll_x / 256;

  if (scroll_nametable == 0) {
    draw_nametable_rect(scroll_off, 32, 0x2000, 0, 4, 32, 30);
    draw_nametable_rect(256 + scroll_off, 32, 0x2400, 0, 4, 32, 30);
  } else {
    draw_nametable_rect(scroll_off, 32, 0x2400, 0, 4, 32, 30);
    draw_nametable_rect(256 + scroll_off, 32, 0x2000, 0, 4, 32, 30);
  }

  if (smb_viewport_active()) {
    const unsigned camera = SMB_STATE->viewport.render_camera;
    const unsigned first = (camera + 256) / 16;
    const unsigned last = (camera + SMB_STATE->viewport.width - 1) / 16;
    for (unsigned column = first; column <= last; ++column) {
      for (unsigned row = 0; row < 13; ++row) {
        u8 tiles[4], palette, collision;
        if (!SMB_viewport_cell(SMB_STATE, column, row, tiles, &palette, &collision)) continue;
        bool bumped = false;
        for (unsigned slot = 0; slot < 2; ++slot)
          bumped |= SMB_STATE->rammem[0x26 + slot] != 0 &&
            SMB_STATE->backtrack.block_columns[slot] == column &&
            SMB_STATE->rammem[0x3e4 + slot] / 16 == row;
        for (unsigned t = 0; t < 4; ++t) {
          struct SMB_tile tile = {0};
          tile.x = (int)(column * 16 + (t >= 2 ? 8 : 0)) - (int)camera;
          if (tile.x < 256) continue;
          tile.y = 32 + (int)(row * 16 + (t & 1) * 8);
          tile.tileidx = 0x100 + (bumped ? 0x24 : tiles[t]);
          tile.paletteidx = palette;
          tile.extra_type = TILE_TYPE_BG_WORLD;
          tile.extra_bg.x = (u16)(column * 2 + (t >= 2));
          tile.extra_bg.y = (u16)(4 + row * 2 + (t & 1));
          draw_tile(tile);
        }
      }
    }
  }

  for (int spriteidx = 64-1; spriteidx >= 0; spriteidx--) {
    if (!sprites[spriteidx].draw_behind) {
      draw_tile(sprites[spriteidx].tile);
    }
  }
}

// A fixed byte format, independent of struct packing, bool size and addresses.
// Header/scalars (44), native byte arrays (90112), FNV-1a checksum (8).
#define SMB_SNAPSHOT_BYTES (44u + 0x10000u + 0x2000u + 0x4000u + 8u)
#define SMB_SNAPSHOT_NULL_POINTER UINT32_C(0xffffffff)
static const u8 SMB_SNAPSHOT_MAGIC[8] = {'S','M','B','S','N','P','0','1'};

static void snapshot_put16(u8 *out, uint16_t value) {
  out[0] = (u8)value; out[1] = (u8)(value >> 8);
}
static void snapshot_put32(u8 *out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out[i] = (u8)(value >> (8u * i));
}
static void snapshot_put64(u8 *out, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) out[i] = (u8)(value >> (8u * i));
}
static uint16_t snapshot_get16(const u8 *in) {
  return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}
static uint32_t snapshot_get32(const u8 *in) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= (uint32_t)in[i] << (8u * i);
  return value;
}
static uint64_t snapshot_get64(const u8 *in) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)in[i] << (8u * i);
  return value;
}
static uint64_t snapshot_checksum(const u8 *in, size_t size) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < size; ++i) {
    hash ^= in[i]; hash *= UINT64_C(1099511628211);
  }
  return hash;
}
static bool snapshot_pointer_offset(const struct SMB_state *state,
                                    const u8 *pointer, uint32_t *offset) {
  if (!pointer) { *offset = SMB_SNAPSHOT_NULL_POINTER; return true; }
  const uintptr_t base = (uintptr_t)state->rammem;
  const uintptr_t address = (uintptr_t)pointer;
  if (address < base || address - base >= sizeof(state->rammem)) return false;
  *offset = (uint32_t)(address - base);
  return true;
}
static bool snapshot_valid_offset(uint32_t offset) {
  return offset < 0x10000u || offset == SMB_SNAPSHOT_NULL_POINTER;
}
static const u8 *snapshot_restore_pointer(struct SMB_state *state, uint32_t offset) {
  return offset == SMB_SNAPSHOT_NULL_POINTER ? NULL : state->rammem + offset;
}

size_t SMB_snapshot_size(void) { return SMB_SNAPSHOT_BYTES; }

bool SMB_snapshot_export(const struct SMB_state *state, void *bytes, size_t size) {
  uint32_t area, enemy, music;
  if (!state || !bytes || size != SMB_SNAPSHOT_BYTES ||
      state->which_game != GAME_SMB1 || state->smb2j_disk_offset != 0 ||
      state->start_on_world < 1 || state->start_on_world > 8 ||
      state->start_on_level < 1 || state->start_on_level > 4 ||
      !snapshot_pointer_offset(state, state->area_data, &area) ||
      !snapshot_pointer_offset(state, state->enemy_data, &enemy) ||
      !snapshot_pointer_offset(state, state->music_data, &music)) return false;
  u8 *out = (u8*)bytes;
  memcpy(out, SMB_SNAPSHOT_MAGIC, 8);
  snapshot_put32(out + 8, 1);
  snapshot_put32(out + 12, SMB_SNAPSHOT_BYTES);
  snapshot_put32(out + 16, (uint32_t)state->which_game);
  out[20] = state->reset_occurred ? 1 : 0;
  out[21] = state->start_on_world;
  out[22] = state->start_on_level;
  out[23] = state->patch_current_player;
  snapshot_put16(out + 24, state->ppu.scroll_x);
  snapshot_put16(out + 26, state->ppu.scroll_y);
  out[28] = state->ppu.screen_on ? 1 : 0;
  out[29] = out[30] = out[31] = 0;
  snapshot_put32(out + 32, area);
  snapshot_put32(out + 36, enemy);
  snapshot_put32(out + 40, music);
  // The SMB2J disk offset is unsupported for SMB1 and is not part of this format.
  memcpy(out + 44, state->rammem, sizeof(state->rammem));
  memcpy(out + 44 + 0x10000u, state->chrrom, sizeof(state->chrrom));
  memcpy(out + 44 + 0x12000u, state->ppuram, sizeof(state->ppuram));
  snapshot_put64(out + size - 8, snapshot_checksum(out, size - 8));
  return true;
}

bool SMB_snapshot_import(struct SMB_state *state, const void *bytes, size_t size) {
  if (!state || !bytes || size != SMB_SNAPSHOT_BYTES ||
      state->which_game != GAME_SMB1 || state->smb2j_disk_offset != 0) return false;
  const u8 *in = (const u8*)bytes;
  if (memcmp(in, SMB_SNAPSHOT_MAGIC, 8) != 0 || snapshot_get32(in + 8) != 1 ||
      snapshot_get32(in + 12) != SMB_SNAPSHOT_BYTES ||
      snapshot_get32(in + 16) != GAME_SMB1 ||
      snapshot_get64(in + size - 8) != snapshot_checksum(in, size - 8) ||
      in[20] > 1 || in[21] < 1 || in[21] > 8 ||
      in[22] < 1 || in[22] > 4 || in[28] > 1 || in[29] || in[30] || in[31]) return false;
  const uint32_t area = snapshot_get32(in + 32);
  const uint32_t enemy = snapshot_get32(in + 36);
  const uint32_t music = snapshot_get32(in + 40);
  if (!snapshot_valid_offset(area) || !snapshot_valid_offset(enemy) ||
      !snapshot_valid_offset(music) ||
      memcmp(in + 44 + 0x8000u, state->rammem + 0x8000u, 0x8000u) != 0 ||
      memcmp(in + 44 + 0x10000u, state->chrrom, 0x2000u) != 0) return false;

  // All validation has finished. Preserve this process's callbacks and userdata.
  memcpy(state->rammem, in + 44, sizeof(state->rammem));
  memcpy(state->chrrom, in + 44 + 0x10000u, sizeof(state->chrrom));
  memcpy(state->ppuram, in + 44 + 0x12000u, sizeof(state->ppuram));
  state->which_game = GAME_SMB1;
  state->reset_occurred = in[20] != 0;
  state->start_on_world = in[21];
  state->start_on_level = in[22];
  state->patch_current_player = in[23];
  state->smb2j_disk_offset = 0;
  state->ppu.scroll_x = snapshot_get16(in + 24);
  state->ppu.scroll_y = snapshot_get16(in + 26);
  state->ppu.screen_on = in[28] != 0;
  state->area_data = snapshot_restore_pointer(state, area);
  state->enemy_data = snapshot_restore_pointer(state, enemy);
  state->music_data = snapshot_restore_pointer(state, music);
  // The portable native envelope is unchanged. A legacy restore must never
  // inherit history from a different run; seed its visible terrain next tick.
  const bool history_enabled = state->backtrack.enabled;
  memset(&state->backtrack, 0, sizeof(state->backtrack));
  state->backtrack.enabled = history_enabled;
  const u16 viewport_width = state->viewport.width;
  memset(&state->viewport, 0, sizeof(state->viewport));
  state->viewport.width = viewport_width;
  const unsigned native_camera = (unsigned)state->rammem[0x71a] * 256 + state->rammem[0x71c];
  unsigned render_camera = (native_camera & ~511u) + (state->ppu.scroll_x & 511u);
  if (render_camera > native_camera + 256u && render_camera >= 512u) render_camera -= 512u;
  if (render_camera + 256u < native_camera && render_camera <= 65535u - 512u) render_camera += 512u;
  state->viewport.render_camera = (u16)render_camera;
  for (unsigned i = 0; i < SMB_VIEWPORT_COLUMNS; ++i) state->viewport.owners[i] = 0xffff;
  viewport_seed_sprites(state);
  update_pattern_tables(state);
  if (state->callbacks.update_palette)
    state->callbacks.update_palette(state->callbacks.userdata, state->ppuram + 0x3f00u);
  return true;
}

// Optional PC camera/history extension. None of these hooks writes native RAM
// while disabled. Forward play also retains original PPU/collision behaviour
// until the first real reverse scroll.
#define BACKTRACK_FILE_BYTES (104u + SMB_BACKTRACK_COLUMNS * 79u + 8u)
static const u8 BACKTRACK_MAGIC[8] = {'S','M','B','B','K','T','0','1'};
static u16 backtrack_camera(const struct SMB_state *state) {
  return (u16)((u16)state->rammem[0x71a] * 256 + state->rammem[0x71c]);
}
static bool backtrack_normal(const struct SMB_state *state) {
  return state->which_game == GAME_SMB1 && state->rammem[0x770] == 1 &&
    state->rammem[0x772] == 3 && state->rammem[0x0e] == 8;
}
static unsigned backtrack_block_offset(unsigned column, unsigned row) {
  return ((column & 31) < 16 ? 0 : 0xd0) + (column & 15) + row * 16;
}
static void backtrack_clear(struct SMB_state *state, bool initialized) {
  const bool enabled = state->backtrack.enabled;
  memset(&state->backtrack, 0, sizeof(state->backtrack));
  struct smb_backtrack_state *b = &state->backtrack;
  b->enabled = enabled;
  b->initialized = initialized;
  b->minimum = b->frontier = b->render_camera = backtrack_camera(state);
  for (unsigned i = 0; i < 32; ++i) b->owners[i] = 0xffff;
  b->block_columns[0] = b->block_columns[1] = 0xffff;
  b->world = state->rammem[0x75f]; b->level = state->rammem[0x75c];
  b->area = state->rammem[0x750]; b->area_type = state->rammem[0x74e];
  b->area_data_address = (u16)(state->rammem[0xe7] | ((u16)state->rammem[0xe8] << 8));
}
void SMB_enable_backtracking(struct SMB_state *state, bool enabled) {
  if (!state) return;
  if (state->backtrack.enabled == enabled) return;
  state->backtrack.enabled = enabled && state->which_game == GAME_SMB1;
  backtrack_clear(state, false);
}
void smb_backtrack_reset(void) {
  if (SMB_STATE && SMB_STATE->backtrack.enabled) backtrack_clear(SMB_STATE, true);
}
// A complete initial column is captured from the native parser before the
// nametable/ring wraps; decor and collision are intentionally separate.
void smb_backtrack_capture_column(u16 column, const u8 *metatiles) {
  struct SMB_state *state = SMB_STATE;
  struct smb_backtrack_state *b = &state->backtrack;
  if (!b->enabled || state->which_game != GAME_SMB1 || column >= SMB_BACKTRACK_COLUMNS) return;
  struct smb_backtrack_column *c = &b->columns[column];
  c->valid = 1;
  b->owners[column & 31] = column;
  for (unsigned y = 0; y < 13; ++y) {
    u8 mt = metatiles[y];
    unsigned bank = mt >> 6;
    unsigned address = state->rammem[0x8b08 + bank] | ((unsigned)state->rammem[0x8b0c + bank] << 8);
    for (unsigned t = 0; t < 4; ++t) c->tiles[y][t] = state->rammem[(address + (unsigned)mt * 4 % 256 + t) & 0xffff];
    c->tiles[y][4] = (u8)bank;
    c->collision[y] = state->rammem[0x500 + backtrack_block_offset(column, y)];
  }
}
bool smb_backtrack_collision_read(u16 column, u16 row, u8 *value) {
  struct smb_backtrack_state *b = &SMB_STATE->backtrack;
  if (smb_viewport_active() && !SMB_STATE->viewport.previewing && row < 13) {
    u8 tiles[4], palette;
    if (SMB_viewport_cell(SMB_STATE, column, row, tiles, &palette, value)) return true;
  }
  if (!b->enabled || !b->active || row >= 13 || column >= SMB_BACKTRACK_COLUMNS || !b->columns[column].valid) return false;
  *value = b->columns[column].collision[row];
  return true;
}
bool smb_backtrack_collision_write(u16 column, u16 row, u8 value) {
  struct smb_backtrack_state *b = &SMB_STATE->backtrack;
  if (!b->enabled || b->parsing || row >= 13 || column >= SMB_BACKTRACK_COLUMNS || !b->columns[column].valid) return true;
  // Collision may temporarily contain BLOCKHIT during the native bump. Graphics
  // are only committed by the final block replacement, never by that marker.
  b->columns[column].collision[row] = value;
  return !b->active || b->owners[column & 31] == column;
}
void smb_backtrack_store_graphics(u16 column, u16 row, u8 tl, u8 bl, u8 tr, u8 br) {
  struct smb_backtrack_state *b = &SMB_STATE->backtrack;
  if (!b->enabled || row >= 13 || column >= SMB_BACKTRACK_COLUMNS || !b->columns[column].valid) return;
  u8 *tiles = b->columns[column].tiles[row];
  tiles[0] = tl; tiles[1] = bl; tiles[2] = tr; tiles[3] = br;
  // The original block writers retain the nametable attribute quadrant.
}
void smb_backtrack_remember_block(u8 slot, u16 column) {
  if (SMB_STATE->backtrack.enabled && slot < 2) SMB_STATE->backtrack.block_columns[slot] = column;
}
u16 smb_backtrack_block_column(u8 slot, u16 fallback) {
  const struct smb_backtrack_state *b = &SMB_STATE->backtrack;
  return b->enabled && slot < 2 && b->block_columns[slot] < SMB_BACKTRACK_COLUMNS ? b->block_columns[slot] : fallback;
}
u8 smb_backtrack_forward_budget(u8 amount) {
  struct smb_backtrack_state *b = &SMB_STATE->backtrack;
  if (!b->enabled || !b->initialized) return amount;
  unsigned next = (unsigned)backtrack_camera(SMB_STATE) + amount;
  if (next <= b->frontier) return 0;
  unsigned advance = next - b->frontier;
  b->frontier = (u16)next;
  return (u8)advance;
}
void smb_backtrack_restore_collision(void) {
  struct SMB_state *state = SMB_STATE;
  struct smb_backtrack_state *b = &state->backtrack;
  if (!b->enabled || !b->active || !backtrack_normal(state)) return;
  // One complete 512px collision ring around the camera. Absolute cached reads
  // also cover live actors outside this window without aliasing visible cells.
  int start = (int)(backtrack_camera(state) / 16) - 8;
  for (int i = 0; i < 32; ++i) {
    int column = start + i;
    if (column < 0 || column >= SMB_BACKTRACK_COLUMNS || !b->columns[column].valid) continue;
    b->owners[column & 31] = (u16)column;
    for (unsigned y = 0; y < 13; ++y)
      state->rammem[0x500 + backtrack_block_offset((unsigned)column, y)] = b->columns[column].collision[y];
  }
}
bool smb_backtrack_scroll_left(i8 amount) {
  struct SMB_state *state = SMB_STATE;
  struct smb_backtrack_state *b = &state->backtrack;
  if (!b->enabled || !b->initialized || !backtrack_normal(state) ||
      state->rammem[0x723] || state->rammem[0x755] > 0x50 ||
      state->rammem[0x785] || amount >= 0 || amount == -128) return false;
  int camera = backtrack_camera(state), next = camera + amount;
  if (next < b->minimum) next = b->minimum;
  // Never expose an unparsed column (legacy saves may know only one window).
  if (next < camera && !b->columns[(unsigned)next / 16].valid) next = camera;
  if (next == camera) return false;
  b->active = true;
  state->rammem[0x71a] = (u8)((unsigned)next >> 8);
  state->rammem[0x71c] = (u8)next;
  state->rammem[0x73f] = (u8)next;
  state->rammem[0x778] = (u8)(((unsigned)next >> 8) & 1);
  unsigned right = (unsigned)next + 255;
  state->rammem[0x71b] = (u8)(right >> 8); state->rammem[0x71d] = (u8)right;
  // ScrollAmount is a signed byte interpreted modulo 256 by floating-number
  // offsets; this preserves their anchoring when the camera moves left.
  state->rammem[0x775] = (u8)(i8)(next - camera);
  state->rammem[0x795] = 8;
  smb_backtrack_restore_collision();
  return true;
}
static void backtrack_seed_visible(struct SMB_state *state) {
  backtrack_clear(state, true);
  struct smb_backtrack_state *b = &state->backtrack;
  unsigned first = backtrack_camera(state) / 16;
  unsigned last = ((unsigned)backtrack_camera(state) + 255) / 16;
  for (unsigned column = first; column <= last && column < SMB_BACKTRACK_COLUMNS; ++column) {
    struct smb_backtrack_column *c = &b->columns[column];
    c->valid = 1; b->owners[column & 31] = (u16)column;
    unsigned x = (column & 15) * 2;
    unsigned nt = 0x2000 + ((column & 16) ? 0x400 : 0);
    for (unsigned y = 0; y < 13; ++y) {
      unsigned ty = (y + 2) * 2, p = nt + ty * 32 + x;
      c->tiles[y][0] = state->ppuram[p]; c->tiles[y][1] = state->ppuram[p + 32];
      c->tiles[y][2] = state->ppuram[p + 1]; c->tiles[y][3] = state->ppuram[p + 33];
      unsigned attr = nt + 0x3c0 + (ty / 4) * 8 + x / 4;
      unsigned shift = ((ty / 2) & 1) * 4 + ((x / 2) & 1) * 2;
      c->tiles[y][4] = (state->ppuram[attr] >> shift) & 3;
      c->collision[y] = state->rammem[0x500 + backtrack_block_offset(column, y)];
    }
  }
}
static void backtrack_before_tick(struct SMB_state *state) {
  if (!state->backtrack.enabled || state->which_game != GAME_SMB1) return;
  if (!state->backtrack.initialized && backtrack_normal(state)) backtrack_seed_visible(state);
  state->backtrack.render_camera = backtrack_camera(state);
  smb_backtrack_restore_collision();
}
static void backtrack_restore_ppu(struct SMB_state *state) {
  struct smb_backtrack_state *b = &state->backtrack;
  if (!b->enabled || !b->active || !backtrack_normal(state)) return;
  // NMI latches scrolling/OAM before gameplay moves the camera. Render from
  // that same old camera so one frame never mixes sprites and terrain.
  int first = (int)(b->render_camera / 16) - 1;
  int last = (int)(((unsigned)b->render_camera + 255) / 16) + 1;
  for (int column = first; column <= last; ++column) {
    if (column < 0 || column >= SMB_BACKTRACK_COLUMNS || !b->columns[column].valid) continue;
    const struct smb_backtrack_column *c = &b->columns[column];
    unsigned x = ((unsigned)column & 15) * 2;
    unsigned nt = 0x2000 + (((unsigned)column & 16) ? 0x400 : 0);
    for (unsigned y = 0; y < 13; ++y) {
      unsigned ty = (y + 2) * 2, p = nt + ty * 32 + x;
      bool bumped = false;
      for (unsigned slot = 0; slot < 2; ++slot)
        bumped |= state->rammem[0x26 + slot] != 0 && b->block_columns[slot] == column &&
          state->rammem[0x3e4 + slot] / 16 == y;
      // The moving native block sprite owns a bumped cell until its animation
      // finishes. Keep this temporary hole out of persistent map history.
      state->ppuram[p] = bumped ? 0x24 : c->tiles[y][0];
      state->ppuram[p + 32] = bumped ? 0x24 : c->tiles[y][1];
      state->ppuram[p + 1] = bumped ? 0x24 : c->tiles[y][2];
      state->ppuram[p + 33] = bumped ? 0x24 : c->tiles[y][3];
      unsigned attr = nt + 0x3c0 + (ty / 4) * 8 + x / 4;
      unsigned shift = ((ty / 2) & 1) * 4 + ((x / 2) & 1) * 2;
      state->ppuram[attr] = (state->ppuram[attr] & ~(3u << shift)) | ((unsigned)c->tiles[y][4] << shift);
    }
  }
}
size_t SMB_backtracking_size(const struct SMB_state *state) {
  (void)state; return BACKTRACK_FILE_BYTES;
}
bool SMB_backtracking_export(const struct SMB_state *state, void *bytes, size_t size) {
  if (!state || !bytes || size != BACKTRACK_FILE_BYTES || state->which_game != GAME_SMB1) return false;
  const struct smb_backtrack_state *b = &state->backtrack;
  u8 *out = (u8*)bytes; memset(out, 0, size);
  memcpy(out, BACKTRACK_MAGIC, 8); snapshot_put32(out + 8, 1); snapshot_put32(out + 12, BACKTRACK_FILE_BYTES);
  out[16] = b->enabled; out[17] = b->active; out[18] = b->initialized;
  snapshot_put16(out + 20, b->minimum); snapshot_put16(out + 22, b->frontier);
  snapshot_put16(out + 24, b->render_camera);
  out[26] = b->world; out[27] = b->level; out[28] = b->area; out[29] = b->area_type;
  snapshot_put16(out + 30, b->area_data_address);
  for (unsigned i = 0; i < 2; ++i) snapshot_put16(out + 32 + i * 2, b->block_columns[i]);
  for (unsigned i = 0; i < 32; ++i) snapshot_put16(out + 40 + i * 2, b->owners[i]);
  for (unsigned i = 0; i < SMB_BACKTRACK_COLUMNS; ++i) {
    u8 *p = out + 104 + i * 79;
    p[0] = b->columns[i].valid;
    memcpy(p + 1, b->columns[i].tiles, 65); memcpy(p + 66, b->columns[i].collision, 13);
  }
  snapshot_put64(out + size - 8, snapshot_checksum(out, size - 8));
  return true;
}
bool SMB_backtracking_validate(const void *bytes, size_t size) {
  if (!bytes || size != BACKTRACK_FILE_BYTES) return false;
  const u8 *in = (const u8*)bytes;
  if (memcmp(in, BACKTRACK_MAGIC, 8) || snapshot_get32(in + 8) != 1 ||
      snapshot_get32(in + 12) != BACKTRACK_FILE_BYTES ||
      snapshot_get64(in + size - 8) != snapshot_checksum(in, size - 8) ||
      in[16] > 1 || in[17] > 1 || in[18] > 1 || in[19] ||
      in[36] || in[37] || in[38] || in[39] || in[26] > 7 || in[27] > 3 || in[29] > 3 ||
      (in[17] && (!in[16] || !in[18])) || snapshot_get16(in + 20) > snapshot_get16(in + 22)) return false;
  for (unsigned i = 0; i < 34; ++i) {
    unsigned p = i < 2 ? 32 + i * 2 : 40 + (i - 2) * 2;
    u16 column = snapshot_get16(in + p);
    if (column != 0xffff && column >= SMB_BACKTRACK_COLUMNS) return false;
  }
  for (unsigned i = 0; i < SMB_BACKTRACK_COLUMNS; ++i) {
    const u8 *p = in + 104 + i * 79;
    if (p[0] > 1) return false;
    for (unsigned y = 0; y < 13; ++y) if (p[1 + y * 5 + 4] > 3) return false;
  }
  return true;
}
bool SMB_backtracking_matches_ram(const void *bytes, size_t size, const u8 *incoming_ram) {
  if (!incoming_ram || !SMB_backtracking_validate(bytes, size)) return false;
  const u8 *in = (const u8*)bytes;
  // AreaPointer (0750) is a future pipe destination and changes while merely
  // passing pipes. The current terrain stream address is the stable identity.
  return !in[18] || (in[26] == incoming_ram[0x75f] && in[27] == incoming_ram[0x75c] &&
    in[29] == incoming_ram[0x74e] && snapshot_get16(in + 30) ==
      (unsigned)(incoming_ram[0xe7] | ((unsigned)incoming_ram[0xe8] << 8)));
}
bool SMB_backtracking_import(struct SMB_state *state, const void *bytes, size_t size) {
  if (!state || state->which_game != GAME_SMB1 || !SMB_backtracking_matches_ram(bytes, size, state->rammem)) return false;
  const u8 *in = (const u8*)bytes;
  // Every check finished before mutating state, with no renderer callbacks.
  struct smb_backtrack_state *b = &state->backtrack;
  memset(b, 0, sizeof(*b));
  b->enabled = in[16] != 0; b->active = in[17] != 0; b->initialized = in[18] != 0;
  b->minimum = snapshot_get16(in + 20); b->frontier = snapshot_get16(in + 22);
  b->render_camera = snapshot_get16(in + 24);
  if (b->initialized) state->viewport.render_camera = b->render_camera;
  b->world = in[26]; b->level = in[27]; b->area = in[28]; b->area_type = in[29];
  b->area_data_address = snapshot_get16(in + 30);
  for (unsigned i = 0; i < 2; ++i) b->block_columns[i] = snapshot_get16(in + 32 + i * 2);
  for (unsigned i = 0; i < 32; ++i) b->owners[i] = snapshot_get16(in + 40 + i * 2);
  for (unsigned i = 0; i < SMB_BACKTRACK_COLUMNS; ++i) {
    const u8 *p = in + 104 + i * 79;
    b->columns[i].valid = p[0];
    memcpy(b->columns[i].tiles, p + 1, 65); memcpy(b->columns[i].collision, p + 66, 13);
  }
  return true;
}

// The expanded PC view is derived from the real area parser. It never copies
// preview RAM/PPU, enemies, timers, or parser side effects into the live engine.
int SMB_viewport_width(const struct SMB_state *state) {
  return state && state->viewport.width >= 256 ? state->viewport.width : 256;
}
int SMB_viewport_camera_x(const struct SMB_state *state) {
  return state ? state->viewport.render_camera : 0;
}
const u8 *SMB_presentation_ram(const struct SMB_state *state) {
  return state ? (state->viewport.latched ? state->viewport.latched_ram : state->rammem) : NULL;
}
bool smb_viewport_active(void) {
  return SMB_STATE && SMB_STATE->which_game == GAME_SMB1 &&
    SMB_viewport_width(SMB_STATE) > 256 && SMB_STATE->rammem[0x722] != 0 &&
    SMB_STATE->rammem[0x772] == 3 && SMB_STATE->rammem[0x770] <= 2;
}
u16 smb_viewport_right(void) {
  if (smb_viewport_active())
    return (u16)(backtrack_camera(SMB_STATE) + SMB_viewport_width(SMB_STATE) - 1);
  return (u16)((unsigned)SMB_STATE->rammem[0x71b] * 256 + SMB_STATE->rammem[0x71d]);
}
static unsigned viewport_bbox_object(unsigned slot) {
  return slot >= 9 && slot <= 17 ? slot + 4 : slot;
}
void smb_viewport_bbox(u8 slot, u8 left, u8 right) {
  if (!smb_viewport_active() || slot >= 22) return;
  const unsigned object = viewport_bbox_object(slot);
  if (object >= 25) return;
  const i32 x = (i32)((unsigned)SMB_STATE->rammem[0x6d + object] * 256 + SMB_STATE->rammem[0x86 + object]);
  SMB_STATE->viewport.bbox_left[slot] = x + left;
  SMB_STATE->viewport.bbox_right[slot] = x + right;
  SMB_STATE->viewport.bbox_valid[slot] = true;
}
void smb_viewport_bbox_hide(u8 slot) {
  if (SMB_STATE && slot < 22) SMB_STATE->viewport.bbox_valid[slot] = false;
}
void smb_viewport_origin(u8 object) {
  if (!smb_viewport_active() || object >= 25) return;
  SMB_STATE->viewport.origin = (i32)((unsigned)SMB_STATE->rammem[0x6d + object] * 256 +
    SMB_STATE->rammem[0x86 + object]) - backtrack_camera(SMB_STATE);
  SMB_STATE->viewport.origin_valid = true;
}
u8 *smb_viewport_sprite_x(unsigned offset) {
  if (offset < 256 && (offset & 3) == 3 && smb_viewport_active() && SMB_STATE->viewport.origin_valid) {
    SMB_STATE->viewport.sprite_origins[offset / 4] = SMB_STATE->viewport.origin;
    SMB_STATE->viewport.sprite_valid[offset / 4] = true;
  }
  return SMB_STATE->rammem + 0x200 + offset;
}
static void viewport_seed_group(struct SMB_state *state, unsigned offset, unsigned count, unsigned object) {
  if (object >= 25) return;
  const i32 origin = (i32)((unsigned)state->rammem[0x6d + object] * 256 +
    state->rammem[0x86 + object]) - backtrack_camera(state);
  for (unsigned i = 0; i < count; ++i) {
    const unsigned slot = ((offset + i * 4) & 255) / 4;
    state->viewport.sprite_origins[slot] = origin;
    state->viewport.sprite_valid[slot] = true;
  }
}
static void viewport_seed_sprites(struct SMB_state *state) {
  if (SMB_viewport_width(state) <= 256) return;
  memset(state->viewport.sprite_valid, 0, sizeof(state->viewport.sprite_valid));
  // Saved native sprite allocation supplies origins without extending the save
  // format. Inactive/offscreen OAM retains its original Y and is still clipped.
  for (unsigned i = 0; i < 9; ++i)
    if (state->rammem[0x2a + i]) viewport_seed_group(state, state->rammem[0x6f3 + i], 2, 13 + i);
  for (unsigned i = 0; i < 3; ++i)
    viewport_seed_group(state, state->rammem[0x6ee + i], 1, 22 + i);
  for (unsigned i = 0; i < 2; ++i) {
    if (state->rammem[0x26 + i]) viewport_seed_group(state, state->rammem[0x6ec + i], 4, 9 + i);
    if (state->rammem[0x24 + i]) viewport_seed_group(state, state->rammem[0x6f1 + i], 4, 7 + i);
  }
  for (unsigned i = 0; i < 6; ++i)
    if (state->rammem[0xf + i]) viewport_seed_group(state, state->rammem[0x6e5 + i], 6, i + 1);
  viewport_seed_group(state, state->rammem[0x6e4], 8, 0);
}
void SMB_set_viewport_width(struct SMB_state *state, int width) {
  if (!state) return;
  if (width < 256) width = 256;
  if (width > 512) width = 512;
  if (SMB_viewport_width(state) == width) return;
  state->viewport.width = (u16)width;
  memset(state->viewport.columns, 0, sizeof(state->viewport.columns));
  for (unsigned i = 0; i < SMB_VIEWPORT_COLUMNS; ++i) state->viewport.owners[i] = 0xffff;
  viewport_seed_sprites(state);
  if (!state->viewport.latched) {
    memcpy(state->viewport.latched_origins, state->viewport.sprite_origins, sizeof(state->viewport.sprite_origins));
    memcpy(state->viewport.latched_valid, state->viewport.sprite_valid, sizeof(state->viewport.sprite_valid));
  }
}
bool SMB_viewport_cell(const struct SMB_state *state, unsigned column, unsigned row,
                       u8 tiles[4], u8 *palette, u8 *collision) {
  if (!state || !tiles || !palette || !collision || column >= SMB_BACKTRACK_COLUMNS || row >= 13) return false;
  const struct smb_backtrack_column *cell = NULL;
  if (state->backtrack.enabled && state->backtrack.initialized && state->backtrack.columns[column].valid)
    cell = &state->backtrack.columns[column];
  else if (state->viewport.owners[column % SMB_VIEWPORT_COLUMNS] == column &&
           state->viewport.columns[column % SMB_VIEWPORT_COLUMNS].valid)
    cell = &state->viewport.columns[column % SMB_VIEWPORT_COLUMNS];
  if (!cell) return false;
  memcpy(tiles, cell->tiles[row], 4); *palette = cell->tiles[row][4]; *collision = cell->collision[row];
  return true;
}
static const u8 *viewport_clone_pointer(const struct SMB_state *state, struct SMB_state *clone, const u8 *p) {
  if (!p) return NULL;
  const uintptr_t address = (uintptr_t)p, base = (uintptr_t)state->rammem;
  return address >= base && address - base < sizeof(state->rammem) ? clone->rammem + (address - base) : p;
}
static void viewport_prepare(struct SMB_state *state) {
  if (!smb_viewport_active() || state->viewport.previewing) return;
  struct SMB_state *clone = (struct SMB_state*)malloc(sizeof(*clone));
  if (!clone) return;
  memcpy(clone, state, sizeof(*clone));
  clone->area_data = viewport_clone_pointer(state, clone, state->area_data);
  clone->enemy_data = viewport_clone_pointer(state, clone, state->enemy_data);
  clone->music_data = viewport_clone_pointer(state, clone, state->music_data);
  memset(&clone->callbacks, 0, sizeof(clone->callbacks));
  clone->viewport.previewing = true;
  clone->backtrack.enabled = true;
  clone->backtrack.active = false;
  // Gameplay has already prepared the initial viewport. Continue from its
  // exact parser task, including partially emitted columns and area commands.
  const unsigned first = state->viewport.render_camera / 16;
  const unsigned last = (state->viewport.render_camera + state->viewport.width + 80) / 16;
  const unsigned parser_column = (unsigned)clone->rammem[0x725] * 16 + clone->rammem[0x726];
  const unsigned budget = last >= parser_column ? (last - parser_column + 2) * 4 + 16 : 0;
  struct SMB_state *previous = SMB_STATE;
  SMB_STATE = clone;
  for (unsigned step = 0; step < budget && step < 1024; ++step) {
    const unsigned current = (unsigned)clone->rammem[0x725] * 16 + clone->rammem[0x726];
    if (current > last) break;
    // Normal NMI flushes this 192-byte command buffer between tasks. Preview
    // consumes complete metatiles, so avoid accumulating unused VRAM commands.
    clone->rammem[0x340] = 0;
    smb1_AreaParserTaskHandler();
  }
  SMB_STATE = previous;
  memset(state->viewport.columns, 0, sizeof(state->viewport.columns));
  for (unsigned i = 0; i < SMB_VIEWPORT_COLUMNS; ++i) state->viewport.owners[i] = 0xffff;
  for (unsigned column = first; column <= last && column < SMB_BACKTRACK_COLUMNS; ++column) {
    if (!clone->backtrack.columns[column].valid) continue;
    const unsigned slot = column % SMB_VIEWPORT_COLUMNS;
    state->viewport.owners[slot] = (u16)column;
    state->viewport.columns[slot] = clone->backtrack.columns[column];
  }
  free(clone);
}
void SMB_redraw(struct SMB_state *state) {
  if (!state) return;
  struct SMB_state *previous = SMB_STATE;
  SMB_STATE = state;
  viewport_prepare(state);
  struct sprite sprites[64];
  if (!state->viewport.latched) {
    viewport_seed_sprites(state);
    memcpy(state->viewport.latched_origins, state->viewport.sprite_origins, sizeof(state->viewport.sprite_origins));
    memcpy(state->viewport.latched_valid, state->viewport.sprite_valid, sizeof(state->viewport.sprite_valid));
  }
  transfer_sprite_data(sprites, state->viewport.latched ? state->viewport.latched_oam : state->rammem + 0x200);
  draw_graphics(sprites);
  SMB_STATE = previous;
}

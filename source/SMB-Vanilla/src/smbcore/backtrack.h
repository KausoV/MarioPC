#ifndef SMB_BACKTRACK_H
#define SMB_BACKTRACK_H
// Internal hooks run only inside SMB_tick, with SMB_STATE selected.
void smb_backtrack_reset(void);
void smb_backtrack_capture_column(unsigned short column, const unsigned char *metatiles);
unsigned char smb_backtrack_forward_budget(unsigned char amount);
bool smb_backtrack_scroll_left(signed char amount);
void smb_backtrack_restore_collision(void);
bool smb_backtrack_collision_read(unsigned short column, unsigned short row, unsigned char *value);
bool smb_backtrack_collision_write(unsigned short column, unsigned short row, unsigned char value);
void smb_backtrack_store_graphics(unsigned short column, unsigned short row,
 unsigned char tl, unsigned char bl, unsigned char tr, unsigned char br);
void smb_backtrack_remember_block(unsigned char slot, unsigned short column);
unsigned short smb_backtrack_block_column(unsigned char slot, unsigned short fallback);
#endif

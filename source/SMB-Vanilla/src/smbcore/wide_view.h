#ifndef SMB_WIDE_VIEW_H
#define SMB_WIDE_VIEW_H
// Optional PC presentation/spawn extension. Native RAM remains byte-sized.
bool smb_viewport_active(void);
u16 smb_viewport_right(void);
void smb_viewport_origin(u8 object);
u8 *smb_viewport_sprite_x(unsigned offset);
void smb_viewport_bbox(u8 slot, u8 left, u8 right);
void smb_viewport_bbox_hide(u8 slot);
#endif

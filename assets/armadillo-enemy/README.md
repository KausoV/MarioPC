# Supplied armadillo: green walking Koopa presentation

The original 320×180,10-fps supplied video is preserved byte for byte. Source
frames17 through27 form one chronological 11-frame walk facing left. A fixed
isotropic224-square crop and62-square resize preserve proportions, bottom
registered at atlas row93 in a64×96 canvas. No per-frame fit, pose interpolation,
pingpong, crossfade or synthetic extra frames are used. Pink floor, shadow and
reflected magenta contour are removed; emerald armour and brown skin are kept.

The temporary shell presentation is a fixed polygon cut from the same supplied
armour in source17, without head, legs or tail. There is no new shell animation
in this supplied walk. The renderer uses this same static image for native
stationary/kicked/dead shell states and handles native +2px shell Y placement.

At native enemy speed8/16 pixel per tick, the measured planted paw requires
approximately57 cosmetic source frames/s for ground-relative foot motion. The
game still runs on the same60.0988-Hz native clock with unchanged enemy speed.
The source10-fps preview and native-tick-sampled runtime preview are separate.

Rebuild offline with Python,Pillow,NumPy and local FFmpeg/FFprobe:
`python tools/build_armadillo_assets.py --ffmpeg PATH`. The project copy in
original/ is selected automatically, so no external user download path is
needed after delivery. The game embeds generated RGBA and needs no build tools.

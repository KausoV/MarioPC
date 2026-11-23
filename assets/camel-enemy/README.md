# Camel/beetle Goomba presentation

The original supplied video is preserved byte for byte in `original/`.
Chronological source frames 82 through 109 form one 28-frame forward walk.
The supplied video is 24 fps. The cosmetic playback clock is 115 fps, sampled
at the game's native 60.0988-Hz presentation ticks. This preserves native enemy
movement while matching the planted hoof's backwards travel to body motion.
No ping-pong, pose interpolation or ghost crossfade is used. A fixed
square crop and isotropic scale preserve proportions. The shell stays in place
while the original legs and body move; frames are not independently fitted.

Green background, reflected contour green and cast floor shadow are removed.
Black pupils and armor joints are preserved. Walk art fits the native Goomba's
16 by 16 visible footprint inside a 16 by 24 OAM canvas. The stomp picture is
one bounded compression of this same creature. Native defeated/flip states,
movement, collisions, score and enemy timing remain the gameplay core's work.

Offline rebuilding requires local Python with Pillow and NumPy, plus FFmpeg
and FFprobe. Run `python tools/build_camel_enemy_assets.py --ffmpeg PATH` from
the project folder, where PATH points to the local FFmpeg executable; its
FFprobe companion must be alongside it. No source-download step is required.
The game embeds generated RGBA data and does not require these build tools.

Inspect `docs/camel-enemy/asset-manifest.json`, `asset-checks.json`, the complete
frame contacts, loop seam contact and loops for preparation evidence.
`cadence-checks.json` records the planted-hoof regression. The measured source
foot compensation is 6.255 native pixels/s at 24 fps, versus the unchanged
enemy's 30.049 pixels/s. Cosmetic 115-fps playback gives 29.972 pixels/s (about
0.26 percent residual measured slip). `camel-keyed-loop.gif` and `camel-loop.gif`
retain the original 24-fps reference; `camel-runtime-loop.gif` samples the
115-fps art clock at native game ticks, with cumulative GIF timestamp rounding.

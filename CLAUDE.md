# Lookout: notes for coding sessions

Phone-viewable camera + microphone app for the M5Stack Cardputer Zero (LVGL 9.5, C++17).
Sibling project of ~/projects/beamhound (same build setup and conventions).

- Build: `make cross` (Zig, glibc 2.36, static libc++), `make package`, `make deploy`. `make native`
  is a compile check only. Never compile on the device (it crashed twice from memory pressure).
- Test on device: stop APPLaunch (`systemctl --user stop APPLaunch`), run the binary, drive it from
  the PC with curl using the token in `~/.config/lookout/settings` on the device; capture the LCD
  with `cat /dev/fb0` (RGB565, stride 640, 320x170). Restart APPLaunch afterwards.
  `LOOKOUT_VIEW=qr|full` starts in that view.
- Layout: `src/platform/device.*` (keyboard, battery, backlight, IP, settings, child processes with
  PR_SET_PDEATHSIG), `src/media/media.*` (rpicam-vid MJPEG splitter, ffmpeg MP3), `src/net/server.*`
  + `page.hpp` (HTTP, token auth, viewer page), `src/app/app.*` (UI, start/stop media on demand),
  `src/app/preview.*` (libturbojpeg via dlopen).
- Audio: the mic is on the LEFT channel only and quiet; keep `pan=mono|c0=c0` and the gain stage.
- Waits in media sources must use real time even while the source is stopped (an early-return
  bug once made streams exit instantly before the camera/mic started).
- QR: light colour #C8C8C8 (pure white overexposes phone cameras) with quiet zone enabled.
- Version lives in app-builder.json only (`tools/bump_version.sh`). Commit as
  `Bryan Feller <6282600+bfeller@users.noreply.github.com>`.

<p align="center"><img src="assets/images/lookout_256.png" width="128" alt="Lookout logo"></p>

# Lookout

**See and hear through the M5Stack Cardputer Zero's camera from your phone.** A portable
security camera or an inspection camera for hard-to-see places. Scan the QR code on the
Cardputer and the live view opens in your phone's browser: no app, no account, and nothing
leaves your Wi-Fi network.

## Features

- MJPEG video to any browser (480p / 720p / 1080p), switchable from the phone
- MP3 audio from the microphone with +12/+24/+36 dB boost; the mic only runs while someone listens
- Snapshot, rotate and full-screen on the phone page
- Live preview on the Cardputer, full-screen view (V) and a large QR code (Enter)
- Sleep mode (S): screen off, camera only runs while someone is watching
- The Cardputer shows when someone is watching (LIVE n) or listening (mic LIVE)
- Private link: the QR carries a secret key; N makes a new key and disconnects everyone

## Keys

Enter big QR · V full-screen view · Q quality · M mic on/off · G mic boost · R rotate 180 ·
S sleep (any key wakes) · N new link · hold Esc to exit

## How it works

`rpicam-vid` captures MJPEG from the IMX219 camera and `ffmpeg` encodes the microphone (left
channel, high-pass, gain, limiter) to MP3. Lookout runs a small HTTP server: `/` is the viewer
page, `/video` a multipart MJPEG stream, `/audio` an MP3 stream, `/snapshot.jpg`, `/status` and
`/set`. Every request needs the secret key from the QR code (`?k=...`). The camera and mic run
only while needed, and child processes die with the app.

Measured on a Cardputer Zero (720p, one viewer + audio): about 60% of one core in total
(camera 37%, audio 16%, app 9%), 56 °C, 4.6 MB RAM, steady 12-13 fps indoors.

## Building

```sh
git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git deps/lvgl
make cross     # cross-compile with Zig (fetched via uv): build/cross/lookout
make package   # .deb in build/pkg/
make deploy    # install on a Cardputer over ssh (DEVICE=cardputer)
```

Don't build on the Cardputer itself (352 MB RAM). `LOOKOUT_VIEW=qr|full` opens a view
directly, for screenshots.

## Security notes

Video and audio travel unencrypted (HTTP) over your local network, protected by the secret key in
the link. Use it on networks you trust, and press N to revoke a link you've shared.

## License

MIT. See [LICENSE](LICENSE).

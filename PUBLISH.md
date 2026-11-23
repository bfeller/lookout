# Application Store Publish Record

Portal: <https://dev.cardputer.cc/#/upload>

## Package

- Debian package path: `build/pkg/lookout_0.1.0_arm64.deb` (built with `make package`)
- Package: `lookout`
- Version: `0.1.0`
- Architecture: `arm64`
- Maintainer: `Bryan Feller <6282600+bfeller@users.noreply.github.com>`
- APPLaunch desktop path: `/usr/share/APPLaunch/applications/lookout.desktop`
- Executable path: `/usr/share/APPLaunch/bin/lookout`
- Depends: `libc6 (>= 2.34), rpicam-apps-core, ffmpeg` (Recommends: `libturbojpeg0, pipewire-pulse`)

## Source and Ownership

- Public source repository: https://github.com/bfeller/lookout
- Expected GitHub uploader/owner: `bfeller`
- Hide uploader email: `true`

## Listing

- Application name: Lookout
- One-line summary (maximum 80 characters): See and hear through the camera from your phone: scan the QR, no app.
- Description: see `store.description` in `app-builder.json`
- Categories (maximum 6): Media, System Tools
- Share code: `LOOK`
- License: MIT
- Permissions: camera, microphone, network; everything else false

## Assets

- 256 x 256 source icon: `assets/images/lookout_256.png` (from `assets/images/lookout.svg`)
- Icon packaged in `.deb`: `usr/share/APPLaunch/share/images/lookout.png` (100 x 100)
- Screenshot 1 (320 x 170): `store/screenshots/1-main.png`
- Screenshot 2 (320 x 170): `store/screenshots/2-qr.png`
- Screenshot 3 (320 x 170): `store/screenshots/3-fullview.png`
- Screenshot 4 (320 x 170): `store/screenshots/4-idle.png`
- Screenshots were captured on the device with a throwaway access key (`ScreenshotDemoKey0`).

## Preflight

- [x] `.deb` exists and can be parsed.
- [x] Control fields contain Package, Version, Architecture, and Maintainer.
- [x] Architecture is appropriate for the package (`arm64` for native binaries).
- [x] APPLaunch `.desktop`, executable, and PNG icon exist inside the package.
- [ ] Package name ownership and update version are valid (checked by the portal at upload).
- [x] Listing copy and categories have been reviewed.
- [x] Four clean 320 x 170 baseline screenshots exist.
- [x] No credentials, tokens, or private server details are embedded.
- [x] `prepublish_check.py` passed (2026-11-22).
- [x] Tested on a Cardputer Zero (720p ~12.5 fps, audio, auth, 3-minute soak).
- [ ] The user explicitly approved external submission.

## Submission Result

- Status: `not submitted`
- Submitted at: TODO
- Portal message: TODO
- Actions URL: TODO
- Tracking or pull-request URL: TODO

# Releasing Lookout

Releases are built by GitHub Actions from a version tag. Submitting to the CardputerZero
Application Store is a short manual step (the store's developer portal signs in with your
GitHub account in the browser, so no store credentials live in this repo).

## What CI does

`.github/workflows/build.yml` runs on every push to `master`, every pull request, and every
`v*` tag:

1. Fetches LVGL v9.5.0 (cached).
2. Checks that the tag matches the version in `app-builder.json`.
3. Compiles a native build as a check; any compiler warning in `src/` fails the build.
4. Cross-compiles with Zig, builds `build/pkg/lookout_<version>_arm64.deb`, and checks the binary
   needs no glibc newer than 2.36 (the Cardputer Zero ships 2.41).
5. Runs the store's own `prepublish_check.py` against the package.
6. Uploads the `.deb` as a workflow artifact.
7. **On a tag only:** creates a GitHub Release named after the tag with the `.deb` attached.

## Cutting a release

```sh
tools/bump_version.sh 0.1.1            # updates app-builder.json (the only place the version lives)
make package && make deploy            # optional: build locally and try it on the Cardputer
git commit -am "Release v0.1.1"
git tag v0.1.1
git push && git push origin v0.1.1     # CI builds, checks and creates the GitHub Release
```

Then publish to the store:

1. Wait for the **build** workflow on the tag to go green; open the new release on GitHub and
   download `lookout_0.1.1_arm64.deb`.
2. Upload it at <https://dev.cardputer.cc/#/upload> (signed in as `bfeller`), source repository
   `https://github.com/bfeller/lookout`, keep "hide email" on. **Check the source field before
   submitting:** the portal can pre-fill the repo from your last upload of a different app (v0.1.0
   was recorded with bfeller/beamhound by mistake).
3. The portal opens a PR on `CardputerZero/packages`. The **first** release of a new app needs a
   short video of it running on a real device, posted as a comment on that PR; a maintainer then
   merges it. Later updates merge automatically once the store's checks pass.
4. Record the result (PR link, SHA-256) in `PUBLISH.md` and commit it.

The store rejects a version that is not newer than the published one, so always bump before
tagging. If CI fails on a tag, fix the problem, delete the tag
(`git tag -d v0.1.1 && git push origin :refs/tags/v0.1.1`), and tag again.

## Changing store listing details

The listing (summary, description, categories, screenshots, share code, permissions) comes from
the `store` section of `app-builder.json`; the portal imports it from the source repository.
Screenshots live in `store/screenshots/` and must be exactly 320 x 170; regenerate them with the
device: run the app with `LOOKOUT_VIEW=qr|full`, capture `/dev/fb0` (RGB565, 320x170), and use a throwaway `token=` in `~/.config/lookout/settings` so no real access key ends up in a published image.

## Before tagging

Lookout talks to real hardware, so CI can only check that it builds and packages correctly. Test
on a device before tagging: `make deploy`, open Lookout, scan the QR code with a phone, and check
video, Listen, snapshot, quality changes, sleep/wake (S) and a new link (N).


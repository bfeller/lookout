#!/bin/sh
# Set the app version (app-builder.json is the single source of truth; the Makefile reads it).
# Usage: tools/bump_version.sh 0.1.1
set -eu
new="${1:?usage: tools/bump_version.sh X.Y.Z}"
echo "$new" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "version must look like 1.2.3" >&2; exit 1; }
cd "$(dirname "$0")/.."
old=$(make -s version)
sed -i.bak "s/^  \"version\": *\"[^\"]*\"/  \"version\": \"$new\"/" app-builder.json && rm -f app-builder.json.bak
[ "$(make -s version)" = "$new" ] || { echo "failed to update app-builder.json" >&2; exit 1; }
echo "version: $old -> $new"
echo "next: git commit -am \"Release v$new\" && git tag v$new && git push && git push origin v$new"

#!/usr/bin/env bash
# release_linux.sh - the Linux AppImage "Panzer_Dragoon_Orta_Recomp-<version>-x86_64.AppImage" (a local file only,
# never committed). Since v1.3.0 the toolkit's tools/release/port_release_linux.sh does the work (it grew out of this
# script, option-c step 4; it also carries the APU module libxbox_apu.so and the LGPL source bundle) with Orta's names
# from tools/release_linux.conf. This wrapper adds Orta's own check: a release build has the F9 debug menu compiled out
# (configure the release build with -DXR_DEBUG_PANEL=OFF; option-c docs/porting_guide.md, release steps).
#
# Runs inside WSL (Ubuntu 24.04):
#   bash tools/release_linux.sh <version> [bundle dir with llvm/ + python/ (default ~/b_bundle/out)] [--dev]
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="${1:?version}"
shift
DEV=0; for a in "$@"; do [ "$a" = --dev ] && DEV=1; done
B=${RELEASE_BUILD:-$([ $DEV = 0 ] && echo ~/orta_linux/out_rel/build || echo ~/orta_linux/out/build)}
if [ $DEV = 0 ] && ! grep -q '^XR_DEBUG_PANEL:BOOL=OFF$' "$B/CMakeCache.txt"; then
  echo "build in $B has the F9 debug menu - configure it with -DXR_DEBUG_PANEL=OFF"; exit 1; fi
exec bash ~/orta_linux/toolkit/tools/release/port_release_linux.sh "$HERE/release_linux.conf" "$VERSION" "$@"

#!/bin/bash
#
# Build the Windows installer for a version that make-release.sh has already
# packaged. The zip stays the real package - this wraps the exact same files so
# that somebody who would rather not copy things into a game folder does not
# have to.
#
# usage:  ./make-installer.sh 0.11
#
# Needs Inno Setup 6 (winget install JRSoftware.InnoSetup).

set -e

VERSION="$1"
if [ -z "$VERSION" ]; then
	echo "usage: $0 <version>   e.g. $0 0.11" >&2
	exit 1
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
PAYLOAD="$HERE/release/arx-coop-$VERSION"
OUT="$HERE/release/arx-coop-$VERSION-setup.exe"

# The installer wraps what was packaged, never a fresh build: whatever people
# downloaded as a zip and whatever they get from the installer must be the same
# bytes, or a bug report about one tells you nothing about the other.
if [ ! -d "$PAYLOAD" ]; then
	echo "no packaged build at $PAYLOAD" >&2
	echo "run ./make-release.sh $VERSION first" >&2
	exit 1
fi

if [ ! -f "$PAYLOAD/arx.exe" ]; then
	echo "$PAYLOAD has no arx.exe in it" >&2
	exit 1
fi

if [ -f "$OUT" ]; then
	echo "$OUT already exists." >&2
	echo "A published installer is never replaced - bump the version instead." >&2
	exit 1
fi

# Inno installs per-user by default, so it is not on PATH.
ISCC=""
for candidate in \
	"$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe" \
	"/c/Users/$USER/AppData/Local/Programs/Inno Setup 6/ISCC.exe" \
	"/c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
	"/c/Program Files/Inno Setup 6/ISCC.exe"
do
	if [ -f "$candidate" ]; then
		ISCC="$candidate"
		break
	fi
done

if [ -z "$ISCC" ]; then
	echo "Inno Setup 6 not found." >&2
	echo "  winget install JRSoftware.InnoSetup" >&2
	exit 1
fi

echo "building installer for $VERSION"
echo "  payload: $(ls "$PAYLOAD" | wc -l) files"

"$ISCC" //DVersion="$VERSION" "$(cygpath -w "$HERE/installer/arx-coop.iss")" | tail -3

if [ ! -f "$OUT" ]; then
	echo "Inno reported success but produced no $OUT" >&2
	exit 1
fi

SIZE=$(du -h "$OUT" | cut -f1)
echo
echo "ready: $OUT ($SIZE)"

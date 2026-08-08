#!/bin/bash
#
# Package a release someone can actually use: extract, double click, play.
#
# The build itself needs MSYS2, but nobody downloading this should. So every
# library the executable asks for is copied in beside it - and the list is not
# written by hand, it is read out of the binary, because a hand written list
# goes stale the moment a dependency changes and the failure it produces is a
# silent "this application was unable to start correctly" that tells the player
# nothing.
#
# usage: ./make-release.sh [version]

set -e

VERSION="${1:-0.6}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/release/arx-coop-$VERSION"
MINGW="/c/msys64/mingw64/bin"

if [ ! -f "$HERE/build/arx.exe" ]; then
	echo "build/arx.exe is missing - build it first"
	exit 1
fi

echo "packaging arx-coop-$VERSION"
rm -rf "$OUT"
mkdir -p "$OUT"

cp "$HERE/build/arx.exe" "$OUT/"

# Ask the binary what it needs, then follow the chain: the libraries have
# libraries of their own, and missing one of those fails just as hard.
echo "  collecting libraries..."
collect() {
	local target="$1"
	ldd "$target" 2>/dev/null | grep -io "$MINGW/[^ ]*\.dll" | while read -r dll; do
		local name
		name="$(basename "$dll")"
		if [ ! -f "$OUT/$name" ]; then
			cp "$dll" "$OUT/"
			collect "$OUT/$name"
		fi
	done
}
collect "$OUT/arx.exe"

COUNT=$(ls "$OUT"/*.dll 2>/dev/null | wc -l)
echo "  $COUNT libraries"

# What a player needs to know, in the folder rather than on a web page they
# will not have open when it goes wrong.
cat > "$OUT/READ ME FIRST.txt" << 'EOF'
Arx Fatalis Co-op
=================

WORK IN PROGRESS - EXPECT ANYTHING.


To play
-------

1. You need to own Arx Fatalis (GOG or Steam). This has no game content in it
   and will not run without your copy.

2. Double click arx.exe.

That is all. It looks for Arx Fatalis by itself and usually finds it.

If it cannot, it will say so and list where it looked. Drag your Arx Fatalis
folder onto arx.exe, or run:

    arx.exe -d "C:\GOG Games\Arx Fatalis"


To play together
----------------

One of you hosts: open CO-OPERATIVE PLAY, press HOST GAME, and give your
friend your IP address. The port is usually opened on your router
automatically, so most people need to set up nothing.

The other joins: type that address, press JOIN GAME.

You can join at any point in the game - you turn up wherever the host is.
But if you join partway through for the first time your character is still
level 1 and will be somewhere it is not ready for, so starting together from
the beginning is much better.

If it will not connect, some internet providers put you behind a second
router you cannot open ports on. Both install Radmin VPN (free), join the
same network, and use the 26.x.x.x address it gives you.


Voice chat
----------

Tick VOICE CHAT in the co-op menu and hold V to speak. Your voice comes out
of your character, so it fades with distance.

Use MIC TEST first. If the meter does not move when you talk, click MIC: to
try another microphone - most machines have several and only one is real.


To remove it
------------

Delete this folder. Your Arx Fatalis installation was never touched.

Your saves and settings live in:
    C:\Users\<you>\Saved Games\Arx Libertatis\
Note that ordinary Arx Libertatis uses that folder too.


Something broke?
----------------

Very likely. Please say so:
    https://github.com/kingzmanh/arx-fatalis-coop/issues

The useful things to include are what you were doing, which player you were,
and this file:
    C:\Users\<you>\Saved Games\Arx Libertatis\arx.log


Built on Arx Libertatis (https://arx-libertatis.org/), which is why any of
this is possible. GPL v3 - the source is on the GitHub page above.

Not affiliated with Arkane Studios or Bethesda.
EOF

cp "$HERE/LICENSE" "$OUT/LICENSE.txt"
cp "$HERE/CREDITS.md" "$OUT/CREDITS.txt"

ZIP="$HERE/release/arx-coop-$VERSION-windows.zip"
rm -f "$ZIP"
if command -v powershell >/dev/null 2>&1; then
	powershell -NoProfile -Command \
		"Compress-Archive -Path '$(cygpath -w "$OUT")\*' -DestinationPath '$(cygpath -w "$ZIP")' -Force" \
		>/dev/null 2>&1
fi

echo
if [ -f "$ZIP" ]; then
	echo "ready: $ZIP ($(du -h "$ZIP" | cut -f1))"
else
	echo "ready: $OUT (zip it yourself)"
fi

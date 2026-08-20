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

# A published build is never replaced. Someone is running the last one, and if
# this release breaks something for them the only way back is the old download
# still being there. Bump the version instead - that is what versions are for.
#
# release/backup is checked as well as release itself: packaged zips are filed
# away there once published, and a build that has been tidied out of sight is
# still a build somebody downloaded. Tidying must not quietly make it
# overwritable.
if { [ -f "$HERE/release/arx-coop-$VERSION-windows.zip" ] \
     || [ -f "$HERE/release/backup/arx-coop-$VERSION-windows.zip" ]; } \
   && [ "$2" != "--replace" ]; then
	echo "arx-coop-$VERSION-windows.zip already exists."
	echo "Bump the version, or pass --replace if it was never published."
	exit 1
fi

if [ ! -f "$HERE/CHANGELOG.md" ]; then
	echo "CHANGELOG.md is missing - every release says what changed and what is"
	echo "still broken, so nobody has to guess which build they are on."
	exit 1
fi

if ! grep -q "^## $VERSION$" "$HERE/CHANGELOG.md"; then
	echo "CHANGELOG.md has no '## $VERSION' section."
	echo "Write what changed, what was fixed and what is still known broken first."
	exit 1
fi

echo "packaging arx-coop-$VERSION"
rm -rf "$OUT"
mkdir -p "$OUT"

# arx.exe, the same name the game uses.
#
# One executable, so nobody has to be told which one to run - the mod IS the
# game once it is installed. The player's own arx.exe is kept as
# arx-vanilla.exe and still runs: the installer renames it before copying, and
# a zip cannot, so the read me asks for that one step by hand.
cp "$HERE/build/arx.exe" "$OUT/arx.exe"

# What changed, what was fixed, and what is still broken - in the folder, so a
# player who never opens the repository still knows which build they have.
cp "$HERE/CHANGELOG.md" "$OUT/WHAT CHANGED.txt"

# Ask the binary what it needs, then follow the chain: the libraries have
# libraries of their own, and missing one of those fails just as hard.
echo "  collecting libraries..."
collect() {
	local target="$1"
	# Both spellings of the same folder: ldd writes /mingw64/bin inside MSYS2
	# and /c/msys64/mingw64/bin outside it. Matching only one of them finds
	# nothing in the other shell - and finding nothing is not an error to any
	# of the commands here, so it packages an executable that cannot start.
	ldd "$target" 2>/dev/null \
		| grep -ioE "(/[a-z]/msys64)?/mingw64/bin/[^ ]*\.dll" | while read -r dll; do
		case "$dll" in
			/mingw64/*) dll="/c/msys64$dll" ;;
		esac
		local name
		name="$(basename "$dll")"
		if [ ! -f "$OUT/$name" ] && [ -f "$dll" ]; then
			cp "$dll" "$OUT/"
			collect "$OUT/$name"
		fi
	done
}
collect "$OUT/arx.exe"

COUNT=$(ls "$OUT"/*.dll 2>/dev/null | wc -l)
if [ "$COUNT" -eq 0 ]; then
	echo
	echo "no libraries were collected."
	echo "ldd found none beside $MINGW - the executable in this package would"
	echo "not start on anybody's machine, so it is not a package. Run this from"
	echo "the MSYS2 shell the game was built in."
	exit 1
fi
echo "  $COUNT libraries"

# Arx Libertatis' own data, which is not part of the game and not optional.
# The fonts here are what the interface draws its icons and text with; without
# them the game starts and then complains it cannot find them. Easy to miss,
# because a machine that has ever had Arx Libertatis installed already has them
# sitting somewhere it looks.
echo "  copying engine data..."
mkdir -p "$OUT/data"
cp -r "$HERE/data/core/"* "$OUT/data/"
# The spells, which are ordinary game content: they ship with the mod, they
# work for everyone who installs it, and they stay a text file anyone can open
# and change. The installer keeps a copy the player has edited rather than
# writing over it, and leaves the version we shipped beside it to compare.
if [ -f "$HERE/game/data/game/studio-spells.txt" ]; then
	mkdir -p "$OUT/data/game"
	cp "$HERE/game/data/game/studio-spells.txt" "$OUT/data/game/"
	echo "  spells: $(grep -c '^spell ' "$OUT/data/game/studio-spells.txt")"
fi

echo "  $(find "$OUT/data" -type f | wc -l) files"

# What a player needs to know, in the folder rather than on a web page they
# will not have open when it goes wrong.
cat > "$OUT/READ ME FIRST.txt" << 'EOF'
Arx Fatalis Co-op
=================

WORK IN PROGRESS - EXPECT ANYTHING.


To play
-------

You need to own Arx Fatalis (GOG or Steam). This has no game content in it and
will not run without your copy.

1. Find your Arx Fatalis folder. It is the one with data.pak in it.

   Steam:  right click the game, Manage, Browse local files
   GOG:    usually C:\GOG Games\Arx Fatalis

2. Rename the arx.exe already in that folder to arx-vanilla.exe.

   That is your own game, and it still works under the new name - this
   is only so you can go back whenever you like. The installer does
   this step for you; a zip cannot, so it is yours to do.

3. Copy everything from this zip INTO that folder.

4. Double click arx.exe.

Putting it in the game's own folder is the point - that is how it finds your
copy of the game. Dropping it somewhere else and running it will usually fail
to find anything.

Only arx.exe is replaced, and step 2 kept yours under another name.
Everything else is added beside the game's own files.


Spells
------

The co-op spells are in a text file, not in the program:

    data\game\studio-spells.txt

Open it in Notepad. Every spell is a block of lines, and the "runes"
line is the sequence you draw to cast it - change it to whatever you
like from the list at the top of that file, save, and start the game.

If you pick runes another spell already uses, that spell is left out
rather than fighting over them, and arx.log says which one has them.

Updating the mod keeps your version of this file. The one that came
with the new version is left beside it as studio-spells-default.txt.


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

Delete the files this zip added to your game folder - the .dll files,
the data folder, and these text files - then delete arx.exe and rename
arx-vanilla.exe back to arx.exe. Do NOT delete the folder itself, it is
your game.

On Steam you can also just verify the files and it will tidy up.

Nothing else was changed. The game itself is untouched.

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

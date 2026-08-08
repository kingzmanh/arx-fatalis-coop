#!/bin/bash
#
# Every co-op fix, and the fingerprint that proves it is still in the source.
#
# The failure that cost this project weeks was never a hard bug: it was a fix
# quietly disappearing - rolled back with a backup, overwritten by a restore,
# or edited out while working on something else - and nobody noticing until it
# was found again by playing. This file is the memory. Run it before every
# build. If a line says MISSING, that behaviour is broken RIGHT NOW, whatever
# the last thing you changed was.
#
# Adding a fix? Add a line here in the same breath. A fix that is not
# registered is a fix that will be lost.
#
# usage:  ./coop-check.sh          list everything
#         ./coop-check.sh -q       only what is missing (exit 1 if any)

SRC="$(dirname "$0")/src"
QUIET=0
[ "$1" = "-q" ] && QUIET=1

pass=0
fail=0

# name | file | pattern that must be PRESENT
check() {
	local name="$1" file="$2" pattern="$3"
	if grep -q -- "$pattern" "$SRC/$file" 2>/dev/null; then
		pass=$((pass + 1))
		[ $QUIET -eq 0 ] && printf "  \033[32mok\033[0m      %s\n" "$name"
	else
		fail=$((fail + 1))
		printf "  \033[31mMISSING\033[0m %-46s (%s)\n" "$name" "$file"
	fi
}

# name | file | pattern that must be ABSENT
checkgone() {
	local name="$1" file="$2" pattern="$3"
	if grep -q -- "$pattern" "$SRC/$file" 2>/dev/null; then
		fail=$((fail + 1))
		printf "  \033[31mBROKEN\033[0m  %-46s (%s)\n" "$name" "$file"
	else
		pass=$((pass + 1))
		[ $QUIET -eq 0 ] && printf "  \033[32mok\033[0m      %s\n" "$name"
	fi
}

# name | file | pattern | how many times it must appear
checkcount() {
	local name="$1" file="$2" pattern="$3" want="$4"
	local got
	got=$(grep -c -- "$pattern" "$SRC/$file" 2>/dev/null)
	if [ "$got" = "$want" ]; then
		pass=$((pass + 1))
		[ $QUIET -eq 0 ] && printf "  \033[32mok\033[0m      %s\n" "$name"
	else
		fail=$((fail + 1))
		printf "  \033[31mMISSING\033[0m %-46s (%s: found %s, need %s)\n" "$name" "$file" "$got" "$want"
	fi
}

[ $QUIET -eq 0 ] && echo "--- travel and areas"
checkgone "script timers run on replicas too"        core/ArxGame.cpp     "worldIsRemote) {\s*ARX_SCRIPT_Timer_Check"
check     "guest reports its own zone crossings"     net/CoopNet.cpp      "MsgZoneEnter"
check     "zone crossings run in the partner's name" net/CoopNet.cpp      "ScopedPlayerContext context(body)"

[ $QUIET -eq 0 ] && echo "--- items and identity"
check     "guest mints items in a private id range"  net/CoopPlayer.cpp   "GuestItemInstanceBase"
check     "audit never reports our own belongings"   net/CoopWorld.cpp    "isOwnBelonging"
check     "a taken item leaves the world registry"   net/CoopNet.cpp      "forgetReplicatedEntity"
checkcount "dragged items are never dragged back"    net/CoopWorld.cpp    "g_draggedEntity" 2
check     "a dropped item is launched, not placed"   net/CoopWorld.cpp    "EERIE_PHYSICS_BOX_Launch"
check     "the release impulse travels with a drop"  net/CoopNet.cpp      "writer.put(velocity)"

[ $QUIET -eq 0 ] && echo "--- combat"
check     "creatures block and can be fought"        physics/Collisions.cpp "lagDriven"
check     "creatures can reach the other player"     game/NPC.cpp         "coop::isAvatarEntity(target)"
check     "a struck creature turns on its attacker"  net/CoopPlayer.cpp   "scriptContextPlayer()"
check     "weapons never wear against a companion"   game/Equipment.cpp   "coop::isAvatarEntity(target)"

[ $QUIET -eq 0 ] && echo "--- what the other player looks like"
check     "their weapon is whatever they hold"       net/CoopPlayer.cpp   "classPath().string()"
check     "their health is a second life orb"        gui/Hud.cpp          "drawPartnerHealthOrb"

[ $QUIET -eq 0 ] && echo "--- menu"
check     "replicas recompute which room they are in" net/CoopWorld.cpp "requestRoomUpdate"
check     "the port is optional"                     gui/MainMenu.cpp     "PORT ARE DISABLE"
check     "voice carries from the body, not the ear" net/CoopVoice.cpp    "AL_REFERENCE_DISTANCE"
check     "the microphone can be tested alone"       net/CoopVoice.cpp    "g_testing"

echo
if [ $fail -eq 0 ]; then
	printf "\033[32mall %d fixes present\033[0m\n" "$pass"
else
	printf "\033[31m%d MISSING, %d present\033[0m - the missing ones are broken right now\n" "$fail" "$pass"
fi
exit $fail

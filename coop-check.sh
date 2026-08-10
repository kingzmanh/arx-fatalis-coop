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
# Set where the crossing is HANDLED rather than where it is received, so it
# covers both machines' own crossings and the guest's reported one alike.
check     "zone crossings run in the partner's name" ai/Paths.cpp         "ScopedPlayerContext context(io)"

check     "a guest with no area still travels"       net/CoopNet.cpp      "Standing nowhere is a reason to travel"

check     "co-op memory travels with the savegame"  net/CoopNet.cpp      "void saveSideState"
checkgone "the story ledger is not a loose file"     net/CoopNet.cpp      'fopen("coop-story.txt"'
checkgone "the playthrough id is not a loose file"   net/CoopNet.cpp      'fopen("coop-guid.txt"'

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
# ...and KEEPS turning on them. The aggro pass re-runs every frame from sight
# and distance alone, so without a remembered grudge it handed the creature
# straight back to whoever stood nearer - the one not fighting it.
check     "and keeps facing them for a while"        net/CoopPlayer.cpp   "GrudgeWindow"
check     "every hit records who threw it"           game/Damage.cpp      "coop::noteAttacker"

[ $QUIET -eq 0 ] && echo "--- what stands out"
# HALO -o is how the game says "this one matters" - a quest item, the thing you
# were just asked to find. It is set by a script, scripts run on the authority,
# and it was missing from the snapshot, so the other player saw plain scenery.
check     "a glow put on by a script travels"        net/CoopWorld.cpp    "halo_native.flags"
# ...and is promoted to the copy the renderer reads. The two are joined only by
# ARX_HALO_SetToNative, which nothing calls on this side for a remote glow, so
# setting the native value alone was invisible until something else touched it.
check     "and is the copy actually drawn"           net/CoopWorld.cpp    "ARX_HALO_SetToNative(entity)"

check     "weapons never wear against a companion"   game/Equipment.cpp   "coop::isAvatarEntity(target)"

[ $QUIET -eq 0 ] && echo "--- what the other player looks like"
# A player will wield anything, and most things are not filed under weapons -
# a bone is a provision. The full path travels and the receiver builds from it
# directly, instead of Prepare_SetWeapon guessing the weapons folder.
check     "their weapon is whatever they hold"       net/CoopPlayer.cpp   "out.weapon = weapon->classPath().string()"
checkgone "the held item is not looked for in weapons/" net/CoopPlayer.cpp "Prepare_SetWeapon(body"
check     "their health is a second life orb"        gui/Hud.cpp          "drawPartnerHealthOrb"
# A shield is neither worn nor wielded: there is no tweak to apply and no slot
# on the body to keep it in, so it is a linked entity only one pointer knows of.
check     "their shield hangs off their arm"          net/CoopPlayer.cpp   "shield_attach"
# ANIM_WAIT_SHORT is not a short wait: player.asl binds it to player_wait_1st,
# the first person idle. Which camera we use must not change what they see.
check     "they idle in third person, whatever we use" net/CoopPlayer.cpp  "u8(ANIM_WAIT)"
# The shield hold is a LOOPING clip on layer 3, and only exists while a shield
# is equipped. Sent with no flags it stopped looping; sent with a playhead of 0
# it was dragged back to the start every second. Every layer carries both.
checkgone "the shield hold is not sent timeless"     net/CoopPlayer.cpp   "anim3, 0, 0)"
check     "every animation layer carries its clock"  net/CoopPlayer.cpp   "g_avatar.anim3Flags, g_avatar.anim3Time"

[ $QUIET -eq 0 ] && echo "--- shared knowledge"
# A rune is knowledge, not an object: both keep the union, and nothing removes.
check     "runes learned by either are learned by both" game/Player.cpp    "coop::reportRunes()"
check     "runes are shared on meeting, not only on learning" net/CoopNet.cpp "reportRunes();"

[ $QUIET -eq 0 ] && echo "--- cutscenes"
# A lock is only safe where the thing that lifts it can run. SENDEVENT queues an
# event, and the queue is drained only where the area is simulated, so a guest
# that locked itself could never reach its own SET_PLAYER_CONTROLS ON.
check     "a guest never locks itself for a cutscene" script/ScriptedPlayer.cpp "coop::isReplica()"
check     "the bars stay up only for the viewer copy" script/ScriptedCamera.cpp "enable && coop::isReplica()"
# The zone disarms for the world, partner or not: the host runs the whole script
# in their name, so leaving the trigger armed re-runs a cutscene whose cameras
# the first run destroyed - and the early return skipped consuming its argument.
checkgone "a one-shot zone disarms for both players" script/ScriptedAnimation.cpp "isPartnerScriptContext"
# The SHOW that undoes a HIDE is at the end of the same unreachable chain, so a
# guest that hid its interface never gets a cursor back. Only hiding is refused.
check     "a guest keeps its cursor and its hands"   script/ScriptedInterface.cpp 'command == "hide" && (coop::isReplica() || !coop::presentsCutscene())'
# Whoever trips a story moment, the host performs it - bars, hands, locked
# controls and all - and sends the guest a viewer copy. Bowing out because the
# OTHER player set it off left the scene playing on neither machine.
checkgone "the host performs a scene it did not trip" script/ScriptedPlayer.cpp "their machine owns"
check     "and the guest is sent a copy to watch"    script/ScriptedConversation.cpp "coop::relaysCutscene()"
# A scene belongs to whoever walked into it. Declining it here has to leave the
# signal that says a story moment is running, or it would be shown to nobody and
# sent to nobody either - which is exactly how it broke the first time.
check     "a scene goes to the one who tripped it"   net/CoopNet.cpp      "return !isPartnerScriptContext();"
check     "declining it still marks it a scene"      script/ScriptedPlayer.cpp "coop::noteCutsceneForPartner(!enable)"
check     "so it is still worth sending them"        script/ScriptedConversation.cpp "coop::isPartnerCutscene()"
check     "and a guest never performs one itself"    script/ScriptedPlayer.cpp "if(coop::isReplica())"
# The view is most of what a cutscene IS. Bars, dead controls and hidden hands
# all went to whoever the scene belonged to; the camera did not, so a scene
# declined here still took this screen's eyes and pointed them at the show.
check     "a declined scene does not take our eyes"  script/ScriptedCamera.cpp "if(ours) {"
check     "and the watcher is given the same view"   net/CoopNet.cpp      "reportCutsceneCamera"
check     "with their own eyes given back after"     net/CoopNet.cpp      "g_viewerCamera = false;"
# SENDEVENT does not call, it queues, and a story moment is mostly these hops:
# the goblin sends the camera an event and the camera takes the view. Drained
# from the top of the frame, nothing remembered whose scene it was.
check     "a queued event remembers whose it was"    script/Script.cpp    "entry.partnerContext = coop::isPartnerScriptContext()"
check     "and is run in that name when it fires"    script/Script.cpp    "event.partnerContext ? coop::avatarEntity()"

[ $QUIET -eq 0 ] && echo "--- giving things away"
# Handing an item over is how the quests move, and the quest lives with the
# world. Run locally by a guest, the goblin takes the form on one screen and
# refuses it on the other.
check     "a give is made where the quest lives"     gui/Interface.cpp    "coop::requestCombine"
check     "and the item only then leaves the pack"   net/CoopWorld.cpp    "applyCombineTaken"
# "Give it to the player" means the one who earned it, and the paying half of
# these scripts usually runs after the line is spoken, long after the call that
# knew whose errand it was has returned.
check     "the reward follows the giver"             script/ScriptedInventory.cpp "coop::giveToPartner"
check     "even when it is paid after the speech"    gui/Speech.cpp       "forPartner ? coop::avatarEntity()"

[ $QUIET -eq 0 ] && echo "--- breathing"
# Idle breathing is not a looping clip: it is played once and started again by
# hand. That restart changes nothing about WHAT is playing, so the snapshot
# left it out and the replica, forbidden from winding a one-shot back, held the
# last frame forever.
check     "a clip started over is worth sending"     net/CoopWorld.cpp    "20 < a.animTime"
check     "and the replica is allowed to follow"     net/CoopWorld.cpp    "!(layer.flags & EA_LOOP) && !restarted"

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

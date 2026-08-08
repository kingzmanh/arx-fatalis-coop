# Fix log

Every problem that has been found **and confirmed fixed**, written down so it
does not have to be worked out twice.

Three parts to every entry:

- **The problem** - the symptom, as it was actually seen.
- **Why did it happen?** - always with the question mark. This is the part most
  likely to be wrong, so it stays open to challenge. If a related bug turns up
  later, come back and argue with it.
- **The fix** - what actually changed.

**Nothing goes in here until the fix is certain.** A log full of maybes is worse
than no log.

---

## Player 2 stranded after coming through a hole

**The problem.** The second player would arrive in a new area and simply stop -
no control, no fall, stuck at the arrival point. Confirmed by the user.

**Why did it happen?** Script timers were not being run on the replica. Arx
levels drive their own arrival rituals through those timers: a hole arrival
teleports the player via a 50 ms timer, and fades and invulnerability windows
work the same way. With timers muted, the arrival sequence started and never
finished.

**The fix.** `ARX_SCRIPT_Timer_Check()` now runs on replicas too, in
`src/core/ArxGame.cpp`.

---

## Any creature touching player 2 launched them across the room

**The problem.** A rat brushing against the second player would fire them into
the far wall.

**Why did it happen?** A feedback loop of my own making. A per-frame
`sendPlayerPush` message pushed the player, which changed the separation
distance, which sent another push, and so on - each frame compounding the last.
The creature was not the cause; the message was.

**The fix.** `sendPlayerPush` removed entirely (`MsgPlayerPush = 74` stays
reserved so old builds cannot misread the number). The creature now takes 100%
of the separation, which is what the single-player engine already did.

---

## Enemies could not reach player 2

**The problem.** Creatures would approach the second player, then mill about
just out of range, unable to close the last step.

**Why did it happen?** `ComputeTolerance` measured the avatar body as radius 0.
The pathfinder therefore aimed at a point rather than a body, and the arrival
test could never be satisfied.

**The fix.** Use `ARXCHARACTER::baseRadius()` for the avatar, the same figure
the engine uses for the real player.

---

## An item in player 2's hands was destroyed when player 1 used theirs

**The problem.** Both players holding the same kind of item; one player's item
breaking or being used destroyed the other's.

**Why did it happen?** An entity id collision. Ids are the names both machines
use for an entity, and both machines had independently created something called
`bone_0001`. A message about one arrived and was applied to the other. Proven at
the time by the `[coop-item]` log lines showing the same id on both sides.

**The fix.** A private id range for guest-created entities, so the two machines
cannot invent the same name.

---

## Player 1 had cutscenes skipped that they had never seen

**The problem.** Conversations and story sequences were being skipped for the
host, including on a fresh playthrough.

**Why did it happen?** The story ledger - the record of what has already been
watched - was a single global file living beside the executable. It was shared
across every playthrough, so a sequence seen once was marked seen forever.

**The fix.** The ledger moved inside the savegame folder, keyed to the
playthrough.

---

## An NPC was invisible on player 2's screen

**The problem.** A creature clearly present for the host could not be seen at
all by the guest. Confirmed fixed by the user.

**Why did it happen?** A stale portal room. Arx clips what it draws by which
room an entity is in, and that room is normally recomputed in the AI tick. On a
replica the AI tick does not run, so a replicated entity kept whatever room it
was first assigned and got clipped away.

**The fix.** Recompute `UpdateIORoom` whenever a replicated entity moves.

---

## The partner's weapon was invisible in their hands

**The problem.** The other player's equipped weapon did not appear.

**Why did it happen?** Only the folder name was being sent, not the full class
path, so the receiving side could not find the model to load.

**The fix.** Send the complete class path.

---

## Port forwarding asked the VPN to open the port, not the router

**The problem.** libplum was added so a host's router would open the co-op port
by itself. On the first live test against the real router it failed outright -
no port opened, by any of the three protocols it speaks.

**Why did it happen?** Two separate faults in libplum, both the same underlying
mistake: it assumes the machine has one way out to the internet.

1. `net_get_default_gateway` walked the Windows routing table and returned the
   **first** default route it found, ignoring the metric. This machine has two -
   Radmin VPN's at metric 9257 and the real router's at metric 20 - and the VPN's
   was listed first. Proven by the debug log: `Probing gateway at 26.0.0.1:5351`,
   which is Radmin's network, while the real router is 192.168.4.1.
2. The UPnP discovery socket binds to `INADDR_ANY` and never sets
   `IP_MULTICAST_IF`, so the SSDP search left by whichever interface the routing
   table offered first - again the VPN. Proven by sending the identical SSDP
   search by hand: bound to the LAN adapter it drew 24 answers from
   192.168.4.1 advertising `http://192.168.4.1:1900/igd.xml`; bound the way
   libplum does it, zero. The router had UPnP enabled the whole time.

   Setting `IP_MULTICAST_IF` alone was then tested on its own, with the socket
   still bound to `INADDR_ANY`, and drew the same 24 answers - so that one
   socket option is the whole fix, not the bind.

**The fix.** Both in `thirdparty/libplum`, kept as
`thirdparty/libplum-multihomed.patch` against upstream 0.6.0:

1. `net_get_default_gateway` now asks `GetBestRoute2` which route Windows would
   really use, which applies metrics and interface priority the same way an
   outgoing packet does. The old table walk stays as a fallback, but now picks
   the lowest metric rather than the first row, and frees the table on every
   path out - the original leaked it on success.
2. The UPnP socket sets `IP_MULTICAST_IF` to the default interface.

After both, the mapping succeeds: `SUCCESS via UPnP, public address
192.168.118.207:38595, forwarded to local port 27100`.

This matters more than it looks. The people most likely to try this mod are the
ones who already have Radmin VPN installed - which is exactly the case that
broke it.

---

## Items snapped back when the second player dragged them quickly

**The problem.** The second player picks up an item and drags it. Moved slowly
it behaves; moved quickly it jumps back towards where it started. Found and
described by the user, including the detail that made it solvable: "when I give
them like more split sec they work fine".

**Why did it happen?** `smoothReplicatedEntities()` pulls every replicated
entity towards the position the authority last reported, once a frame. The
guard that excludes the item currently in the player's hand existed at only one
of the two places it was needed - `coop-check.sh` had been reporting exactly
this for some time as "dragged items are never dragged back (found 1, need 2)",
having been lost in the rollback between backups 0.3 and 0.4.

While an item is being dragged, the authority has not yet heard about it, so its
idea of where the item is remains wherever it was picked up from. Every frame
the correction pulled it back there.

The speed is what proves it. The correction was always happening; it only became
visible once the cursor moved far enough from the remembered position in a
single frame for the pull-back to be worth more than a pixel. A slow drag stays
inside that margin and looks perfect.

**The fix.** `smoothReplicatedEntities()` now skips the dragged entity outright
rather than correcting it (`src/net/CoopWorld.cpp`). Confirmed working by the
user.

---

## A creature inside the cell was only a shadow to the second player

**The problem.** The guard walks into the cell. The host sees him. The guest
sees a shadow on the floor and nothing casting it, and only catches sight of the
guard himself when very close and at certain angles. Outside the cell he is
visible normally. Found by the user.

**Why did it happen?** Arx only draws something if the room it is in can be seen
from the room the camera is in, and an entity's room is recalculated by the
movement code in `NPC.cpp` - which never runs on the guest, because there a
creature is a replica whose position simply arrives. Nothing on the guest ever
raised `requestRoomUpdate`, so the guard kept whichever room he was in when
first seen. Walking into the cell put him somewhere his recorded room said he
was not, and the portal test discarded him. The shadow survived because it is
drawn by a path that never asks about rooms.

The "close, and at certain angles" part is the same cause: those are the
positions from which the stale room happens to be visible anyway.

**The fix.** Replicated entities now request a room update when they move, in
`smoothReplicatedEntities()`. Only after moving a real distance rather than
every frame - working out a room means searching the level geometry, and asking
for that on every replica on every frame cost enough to be felt as lag.
Confirmed by the user.

---

## Dropped items hung in the air, or snapped across the room

**The problem.** Items misbehaved for the second player in two ways that turned
out to be one story. Dragging quickly made them snap back; and once that was
stopped, dropped items hung motionless in mid-air instead of falling.

**Why did it happen?** The guest never ran the physics at all.
`ARX_PHYSICS_Apply()` is called only when the world is not remote, so on the
guest a dropped item had its physics started and then nothing ever stepped it.

The snapping was this same fault wearing a disguise. The item hung where it was
released, the authority's copy fell to the floor, and the next snapshot moved
the guest's copy to where the authority's had landed - which looked like a
snap, and was actually the only thing making dropped items appear to fall at
all. Stopping the snap revealed what had always been underneath.

**The fix.** Three layers, because one was not enough:

1. **You own what you touch.** An item being carried, dragged, or still
   settling after being thrown is never corrected from the other side.
2. **You simulate what you own.** `ARX_PHYSICS_Apply()` now runs on the guest
   too, over owned items only. Both machines run the same fall from the same
   impulse and land in nearly the same place.
3. **Nothing is ever teleported into place if it can be walked there.** The one
   path that moved a replica without blending now starts its timeline from where
   the entity currently appears, so what is left over glides instead of jumping.

Confirmed by the user.

---

## Joining sometimes gave the second player a black screen

**The problem.** Sometimes the guest joined and saw nothing but black, and the
only way out was to close the game and join again. Sometimes it worked. Found by
the user.

**Why did it happen?** The gate that walks a freshly joined guest to the host
required the guest to already be standing in an area of its own. Usually it is -
but when the join finished before the guest's own level had loaded, it never
would be, and the one condition being waited on was the one that could no longer
happen.

Caught in the log as it happened: `travel gate: playing=1 ingame=1 area=-1
remote=1 teleportPending=0` repeating every two seconds forever, with
`[coop-render] polys=19 campos=0,0,0 camroom=inv area=inv`. Connected, in game,
knows exactly where the host is, no level loaded, camera at the origin, nineteen
polygons on screen. That is the black screen.

The race is why it only happened sometimes.

**The fix.** Standing nowhere is a reason to travel, not a reason to wait -
knowing where the host is, is enough. An area that does not exist never compares
equal to one that does, so the "already there" check still works. Confirmed by
the user.

---

## The other player's bone was invisible in their hands

**The problem.** The second player picks up a bone and wields it. The first
player sees empty hands. Found by the user, who also worked out the cause:
*"check where bone is, maybe it's not in weapon list"*.

**Why did it happen?** `Prepare_SetWeapon` takes a bare name and builds
`graph/obj3d/interactive/items/weapons` / name / name - it looks in the weapons
folder and nowhere else. A bone is not a weapon. It lives in
`graph/obj3d/interactive/items/provisions/bone/`, because it is food. Arx is
quite happy to let someone hit a goblin with it, but the lookup built
`items/weapons/bone/bone`, found nothing, and failed without a word. A sword
would have worked perfectly, which is why this went unnoticed.

**The fix.** The whole class path travels now, and the receiving side builds the
entity straight from it rather than guessing a folder, so it works for whatever
is in the weapon slot. Confirmed by the user.

**Worth recording:** this took three attempts. The first changed only the
sending side, which did nothing because the receiver still guessed the folder.
The second reverted it as "correct" after reading `Prepare_SetWeapon` and
concluding the bare name was intended - true in isolation, wrong here. Only
after checking where the file actually sits did the real answer appear. The
lesson is the one already written at the top of this file: the cause is the part
most likely to be wrong.

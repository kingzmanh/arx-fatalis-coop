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

---

## The other player was always in their underwear

**The problem.** Equip a helmet, armour or leggings and the other player still
sees you unarmoured. Reported by a tester, confirmed here.

**Why did it happen?** Two reasons, one on each side.

The avatar carried exactly one piece of equipment - the weapon. Helmet, armour,
leggings and shield were never put on the wire at all, so the other machine had
nothing to draw. That much was obvious once the Avatar structure was read.

The second reason is why the first fix did not work. Armour in Arx is not
carried the way a weapon is: it changes the body itself.
`ARX_EQUIPMENT_RecreatePlayerMesh` throws the mesh away, loads a fresh one, and
applies each piece as a *tweak* - a swapped mesh part and a repainted area of
skin. So the pieces were sent, the body was rebuilt, the tweaks were applied,
and nothing happened.

Because how a piece of armour changes a body is not written in the item file.
The item says it itself, by calling `setplayertweak` when its script starts up.
The armour entities here were created with `AddItem` and never had their script
run, so `tweakerinfo` was null and the tweak returned immediately - silently,
which is why it looked like the whole approach had failed rather than one line
being absent. The weapon path had always called `SendInitScriptEvent` for
exactly this reason.

**The fix.** Helmet, armour, leggings and shield travel with the avatar
(protocol 19 -> 20). `applyTweak` was generalised into
`ARX_EQUIPMENT_ApplyTweak`, which dresses any body rather than only the
player's. The body is rebuilt when the set changes - not per frame, since it
means reloading the mesh - and each piece has its script run before being asked
what it changes. Confirmed by the user.

---

## The other player's armour vanished when either of them changed area

**The problem.** Armour worked, then travelling to another area left the other
player looking unarmoured again. Reported by the user.

**Why did it happen?** The body is rebuilt to wear armour only when the armour
changes - reloading a mesh is too expensive to do every frame. But the check
compared only *what* they were wearing, not *which body* was wearing it.
Travelling tears the body down and builds a fresh, undressed one; the check saw
the same armour as before, decided there was nothing to do, and left it in its
underwear.

**The fix.** The body that was last dressed is remembered alongside the armour,
so a new body is always dressed. The record is also cleared when the body is
destroyed. Confirmed by the user.

---

## Cooked food only existed for one player

**The problem.** Food cooked on a fire appeared only on the host's screen, no
matter which player cooked it. Reported by a tester.

**Why did it happen?** Cooking is the script command `REPLACEME`. Raw bread does
not *become* bread - a new entity is created and the old one destroyed. The
other machine watched the raw item disappear and was never told anything had
replaced it.

`announceSpawn` - the function whose entire purpose is telling the other player
that something now exists - was written, declared in the header, and **called
from nowhere at all**. Every entity a script created was born on one machine
only; cooking was simply the case a tester happened to hit.

**The fix.** `REPLACEME` announces the new entity, but only when it ends up
lying in the world. If the script or the re-insert put it in someone's pack,
announcing a spawn would drop a second copy on the floor at the other end.
Confirmed by the user.

---

## Joining destroyed the host's armour off their body

**The problem.** The host is wearing armour, a guest joins, and the armour
vanishes from the host - not just visually; the equipment slot emptied.

**Why did it happen?** Joining means loading the host's savegame, so for a
moment the guest is an exact copy of the host: wearing their armour, holding
their weapon, carrying their pack. Every one of those items carries the host's
own id. `applyGuestIdentity` then strips that clone.

It stripped it loudly. The destructions were reported over the network, the host
heard that its armour had been destroyed, believed it, and destroyed the real
one.

Proven by tracing the host's equipment slot across a join: `handle=223
entity=alive` became `handle=-1`. The entity was not lost - the slot was
cleared, which pointed at a deliberate removal rather than a replication
failure.

**The fix.** The strip runs inside an `ApplyScope`, so none of it is announced.
Throwing away a copy is housekeeping that belongs to one machine alone.
Confirmed by the user.

---

## A joining player spawned holding a copy of the host's weapon

**The problem.** The guest spawned with the host's weapon in hand - not
equipped, just held, and it could neither be equipped nor dropped properly.

**Why did it happen?** Unequipping does not always put a thing away. With
nowhere to file it, the engine leaves it on the cursor. And an item being
dragged is neither worn nor in an inventory, which is exactly what
`collectBelongings` looks for - so the purge never saw it. It was the one piece
of the host's kit that survived, carrying the host's entity id on a machine with
no business owning it, which is why it behaved so strangely afterwards.

Two wrong guesses came first: that the equipment slots were not being cleared
(they were - traced as `weapon slot=-1`), and that the player mesh was not being
rebuilt (it was not, and that was worth fixing, but it was not this). The clue
that solved it was the user's own description: *"I'm holding it not equip it"*.

**The fix.** The cursor is emptied as part of the strip, before the purge runs.
The player mesh is also rebuilt afterwards, since clearing a slot does not
undress anybody - the weapon is an object linked to a hand and armour is a
swapped mesh part, and neither comes off just because the slot behind it was
emptied. Confirmed by the user.

---

## Fireplaces gave the second player light but no fire

**The problem.** One player lights a fireplace. They see flames; the other sees
only a glow, and hears nothing. Reported by the user.

**Why did it happen?** Two separate faults, and fixing the first alone changed
nothing.

A fireplace is not an entity. It is a light the level places, carrying
`EXTRAS_SPAWNFIRE`, and `m_ignitionStatus` says whether it burns. Nothing about
static lights was replicated anywhere. The glow the guest did see came from the
torch entity beside it, whose `ignition` **is** replicated - two systems
producing two halves of one effect, which is why it looked so strange.

But replicating the flag did nothing, because the flames are drawn by
`TreatBackgroundActions()`, and that was called inside `if(!worldIsRemote)`. The
guest never ran it, so it drew no flames for *any* fire, lit by anyone.

**The fix.** Static light ignition is replicated as changes (`MsgLightIgnite`,
protocol 21), with every lit light described once on joining. And the guest runs
`TreatBackgroundActions()` too - the flames and the crackle belong to both
players. The fire damage inside stays with the authority: registered on both, a
player standing in a fire would burn twice. Confirmed by the user.

## 20. A carried shield was invisible to the other player

**The problem.** One player equips a shield. They carry it; the other sees
nothing on their arm at all. Reported by the user.

**Why did it happen?** The shield was already being captured and already
crossing the wire - it arrived in `remote.shield` and then nothing ever read it.
Only half the feature had been written, and the sending half is the half that
looks finished.

It was missed because the two pieces of equipment already working solve the
problem in two different ways, and a shield is neither of them. Armour is not an
object at all: it is a change to the body, applied by rebuilding the mesh.
A weapon is an object, and the body has a slot to keep it in - `_npcdata->weapon`
- so something already owns it. A shield is an object with no slot: linked
mesh to mesh, `shield_attach` to `shield_attach`, and nothing remembers it.

**The fix.** The receiving side builds the shield from the path that was already
arriving and links it to the arm the way the engine links your own. Because
nothing else knows the entity exists, one pointer holds it, and all three
teardowns had to be handled: an armour change rebuilds the mesh it hangs from,
so it is rebuilt with it; closing the body deletes the shield *first*, because
deleting the body destroys the mesh and unlinking afterwards would read it back;
and an area change forgets it without deleting, because the level teardown has
already freed every entity there. Confirmed by the user.

## 21. The other player's idle animation looked half-finished

**The problem.** Standing still, the other player breathed through about half an
animation and snapped back to the start. Reported by the user, who found it
while looking at the shield fixed above.

**Why did it happen?** The engine chooses the idle by **which camera the player
is using**, and that choice was travelling to the other machine.

```
LOADANIM WAIT        "player_wait_short"   <- third person
LOADANIM WAIT_SHORT  "player_wait_1st"     <- first person
```

The names in `player.asl` are the opposite of how they read, which is what made
this hard to see: `ANIM_WAIT_SHORT` is not a short wait, it is the *first person*
idle - authored for the arms you see from inside your own head and for nothing
else. Playing in first person, that is what the local player's layer 0 held,
what `findAnimIndex` reported, and what the other machine dutifully played on a
whole body viewed from outside.

Both files are 80 frames, so it was never a shorter loop. It was an animation
that only ever animates the part a first person camera can see.

**The fix.** At capture, an idle of `ANIM_WAIT_SHORT` is sent as `ANIM_WAIT`.
What we look like to the other player cannot depend on which camera we happen to
be looking through. These are the only two view-dependent animation choices in
`ARX_PLAYER_Manage_Visual`, so this is the whole of it.

## 22. Both players stood locked under the black bars, waiting for nothing

**The problem.** Walking into the room where Ortiern greets you, and then walking
back, left both players held under cinematic bars with no cutscene playing and
no way out but closing the game. Reported by the user.

**Why did it happen?** Two unrelated faults that produced the same symptom, one
on each machine. Both were mine.

The story moment locks the player and hands the unlocking to a chain of script
events that hops camera to camera to speaker and back - `0085 -> 0088 -> 0089 ->
0091 -> 0090` - ending, many hops later, in the `SET_PLAYER_CONTROLS ON` that
gives control back.

*On the guest:* `SENDEVENT` does not call a script, it **queues** one with
`Stack_SendIOScriptEvent`, and the queue is only drained where the area is
simulated - `if(!coop::isReplica()) { ARX_SCRIPT_EventStackExecute(); ... }`.
So the guest applied the lock and queued the event that would lift it, and that
event was never run. Not late: never.

*On the host:* `UNSET_CONTROLLED_ZONE`, which is how the trigger disarms itself
after firing, returned early whenever the partner's crossing was what ran the
script. The zone therefore stayed armed, and fired a second time when this
machine's own player walked in - into a chain of cameras the first run had
already destroyed, so the lock went on with nothing alive to take it off. The
same early return also skipped the `getWord()` that consumes the zone name,
leaving it in the stream to be read as a command: the log shows the parser
reporting `unknown command: ortiernzone` and carrying on.

**The fix.** The guest no longer locks itself: the script lock and the cinematic
bars are suppressed under exactly the same `coop::isReplica()` condition that
mutes the queue, so a lock can never be applied where its key cannot run. The
guest still watches - the host performs the scene and sends a viewer copy that
holds them for its length and releases itself.

And the zone disarms for the world, partner or not. When the host runs a script
in the partner's name it runs all of it - the cameras are destroyed, the quest is
granted - so leaving the zone armed was omitting one side effect from a script
whose every other side effect had landed. Confirmed by the user.

## 23. After the cutscene, the second player had no cursor and no hands

**The problem.** With the lock and the bars fixed, the guest came out of the
Ortiern scene unable to see or use the crosshair, so nothing could be picked up
or used. Reported by the user.

**Why did it happen?** The same fault as #22, in the third command of the same
three-line block, which I fixed two of:

```
SET_PLAYER_CONTROLS OFF     <- fixed in 22
CINEMASCOPE -s ON           <- fixed in 22
PLAYER_INTERFACE -s HIDE    <- this one
```

Every one of them is half of a pair whose other half - ON, OFF, SHOW - sits at
the end of the queued event chain that a guest never drains. Hiding is therefore
permanent there, and unlike the lock there is no watchdog behind it and no key
the player can press: the cursor simply never comes back.

`PLAYER_INTERFACE` also carried the same argument-consumption bug as
`UNSET_CONTROLLED_ZONE`: an early return placed above its `getWord()`, leaving
"hide" in the stream to be read as a command. That is the `unknown command:
hide` sitting in the log next to `unknown command: ortiernzone`. The other two
commands guarded this way were checked and read their arguments first, so those
two were the whole of it.

**The fix.** The argument is read before anything is decided, and only *hiding*
is refused - on a guest, and for the partner's sequence. SHOW is always allowed
through, whoever asks, because handing the interface back can never be the thing
that strands someone.

## 24. The other player's stance reset over and over, but only with a shield

**The problem.** Carrying a shield, the other player put a hand out, snapped
back to the start, and did it again about once a second. Without a shield they
were fine. Reported by the user, who also supplied the fact that broke it open:
*a player holding a shield does not stand the same way*.

**Why did it happen?** Because that is true, and I had not found it. Equipping a
shield starts a hold animation on a layer of its own:

```
ManageNONCombatModeAnimations():
    if(shield equipped)  changeAnimation(io, 3, ANIM_SHIELD_START)
                         changeAnimation(io, 3, ANIM_SHIELD_CYCLE, EA_LOOP)
```

Layer 3 exists only while a shield is equipped, which is exactly why nothing
else showed a difference. And layer 3 was the one layer sent bare:

```
applyAnimLayer(body, 0, anim0, anim0Flags, anim0Time);
applyAnimLayer(body, 1, anim1, anim1Flags, anim1Time);
applyAnimLayer(body, 3, anim3, 0, 0);          // <- no flags, no playhead
```

Two failures from one line. Flags of zero meant EA_LOOP never arrived, so a clip
written to loop stopped looping. And a playhead of zero, sent every packet, met
the rule that resynchronises a layer drifting more than a second from what the
other machine reports - so once the clip passed one second it was dragged back
to the start, forever.

Time was lost proving layer 0 innocent in great detail - right clip, adopted
once, full length, clean loop, playhead within 30ms - because that is where the
breathing lives and the report was about breathing. Layer 0 was never at fault.
The measurements were sound and pointed at the wrong layer, and a Blender
reconstruction of layer 0 alone could not reproduce the fault for the same
reason. What ended it was the user saying the stance itself differs with a
shield, which no amount of staring at layer 0 would ever have suggested.

**The fix.** Layer 3 carries its flags and its playhead, exactly as 0 and 1 do.
Protocol 21 -> 22, since the packet grew. Registered twice in coop-check.sh: the
bare call must never return, and the full one must be present. Confirmed by the
user.

## 25. A shield put down was still drawn

**The problem.** Found by reading, not by playing, while hunting the above.

**Why did it happen?** ~Entity detaches everything that calls IT owner, but never
removes itself from its OWNER's linked list - that only happens in setOwner(),
which the destructor does not call. So deleting the shield while the body lived
on left its EERIE_LINKED record in place, holding a raw pointer to a mesh that
had just been freed, and the draw loop walks that list every frame. Swapping one
shield for another left the body carrying a dead record and a live one.

**The fix.** Unlink before deleting, at both sites that delete a shield.

# Co-op source snapshot 0.4

Protocol 18. 56 engine files changed (+1,851 / -241). Base engine: Arx
Libertatis commit `5b95e4c`. This is 0.2 plus the work below - 0.3 was rolled
back and is not an ancestor of this.

## New in this snapshot

### coop-check.sh - the anti-regression registry

Every fix, with the fingerprint that proves it is still in the source. Run it
before every build:

```
./coop-check.sh        list all
./coop-check.sh -q     only what is missing (exit code = how many)
```

The failure that cost this project weeks was never a hard bug. It was a fix
quietly disappearing - rolled back with a backup, overwritten by a restore, or
edited out while working on something else - and nobody noticing until it was
found again by playing. **A fix that is not registered in that file is a fix
that will be lost.** Add the line in the same breath as the fix.

Its first run immediately named the three gaps below, which had been found the
slow way minutes earlier.

### Dropped objects obey physics (fixed, confirmed)

Physics runs on the authority alone, and an object only moves while its physics
box is active - but the host received the other player's drop as a *teleport*,
and teleporting switches that box off. So anything the second player let go of
hung in the air: no machine was left to make it fall.

Now the release impulse travels with the drop (a throw sends its direction, a
soft drop its small push, a placement zero) and the host launches the object
with it, exactly as a local drop does. The two-second "ignore corrections"
grace is skipped for a throw, so the flight arrives live instead of the object
hanging still and then teleporting to where it had already landed.

### Weapons do not wear against a companion (partial)

A weapon is charged durability once per FRAME its blade is inside a target -
about three points across one swing. The other player's body is a valid target
and stands beside you constantly, so an ordinary swing at empty air clipped
them: three points against a bone, which has four. That charge now excludes
the partner's body.

It did not fully cure it - the bone still broke in testing - so the build also
carries a tracer, `[coop-wear]`, which names which of the three charge sites
fires and against what. The remaining two sites are "hit a hard object" and
"hit level geometry", both worth one full point.

## Known gaps in THIS snapshot

The registry reports these every run. They are fixes that existed, were proven,
and were removed by the rollback - not mysteries:

1. **zone crossings run in the partner's name** - without it, a script an
   ambush fires when the second player trips it resolves "player" by distance,
   so the jail guard attacks whoever stands closer instead of the escapee.
2. **dragged items are never dragged back** - the smoothing pass writes a
   replicated item's position every frame with no guard, so an item the second
   player picks up snaps back to the floor and has to be dragged twice.
3. **the port is optional** - the USE PORT checkbox; without it the port box is
   always live.

Older known limitations (loot duplication, quest-flag drift after the join,
solo progress being forgotten, debug instrumentation armed, untested above
15 ms, Windows only) are unchanged from 0.2.

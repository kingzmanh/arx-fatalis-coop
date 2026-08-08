# PLAYER TWO — the exact method as it stands, function by function

Same format as PLAYER1-METHOD.md. P2 exists on two machines at once, so every
section says which machine the code runs on.

---

## 1. Identity

**On P2's own machine** — P2 *is* player one's code, verbatim. Entity 0, the
`ARXCHARACTER player` global, all of it. Nothing in section 1 of the P1 file
differs on P2's machine.

**On P1's machine** — P2 is an ordinary entity created in
`net/CoopPlayer.cpp:createAvatarEntity()`:
```cpp
Entity * body = new Entity(AvatarClassPath, EntityInstance(1));   // "coop_player_0001"
body->ioflags = IO_NPC;                        // so weapons/spells know what it is
body->ioflags |= IO_NOSAVE | IO_FORCEDRAW | IO_NO_COLLISIONS;
```
No script, no character sheet — position/animation/health arrive over the wire.
It answers to the name "player" only inside a `ScopedPlayerContext`
(`game/EntityManager.cpp:150`) and in `^sender` / zone parameters.

---

## 2. Travel — the chains P2 can take

P2 has **three** possible transition chains, where P1 has one.

### Chain 1 — P2 travels by its own local door (P2 alone in the area)

When the host is *not* in P2's area, P2's machine has authority: zones run,
timers run, scripts run. A door fires **locally**, exactly like P1's:

- local zone entry → local controller script → local `teleport -l` →
  local globals → `ArxGame.cpp:1187` → `ARX_CHANGELEVEL_Change(area, marker, angle)`
  → `Pop_Player` **marker branch**.

This chain is P1's chain, byte for byte. When it runs, arrival is exact.

*Fragility:* the door must still be armed **on P2's machine**. One-shot doors
(`UNSET_CONTROLLED_ZONE` after first use — e.g. jail exit `marker_0225`)
consume one copy per machine. Since the timer-context change, P2 firing a
door host-side no longer consumes the host's copy, and vice versa.

### Chain 2 — P2 stands in a door while sharing the host's area

P2's own zone checks are muted (replica). Instead, on the **host**:

- P2's body enters the zone → `ai/Paths.cpp:EntityEnteringCurrentZone` fires the
  controller with `PARAM1 = "player"` under `ScopedPlayerContext`
- the door script's `teleport -l` reaches
  `script/ScriptedIOControl.cpp` where the partner interception routes it:
```cpp
if(coop::isPartnerScriptContext()) {
    coop::sendTravelOrder(u32(level), target, angle, confirm);
    return Success;                       // host's own travel state untouched
}
```
- if the teleport was inside a script **timer** (jail exit), the timer carries
  the context (`SCR_TIMER::partnerContext`, stamped in
  `script/Script.cpp:createScriptTimer`, re-raised at fire in
  `ARX_SCRIPT_Timer_Check`) so the interception still triggers
- guest receives `MsgTravel` (`net/CoopNet.cpp`):
```cpp
g_teleportToArea = AreaId(area);
TELEPORT_TO_POSITION = target;
TELEPORT_TO_ANGLE = (angle == -1) ? long(player.angle.getYaw()) : long(angle);
CHANGE_LEVEL_ICON = confirm ? ConfirmChangeLevel : ChangeLevelNow;
```
From here it is `ArxGame.cpp:1187` again — P1's chain on P2's machine, marker
and all.

### Chain 3 — join / rejoin "travel to the host" (`travelToHost`)

`net/CoopNet.cpp` — fires once after a (re)connect when the areas differ:
```cpp
g_teleportToArea = g_session.remoteArea;
TELEPORT_TO_POSITION.clear();            // ← NO MARKER, BY CONSTRUCTION
TELEPORT_TO_ANGLE = 0;
CHANGE_LEVEL_ICON = ChangeLevelNow;
```
This reaches the same `Pop_Player`, but with an **empty target**, so the
placement takes the first branch:
```cpp
if(target.empty()) {
    player.pos = asp->pos.toVec3();      // saved coordinates — FROM THE OLD AREA
}
```
Coordinates from area 1 applied in area 15 space = above the ceiling. The
arrival snap ("placed beside HOST") exists to catch exactly this, but only
fires when the host is already reported in the same area at load time, and is
suppressed when the transition *claimed* to be door-based.

### The placement function itself

P2 uses `ARX_CHANGELEVEL_Pop_Player` — **the identical function** P1 uses
(`scene/ChangeLevel.cpp:1457`). There is no separate P2 placement code. The
entire difference is *what `target` contains when the shared function runs*,
plus its two silent hazards (empty target → stale save coordinates; failed
marker lookup → no fallback, no warning).

---

## 3. The other systems, per machine

| System | P2's own machine | P1's machine (the body) |
|---|---|---|
| Movement | identical to P1 | position interpolated from network |
| Animation | identical to P1 | replicated layer indices, local playback |
| Rendering | identical to P1 | `RenderInter()` like an NPC, `IO_FORCEDRAW` |
| Treat zone | identical + partner added | added unconditionally with P1 |
| AI exemption | identical | `coop::isAvatarEntity` skip in `ARX_PHYSICS_Apply` |
| Damage | `damagePlayer()` — identical | intercepted in `damageNpc`/`damageCharacter`, forwarded |
| Zones | **muted while sharing area** (replica) | body fires zones as "player" with context |
| Sight | seen via `m_seenPartner` (own edge) | — |
| Scripts | run only when alone in the area | context makes "player" mean the body |
| Save | own `current-<pid>.sav`, own slots — identical code | never saved (`IO_NOSAVE`) |

---

## 4. The exact difference (the answer)

**The placement method is already the exact same function.** Both players are
placed by `ARX_CHANGELEVEL_Pop_Player` at `scene/ChangeLevel.cpp:1457`. There
is no "similar" copy for P2 — it is one function, shared.

**The difference is the guarantee feeding it.** P1 enjoys an invariant the
engine was built around: *a door script always ran on P1's machine and filled
`TELEPORT_TO_POSITION` with a marker that exists in the destination level*, so
the marker branch always runs. P2 has three chains, and only two of them
uphold that invariant. Chain 3 (`travelToHost`) violates it **by design** —
it clears the marker — and any breakage in Chain 2's relay (order not sent,
one-shot consumed, context lost) degrades P2 into the same empty-target
arrival.

Observed in the failing session (probe log): P2 arrived in area 15 at
`9579, 695, 9938` — numerically the guest's *area-1* coordinates — with
`camroom=inv` (outside every room). That is the `target.empty()` branch of the
shared function doing exactly what it does for a same-area save-load, applied
across areas where it is meaningless.

**Therefore the fix that makes P2 "exactly P1" is not a new placement method —
it is enforcing P1's invariant for P2:** every P2 transition must reach
`Pop_Player` with a valid destination marker, and the two silent hazards in the
shared function must stop being silent (log the failed lookup; treat an
empty-target arrival into a *different* area as invalid and fall back to a real
anchor — the host's position, or the level's own start marker).

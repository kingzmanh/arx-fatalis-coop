# PLAYER ONE — the exact method the game uses, function by function

Every entry below is the real code from this tree, with file and line. This is
the complete chain the engine runs for player one, in order.

---

## 1. Identity — what makes P1 "the player"

**Slot 0 reserved before any entity exists** — `game/EntityManager.cpp:93`
```cpp
void EntityManager::init() {
    arx_assert(size() == 0);
    entries.resize(1);
    entries[0] = nullptr;
    m_impl->m_minfree = 0;
}
```

**P1's entity is created first and asserts it got slot 0** — `game/Player.cpp:1089`
```cpp
Entity * io = new Entity("graph/obj3d/interactive/player/player", EntityInstance(-1));
arx_assert_msg(io->index() == EntityHandle_Player, "player entity didn't get index 0");
arx_assert(io->idString() == "player");
```

**Every lookup of the name "player" returns entity 0** — `game/EntityManager.cpp:150`
```cpp
if(idString == "player") {
    // (coop context aside) —
    return player();          // = entries[0]
}
```

**The character sheet is a single global** — `game/Player.cpp:149`
```cpp
ARXCHARACTER player;
```
Health, mana, XP, level, skills, gold, equipment slots — all in this one global,
read directly by ~2,076 places in the code.

---

## 2. Travel — the exact transition chain

### Step A. A door script fills the travel state

`script/ScriptedIOControl.cpp` — the `teleport -l` command (the only way any
level transition starts):

```cpp
if(flg & flag('l')) {
    float level = context.getFloat();
    std::string target = context.getWord();      // e.g. "marker_0915"
    ...
    g_teleportToArea = AreaId(u32(level));
    TELEPORT_TO_POSITION = target;               // the DESTINATION MARKER
    if(angle == -1) {
        TELEPORT_TO_ANGLE = static_cast<long>(player.angle.getYaw());
    } else {
        TELEPORT_TO_ANGLE = angle;
    }
    CHANGE_LEVEL_ICON = confirm ? ConfirmChangeLevel : ChangeLevelNow;
    return Success;
}
```

Four globals: destination area, **destination marker**, facing angle, and the
go/confirm state. These are *this machine's player's* travel state.

How that script gets to run for P1 — one of two triggers:
1. **Zone**: P1's body enters a controlled zone; `ai/Paths.cpp`
   (`EntityEnteringCurrentZone`) sends `SM_CONTROLLEDZONE_ENTER` to the zone's
   controller (e.g. `marker_0225`), whose script runs the teleport — sometimes
   via a script **timer** (`TIMERtele -m 1 700 TELEPORT -NAL 180 15 MARKER_0915`).
2. **Click**: P1 clicks a door; `SM_ACTION` runs the door's script
   (`ON ACTION { TELEPORT -NAL 180 11 MARKER_0306 }`).

Both run **on P1's machine**, because that is where P1's body, P1's zone
checks, P1's clicks and P1's script timers live.

### Step B. The main loop consumes the travel state

`core/ArxGame.cpp:1187`
```cpp
if(g_teleportToArea && CHANGE_LEVEL_ICON != NoChangeLevel
   && (CHANGE_LEVEL_ICON == ChangeLevelNow
       || config.input.quickLevelTransition == ChangeLevelImmediately
       || (config.input.quickLevelTransition == JumpToChangeLevel
           && GInput->actionPressed(CONTROLS_CUST_JUMP)))) {
    CHANGE_LEVEL_ICON = NoChangeLevel;
    ARX_CHANGELEVEL_Change(g_teleportToArea, TELEPORT_TO_POSITION, float(TELEPORT_TO_ANGLE));
    g_teleportToArea = { };
    TELEPORT_TO_POSITION.clear();
}
```

### Step C. The transition saves the old area, loads the new

`scene/ChangeLevel.cpp:320` — `ARX_CHANGELEVEL_Change(area, target, angle)`:
- `ARX_CHANGELEVEL_PushLevel(old, new)` — writes the old area **and the player
  (with their current, old-area position)** into `current.sav`
- `ARX_CHANGELEVEL_PopLevel(area, true, target, angle)` — loads the new area,
  then calls the placement function below.

### Step D. THE PLACEMENT — where the player's position is decided

`scene/ChangeLevel.cpp:1457` — inside `ARX_CHANGELEVEL_Pop_Player(target, angle)`:

```cpp
if(target.empty()) {
    player.angle = asp->angle;
    player.pos = asp->pos.toVec3();          // position from the SAVE
} else {
    if(Entity * targetEntity = entities.getById(target)) {
        player.pos = GetItemWorldPosition(targetEntity) + player.baseOffset();
    }                                        // ← NOTE: silent if lookup fails
    player.desiredangle.setYaw(angle);
    player.angle.setYaw(angle);
}
```

**This is the whole secret of P1's clean arrivals**: the door always supplied a
`target` marker, the marker exists in the just-loaded level, so the second
branch runs and P1 stands exactly on the marker. The first branch (`asp->pos`,
raw saved coordinates) is only ever taken on save-loads *into the same area the
save was made in* — where those coordinates are valid by construction.

Two silent hazards in this function (they never bite P1, by luck of the above):
1. `target.empty()` → raw saved coordinates are used **even if they belong to a
   different area**.
2. Marker lookup fails → **no fallback, no warning** — position silently stays
   whatever the save said.

### Step E. Post-placement fixups

- `ARX_CHANGELEVEL_Change` tail: `ARX_PLAYER_RectifyPosition()` (zeroes the
  extra body rotation) — `scene/ChangeLevel.cpp:356`
- `levelInit()` — `core/Core.cpp`: camera snapped to `player.pos`, and
  `if(!CheckInPoly(player.pos) && LastValidPlayerPos != 0) player.pos = LastValidPlayerPos;`
  (`LastValidPlayerPos` was reset to zero during the transition, so this is a
  no-op on a normal travel)
- The arrival level's scripts run their choreography (fade in etc.) — on P1's
  machine, because that is where script timers run.

---

## 3. The other P1-exclusive paths (summary with sites)

| System | Site | What P1 gets |
|---|---|---|
| Movement | `game/Player.cpp` `ARX_PLAYER_Manage_Movement/PlayerMovementIterate` | full player physics, jumping, climbing |
| Animation | `core/ArxGame.cpp updateLevel` | driven directly, not via `UpdateInter()` |
| Rendering | `scene/Scene.cpp:1617` | drawn last, own pass, never portal-culled (`ARX_SCENE_PORTAL_ClipIO` returns visible for player) |
| Treat zone | `scene/Interactive.cpp PrepareIOTreatZone` | added unconditionally, first |
| AI exemption | `game/NPC.cpp ARX_PHYSICS_Apply` | loop starts at 1 — "We don't manage Player(0) this way" |
| Damage | `game/Damage.cpp:damagePlayer` | own function; `damageNpc` asserts it never receives the player |
| Zones | `ai/Paths.cpp:ARX_PATH_CheckPlayerInZone` | dedicated per-frame check of `player.pos`; applies ambiance/fog and fires controller events |
| Sight | `game/NPC.cpp:CheckNPCEx` | creatures test visibility against `player` globals |
| Scripts | `script/Script.cpp` `^player_*`, `^dist_player`, `SETTARGET PLAYER`, `^sender == player` | the entire script language's notion of "player" |
| Save | `scene/ChangeLevel.cpp Push_Player/Pop_Player` | position, stats, inventory, equipment, quests, keyring, map |

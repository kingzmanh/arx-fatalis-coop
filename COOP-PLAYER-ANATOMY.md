# What "the player" is in Arx, and how player two differs

A deep dive into entity 0: what makes it special, everywhere it is named, and the
exact list of differences between player one and player two as the code stands.

Written against the working tree, not from memory. Line references are current.

---

## 1. The player is two separate things

This is the single most important fact, and almost every difference below
follows from it.

**Thing one: an entity.** A normal `Entity` living in the entity manager, with a
mesh, a skeleton, animation layers, a position and an inventory. It is what the
world collides with, draws, and hits.

**Thing two: a character sheet.** A single global struct, `ARXCHARACTER player`,
declared in `game/Player.h:410` and defined in `game/Player.cpp:149`. It holds
health, mana, level, experience, attributes, skills, gold, runes, hunger,
poison, the equipment slot array, the movement flags, the jump state — the whole
character. It is **not** part of the entity. It is a global variable.

There is exactly one of each, and the code assumes so throughout.

---

## 2. What makes entity 0 "the player"

Four separate mechanisms, all independent of each other:

### 2.1 It is index 0, by construction

`EntityManager::init()` (`game/EntityManager.cpp:92`) reserves slot 0 before any
entity exists:

```cpp
void EntityManager::init() {
    arx_assert(size() == 0);
    entries.resize(1);
    entries[0] = nullptr;
    m_impl->m_minfree = 0;
}
```

The player entity is then the first thing created, so `add()` hands it slot 0.
`ARX_PLAYER_LoadHeroAnimsAndMesh()` (`game/Player.cpp:1089`) asserts this:

```cpp
Entity * io = new Entity("graph/obj3d/interactive/player/player", EntityInstance(-1));
arx_assert_msg(io->index() == EntityHandle_Player, "player entity didn't get index 0");
```

Accessors then hardcode the index:

```cpp
// game/EntityManager.h:66
Entity * player() const { return entries[0]; }

// game/GameTypes.h:32
constexpr EntityHandle EntityHandle_Player = EntityHandle(0);
```

### 2.2 It survives level changes

`EntityManager::clear()` deliberately starts at 1:

```cpp
// Free all entities, ignoring the player.
for(size_t i = 1; i < size(); i++) { delete entries[i]; }
entries.resize(1);
```

Every other entity in the game — including player two's body — is destroyed on
every level load. The player entity is the only permanent one.

### 2.3 It is named `player`

Its instance number is `-1`, which `EntityId` renders as the bare string
`"player"` with no numeric suffix. `EntityManager::getById()` then special-cases
that name before consulting the id map at all:

```cpp
// game/EntityManager.cpp
if(idString == "player") { return player(); }
```

This is the hook the entire script layer hangs off. See section 4.

### 2.4 It is skipped by the systems that handle "everyone else"

Not one flag, but a scattering of explicit exclusions:

| Where | What it does |
|---|---|
| `game/NPC.cpp` `ARX_PHYSICS_Apply()` | loop starts at `i = 1` — *"We don't manage Player(0) this way"* |
| `scene/Interactive.cpp` `UpdateInter()` | skips the player; its animation is driven from `ArxGame::updateLevel()` instead |
| `scene/Interactive.cpp` `RenderInter()` | skips the player; drawn separately and last, in `ARX_SCENE_Render()` |
| `scene/Interactive.cpp` `PrepareIOTreatZone()` | `TREATZONE_AddIO(entities.player())` unconditionally, before any distance test |
| `scene/Scene.cpp` `ARX_SCENE_PORTAL_ClipIO()` | returns "visible" immediately for the player — never culled |
| `game/Damage.cpp` `damageNpc()` | `arx_assert(npc != *entities.player())` — damaging the player is a different function entirely |

So the player takes a completely separate path through movement, animation,
rendering, culling and damage. It is not "an NPC that happens to be controlled".

---

## 3. The character sheet is global, and single

`ARXCHARACTER player` is read directly, by name, from everywhere. Raw counts of
references to `entities.player()`, `EntityHandle_Player` or `player.` in the
working tree:

| Subsystem | References |
|---|---|
| `game/` | 1057 |
| `gui/` | 452 |
| `scene/` | 194 |
| `core/` | 192 |
| `script/` | 96 |
| `physics/` | 34 |
| `graphics/` | 25 |
| `animation/` | 20 |
| `ai/` | 6 |
| **Total** | **~2076** |

Every one of those is a place that can only ever mean player one. That is the
scale of the thing, and it is why the approach here is *not* to duplicate the
player struct.

---

## 4. The script layer: one name, one player

This is where the co-op problems actually live. Arx's game logic — what enemies
do, what doors do, what quests do — is in `.asl` scripts, not in C++. Those
scripts know exactly one word for a player: `PLAYER`.

Concretely, from `graph/obj3d/interactive/npc/goblin_base/goblin_base.asl`
(45 KB, and representative of every hostile NPC in the game):

- `SETTARGET PLAYER` — **10 occurrences**
- `IF (^SENDER != PLAYER) ACCEPT` — 3 occurrences
- `IF (^DIST_PLAYER < 600) GOTO ATTACK_PLAYER`
- `IF (^SENDER == PLAYER)`

Every one resolves through the mechanisms in 2.3, and therefore to entity 0.

The engine also exposes a large set of read-only script variables that reach
straight into the global character sheet (`script/Script.cpp:1088-1240`):

```
^player_life  ^player_maxlife  ^player_mana   ^player_maxmana
^player_gold  ^player_zone     ^player_hunger ^player_poison
^player_attribute_{strength,dexterity,constitution,mind}
^player_skill_{stealth,mecanism,intuition,etheral_link,
               object_knowledge,casting,projectile,close_combat,defense}
^playercasting  ^playerspell_<name>
```

All of these are player one's, unconditionally, and there is no syntax in the
script language to ask about anybody else.

---

## 5. What player two actually is

Deliberately **not** a second copy of the player. It is:

- **On its owner's machine:** the ordinary, unmodified player. Entity 0, the
  global `ARXCHARACTER`, all 2076 references above. Player two's game does not
  know it is player two. This is why it has its own health, mana, experience,
  level, skills, gold and inventory for free, and why its progress saves
  normally to its own disk.

- **On the other machine:** a single ordinary entity, class
  `graph/obj3d/interactive/npc/coop_player`, id string `coop_player_0001`,
  created in `net/CoopPlayer.cpp`. It carries `IO_NPC` so that swords, spells
  and blood know what to do with it, plus `IO_NOSAVE`, `IO_FORCEDRAW` and
  `IO_NO_COLLISIONS`. It has the player's mesh and a reference-counted share of
  the player's animation set. It has **no script and no character sheet** — only
  what arrives over the wire: position, facing, animation, health, weapon class.

So "player two" as a thing the world can interact with is a puppet, and the
authority for everything about it is the other machine.

---

## 6. The difference, point by point

| | Player one (entity 0) | Player two (`coop_player_0001`) |
|---|---|---|
| Entity index | Always 0, guaranteed | Whatever slot was free |
| Id string | `player` | `coop_player_0001` |
| Survives level change | Yes, uniquely | No — destroyed and rebuilt |
| Character sheet | Global `ARXCHARACTER` | None on this machine; lives on the owner's |
| Health, mana, XP, gold | In the global struct | Replicated summary only; real values are remote |
| Inventory | `entities.player()->inventory` | Not present on this machine |
| Movement | `ARX_PLAYER_Manage_Movement()` | Interpolated from network updates |
| Animation | Driven in `ArxGame::updateLevel()` | Replicated layer indices + flags |
| Rendering | Own pass, drawn last, never culled | `RenderInter()`, `IO_FORCEDRAW` |
| AI treats it as | Explicitly skipped everywhere | Explicitly skipped (`coop::isAvatarEntity`) |
| Collision | Player cylinder, world collision | `IO_NO_COLLISIONS` — passes through |
| Damage entry point | `damagePlayer()` | Intercepted in `damageNpc()`, forwarded to owner |
| Script name | `player` | **`player`, as of the `^sender` change** |
| `SETTARGET PLAYER` | Resolves to it | Resolves to it **when nearer** |
| `^DIST_PLAYER` | Distance to it | Counted, via "nearest player" |
| `^player_life` etc. | Its own values | **Never** — always reports player one |
| Story / dialogue / quests | Drives everything | Not addressable by scripts |

---

## 7. Why enemies still only fight back after being hit

This is the remaining bug, and the cause is specific.

`CheckNPCEx()` (`game/NPC.cpp:2554`) is the sight test. It ends:

```cpp
if(Visible && !io._npcdata->detect) {
    SendIOScriptEvent(nullptr, &io, SM_DETECTPLAYER);
    io._npcdata->detect = 1;
}
if(!Visible && io._npcdata->detect) {
    SendIOScriptEvent(nullptr, &io, SM_UNDETECTPLAYER);
    io._npcdata->detect = 0;
}
```

`detect` is **one boolean for one player** (`long detect;` in `IO_NPCDATA`), and
`SM_DETECTPLAYER` fires only on the 0 → 1 edge.

The sight test now measures against whichever player is nearer, so player two
*can* set that flag. But if the creature had already seen player one — which is
the normal case, since it has usually noticed somebody by the time player two
walks up — `detect` is already 1, no edge occurs, **and the script is never
told**. The creature never re-runs `ON DETECTPLAYER`, so it never reaches:

```
IF (^DIST_PLAYER < 600) GOTO ATTACK_PLAYER
```

Being hit takes a different route entirely — `damageNpc()` sends `SM_HIT`
directly — which is why attacking works and being seen does not. That matches
the observed behaviour exactly: *ignores player two until player two attacks it.*

**The fix** is to make sight per-player rather than one shared flag: track
"have I seen player one" and "have I seen player two" separately, and fire
`SM_DETECTPLAYER` on either edge. `detect` is saved and restored in
`scene/ChangeLevel.cpp:1151` and `:2043`, so widening it needs care about the
save format — the second bit should not be written to disk.

---

## 8. What "everything shared except story" still needs

Working now: both bodies visible and animated, per-player health/mana/XP/level/
inventory/gold, melee and magic between players, shared treat zone, item
relocation and pickup, entity destruction, doors and collision changes, shared
quest and keyring entries, minimap markers, cross-area play.

Still player-one-only, in rough order of how much they matter:

1. **Sight-based aggro** — section 7 above.
2. **`^player_*` script variables** — a script asking "how much life does the
   player have" always gets player one. Affects any script gating on player
   condition. Would need a notion of "the player this event is about".
3. **`SENDEVENT ... PLAYER`** — scripts sending an event *to* the player always
   reach player one. Fine for story, wrong for "the guard shouts at you".
4. **Dialogue and `SM_CHAT`** — `gui/Interface.cpp:658` sends chat from
   `entities.player()`. Player two talking to an NPC would speak as player one.
   By your design this is correct and should stay.
5. **Zones (`SM_CONTROLLEDZONE_ENTER`)** — `ai/Paths.cpp` tests player one's
   position only, so triggers, traps and ambush zones do not fire for player two.
6. **Secondary inventories / containers** — a chest opened by both players is not
   replicated; its contents can be taken twice.

Items 1 and 5 are the two that most change how the game feels to play; both are
tractable and neither requires touching the character sheet.

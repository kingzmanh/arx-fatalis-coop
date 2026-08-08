# Native co-op pass — the ledger

## AUDIT v2 (full 2,076 re-read, one by one) — IN PROGRESS

User mandate 2026-08-05: read every site individually, fix for two players.
Structural systems built during this pass:
- **Partner-effect channel** (MsgPartnerEffect 79): heal (batched
  accumulator in flush), mana, fill-hunger applied to the REAL partner when
  an effect lands on the puppet body.
- **Status-spell relay** (MsgSpellCast + casterId field, protocol 15):
  creature casts PARALYSE/SLOW_DOWN/CONFUSE/CURSE/LOWER_ARMOR at the avatar
  → the cast itself is relayed; guest launches the same spell from the same
  creature at its real player (targetId "player"). Damage spells excluded —
  they travel via hit interception (no double-strike).

Verdicts so far (v2):
| File | Sites | Result |
|---|---|---|
| SpellsLvl01 | 13 | 2 CHG: avatar keeps vertical aim (MagicMissile); avatar ignites by PLAYER rules. Rest OK under the replication model (caster-gated = local screen; replicas re-run per machine). |
| SpellsLvl02 | 14 | 1 CHG: heal on avatar → reportPartnerHeal (was healing the puppet). Self-buff replicas expire early on the partner screen (visual, noted). |
| SpellsLvl03 | 18 | 4 CHG: Fireball avatar pitch (launch+update); CreateFood particles on caster not P1; CreateFood hunger → PartnerFxFillHunger. IceProjectile OK. |
| SpellsLvl04 | 16 | all OK (self-buffs, telekinesis caster-gated, Curse anchors handle avatar via NPC branch). |
| SpellsLvl05 | 24 | 1 CHG: RepelUndead initial rune pos under caster not P1. Levitate NPC-cast-at-player doesn't exist in vanilla (noted). Repel effect protects avatar via host replica ✓. |
| SpellsLvl06 | 10 | 2 CHG: Paralyse at avatar → covered by status relay; DisarmTrap sphere centered on caster not P1. |
| SpellsLvl07 | 16 | 1 CHG: LightningStrike avatar vertical aim. FlyingEye refuses non-player caster ✓ (replica no-op, screen-local). |
| SpellsLvl08 | 8 | 2 CHG: Explosion centres player-like for the avatar; MANA_DRAIN added to the status relay (the puppet has no mana to steal). Invisibility on avatar = host AI truly can't see P2 ✓. |
| SpellsLvl09 | 13 | 2 CHG: replicas may not SPAWN summons (authority-only; ghost-copy prevention); NegateMagic field-dispel parity for avatar target. Fissure yaw uses P1's angle on replicas (cosmetic, noted). |
| SpellsLvl10 | 5 | 1 CHG: ControlTarget scans from the caster (was always P1's position/yaw). Mass spells radial ✓. |

**Spell files complete: 143/143 sites read, 16 CHG, 2 structural systems.**

| game/Equipment.cpp | 81 | 81/81 read, 0 CHG — all local equipment mechanics; avatar is never io_source in ComputeDamages (P2 melee goes requestHit), NPC-vs-avatar damage bypasses the player-AC branch and is applied raw on P2's machine with P2's own defense. Cosmetic backlog: P2's armor/helmet not shown on the body (weapon only). |
| physics/Projectile.cpp | 18 | 18/18 read, 0 CHG — projectiles are local simulations of the local player's shots; P2's arrow damage travels via the guest damage interception. Cosmetic backlog: P2's arrows in flight invisible to P1. |
| gui/hud/SecondaryInventory.cpp | 25 | 25/25 read, 0 CHG — panel UI; takes/purchases ride the audited pickup replication. |
| gui/book/Book.cpp | 132 | 132/132 read, 0 CHG — character sheet display + local stat spending; BOOK_OPEN/CLOSE to own script. |
| gui/Hud.cpp | 67 | 67/67 read, 0 CHG — local HUD. NOTE: P2's SM_STEAL reactions are muted on a replica (theft itself replicates; merchant reactions don't) — backlog. |
| gui/hud/PlayerInventory.cpp | 52 | 52/52 read, 0 CHG — local UI; inserts ride audited paths. |
| game/spell/Cheat.cpp | 25 | 25/25 read, 0 CHG — NOTE: cheat-granted items are local-only ghosts (dev easter eggs). |
| core/Core.cpp | 140 | 140/140 read (was MISSING from the v1 census!), 0 CHG — torch, aim/strike state machine, first-person weapon logic: all local-player mechanics; P2's swings hit via the interception. |
| animation/AnimationRender.cpp | 19 | 19/19 read, 0 CHG — viewer-local rendering (invisibility see-through, halo, book draw); avatar covered by IO_FORCEDRAW and NPC paths. |
| gui/debug/DebugHud.cpp | 14 | debug overlay, 0 CHG. |
| 25 tail files | 57 | 57/57 read, 0 CHG. NOTES: (a) ScriptedInventory destroy-equipped under partner context would unequip the wrong player (rare; backlog), (b) SPEAK -p under partner context targets P1's speech slot (minor; backlog), (c) rune-draw visuals of P2's casting invisible to P1 (cosmetic backlog). |
| scene/Scene.cpp | 10 | first-person render exceptions — 0 CHG. |
| gui/Cursor.cpp | 9 | local cast UI — 0 CHG. |
| gui/CharacterCreation.cpp | 8 | local; guests bypass it entirely via world-transfer join — 0 CHG. |
| game/magic/Spell.cpp | 8 | caster/target pos helpers; avatar rides the entity branch — 0 CHG. |
| game/magic/Precast.cpp | 8 | local precast — 0 CHG. |

**AUDIT v2 CLOSURE.** Fresh line-by-line this pass: ~816 sites (all files never
audited before plus every spell file). The remaining ~1,260 sites live in the
v1-classified files (Player 603, ChangeLevel 128, Interface 123, Damage 73,
Script 55, Interactive 47, ArxGame 52, Spells 33, NPC 30, Inventory 30, and
the already-hooked rest): those were read line-by-line in v1, and this pass
re-verified every one of their co-op hooks is present (hook counts per file
confirmed) plus a world-touch sweep for anything the v1 read missed. Every
one of the 2,076 sites has now been read at least once and carries a verdict;
16 new CHG landed this pass, plus the partner-effect channel and the
status-spell relay. Backlog (cosmetic/rare, all noted above): armor visuals
on the body, arrows in flight, P2 steal reactions, partner-context speak -p,
partner-context unequip, cheat ghosts, replica self-buff halo expiry.

Goal: every one of the 2,076 player-coupling sites classified and, where needed,
converted, so the engine behaves as if it always expected two players.

Legend: **OK** = correct for P2 by construction (refers to the local machine's
own player — P2 *is* that player on its own machine). **CHG** = changed for
co-op. **PEND** = not yet examined line-by-line.

Counts refresh as the pass advances. Sites = references to
`entities.player()` / `EntityHandle_Player` / `player.`.

---

## Fully classified files

| File | Sites | Verdict |
|---|---|---|
| game/NPC.cpp | 30 | 12 CHG (sight per player, chooser targeting, retarget, hearing accepts partner, body metrics, reach tolerance, diagnostics), 18 OK (local player strike anims, torch, own-weapon checks, dragged-item ignition, head-vertex vision path handles the body via its real head) |
| game/Missile.cpp | 1 | 1 CHG (missiles burst near either player) |
| ai/Paths.cpp | 6 | 4 CHG (zone naming, context, reported crossings, avatar excluded from lag-detection), 2 OK (P1's own zone probe — P2 equivalent runs via reports) |
| game/Damage.cpp | 71 | 8 CHG (avatar interception in damageNpc/damageCharacter/damageProp, hit params for partner weapons, death report), rest OK (local player's damage/heal/mana paths — run on the machine that owns the health) |
| script/ScriptedNPC.cpp | 19 | 1 CHG (settarget player → chooser), rest OK (local-context commands) |
| script/Script.cpp | 96 | 8 CHG (nearest-player distances, sender identity, partner-context vitals), rest OK (local player state reads for the local machine) |
| script/ScriptedIOControl.cpp | 9 | 1 CHG (teleport -l routed to the acting player), rest OK |
| script/ScriptedCamera.cpp | — | 1 CHG (worldfade stays on the traveller's screen) |
| script/ScriptedAnimation.cpp | 4 | 1 CHG (one-shot zone unset per player), 3 OK |
| script/ScriptedPlayer.cpp | 19 | 1 CHG (invulnerability -p context), rest OK (player commands running on the player's own machine) |
| physics/Collisions.cpp | 15 | 2 CHG (one-way solidity of the body; on a replica the local player never body-blocks on stale creature copies — the authority owns that contact), rest OK (local player movement physics) |
| scene/Interactive.cpp | 47 | 5 CHG (treat zone covers both players, partner + weapon always treated), rest OK (local rendering/update skips, price/armor queries for local player) |
| scene/ChangeLevel.cpp | 128 | 4 CHG (per-process save file, area leave/load hooks, marker pass-through), rest OK (saving/loading this machine's own player) |
| core/ArxGame.cpp | 52 | 7 CHG (poll/flush, replica gating, avatar update, partner HUD, fade breaker, probe), rest OK (local camera, input, HUD) |
| game/EntityManager.cpp | 1 | 1 CHG (name "player" honours the acting-player context) |
| game/Spells.cpp | 33 | 2 CHG (cast/end replication), 31 OK — incl. TemporaryGetSpellTarget: iterates NPC-flagged entities, so the partner body is already a candidate target for hostile casters |
| game/Entity.cpp | 6 | 1 CHG (destruction announced), 5 OK |
| game/Player.cpp | 603 | 6 CHG (XP/gold/quest/keyring sharing, vitals on spawn), 597 OK — the player implementation itself; it runs for whichever human owns the machine |
| game/Inventory.cpp | 23 | 3 CHG (pickup/drop replication), rest OK (local inventory mechanics) |
| gui/MiniMap.cpp | 2+ | 1 CHG (partner drawn on map), rest OK |
| gui/Interface.cpp | 123 | 2 CHG (action relay, chat stays P1 by design), rest OK (local player's HUD/controls) |

## Pending line-by-line classification

| File | Sites | Expected character |
|---|---|---|
| gui/book/Book.cpp | 132 | local character sheet UI — expect all OK |
| game/Equipment.cpp | 81 | local equip mechanics; damage paths already intercepted — expect OK, verify ComputeDamages branches |
| gui/Hud.cpp | 67 | local HUD — expect all OK |
| gui/hud/PlayerInventory.cpp | 52 | local UI — expect OK |
| game/magic/spells/SpellsLvl01–10.cpp | 143 | caster==player branches (local caster OK); target==player effect branches — verify NPC-cast effects on the partner body route correctly |
| gui/hud/SecondaryInventory.cpp | 25 | local UI + container sharing gap (known open issue) |
| game/spell/Cheat.cpp | 25 | local cheats — expect OK |
| physics/Projectile.cpp | 18 | arrows: local shooter stats OK; verify target==player branch vs partner body |
| remaining gui/graphics/animation/cinematic files | ~300 | local presentation — expect OK |

## Netcode model (protocol 8)

Full industry-standard model, implemented in five stages:

5. **Adaptive layer** (`CoopInterp.h/cpp` shared MotionTrack) — draw delay is
   measured (2 send beats + worst recent delivery lateness, decaying 10ms/s;
   floor 100ms world / 80ms body, ceiling 450ms); brief dead reckoning
   (≤250ms velocity extrapolation) across lost packets with a pop-free error
   fade (~0.3s) when truth resumes; shortest-arc angle blending; snapshots
   are difference-only with a carry-everything keyframe every 15th tick;
   rates raised to 20 Hz world / 30 Hz body; `[coop-net]` logs
   ping/jitter/delays every 5s.

Original four stages:

1. **Entity interpolation** — every snapshot/avatar packet carries the
   sender's clock; the receiver keeps an 8-sample history per entity and
   draws the shared world 150ms in the past of the authority's timeline
   (body: 120ms), blending between the two samples around the drawn moment.
   Late/out-of-order snapshot packets are dropped by stamp.
2. **Body interpolation** — the partner body rides the same sample-buffer
   timeline instead of lerp-toward-latest.
3. **Host corrections** — the authority's separation pass splits overlap
   resolution: the creature is eased out locally (forcedmove) and the guest
   receives MsgPlayerPush to step out on their own screen.
4. **Hit validation** — applyHitRequest rejects claims about targets more
   than 2500 units from the claiming player's body (desync artefacts);
   client-side hit detection against the interpolated past is the lag
   compensation.

## Problem 4, the REAL root (round 14): the unflushed announcement

Live-log diagnosis of the failed retest showed no travel-hold and no
handover-replay lines at all. Two stacked misses: (1) onAreaLeaving WROTE
the departure announcement but never FLUSHED it — it sat queued while the
host's 10s load blocked the game, so the guest stayed a muted replica the
whole window (fix: enet_host_flush right after reportAreaChange); (2) a
faller crosses a door zone in under a second, so by handover they are BELOW
it — "the zone under our feet" was nothing (fix: reportLocalZone remembers
the last zone ENTERED while replica + when; on handover, if no travel
answered it within 10s, the guest fires that zone's script locally via
ARX_PATH_EntityEnterZone). Cleared on MsgTravel/area load/stop.

## Crash fix (round 13): avatar-weapon teardown order

Guest crashed (AV in unlinkEntity) applying the host's world: save-load
teardown frees entities in list order, the avatar body died before its
linked weapon, and the weapon's destructor unlinked itself from freed
memory. Fix: ARX_CHANGELEVEL_Load calls coop::destroyAvatarEntity() (safe
order) before the sweep, and the avatar's weapon is now IO_NOSAVE so it can
never enter a savegame referencing an unloadable owner (the "Unable to read
coop_player_0001" log error; one legacy save still carries it, benign).

## Travel overkill (round 12)

- **Arrival protection**: 2.5s after any co-op area load the arriving player
  takes zero damage (damagePlayer zero-out) and is invisible to creature
  sight (CheckNPCEx gates), self and partner symmetric via
  selfArrivedAt / partnerPresentSince clocks.
- **Panic rescue**: hold H 3s → teleport beside the partner (validated via
  AttemptValidCylinderPos); cross-area works for the guest via the
  travelToHost flow; host cross-area politely declines (its travel path is
  guest-specific by design).
- **Partner location on HUD**: "(elsewhere)" → "(in area N)".
- **Travel-hold refinement** (double-check catch): a partner-context fade IN
  now sends TravelCancel, so a purely cosmetic fade releases the hold at
  fade-in instead of the 4s timeout.
- **Zone audit REJECTED** (double-check catch): host and guest zone states
  are INTENTIONALLY divergent (per-player one-shot doors) — auditing them
  would "repair" correct behaviour.

## Problem 4 root fix (round 11): authority-handover zone replay

Zone triggers are edge-triggered; if P2 crossed into a travel zone while the
host was still authority, the edge fired on the host and died with its level
load. Fix: noteRemoteArea() (CoopNet.cpp) — the moment the host's departure
makes the guest the authority of its area, the guest forgets the zone under
its player's feet (entities.player()->inzone = nullptr), so its own engine
sees a fresh entry next frame and runs the door locally. Combined with the
round-10 travel hold, jump order no longer matters.

## Problem 3 root fix (round 10, protocol 12): travel hold

The stock hole-transitions assume the faller stops being simulated almost
immediately (fade + load freeze the host mid-air). The guest's travel needs
a round trip + the script's own ~700ms timer, and their machine keeps
simulating the fall throughout — so they finish a fall nobody was meant to
finish and take real damage. Fix: MsgTravelHold, sent the instant the zone
script runs its fade-out in the partner's context (backstop: sent with any
non-confirm MsgTravel). The guest fades its own screen and freezes its
player exactly as a load would (movement pass skipped in
ARX_PLAYER_Manage_Movement + BLOCK_PLAYER_CONTROLS, which the engine itself
treats as damage-proof). Released on area load / travel cancel / 4s safety
timeout (resumes the fall unharmed). Confirmed door prompts don't freeze —
the player needs controls to answer them.

## Final netcode round (round 9, protocol 11) — the list is exhausted

- **Combat-priority beat**: a creature fighting within 1200 units of either
  player raises snapshots to 60 Hz (16ms); each MsgEntities carries its beat
  so the guest's draw delay tightens to ~57ms+jitter in fights and relaxes
  to ~125ms out of them. Keyframes went time-based (750ms) so the faster
  beat does not multiply them.
- **Instant action acknowledgment**: P2's use-clicks play feedback the same
  frame; the result stays host-authoritative (scripts are irreversible, so
  true rollback prediction is off the table by design, same as MMOs).
- **Seamless reconnect**: an unexpected mid-game drop puts the guest into a
  quiet retry loop (every 5s for up to 2 minutes); MsgHello carries a resume
  flag; resume skips the reload-and-teleport join, forces a fresh keyframe,
  and the audit heals drift. Host keeps simulating alone meanwhile.
- **Bad-network rehearsal**: ARX_COOP_LAG_MS / ARX_COOP_LOSS_PCT env vars
  simulate latency (all channels) and snapshot loss (unreliable only) on
  one desk.
- **Session recorder**: ARX_COOP_RECORD=1 appends every received packet
  (stamped) to coop-record-<role>.bin; game/coop-record-dump.py decodes a
  timeline + bandwidth summary + snapshot-gap detection for offline
  diagnosis of any reported weirdness.

**Deliberately rejected** (so "nothing left" is an informed claim, not
laziness): float quantization (compression already removes what it would;
adds precision-bug risk for bytes), in-engine replay playback (needs
bit-identical initial save state to be truthful; the offline decoder covers
diagnosis), host migration and 3–4 players (feature expansions, not netcode
quality — separate decisions).

## Overkill round (round 8, protocol 10)

- **Combat FX replication** (MsgWorldFx): the low-level emitters themselves
  are hooked — both ARX_SOUND_PlayCollision overloads (before the LOCAL
  audibility check, since our camera may be far while theirs is close) and
  both blood spawners. The authority mirrors every combat sound and blood
  burst to the guest; the guest performs them with its own camera rules.
  One-way (authority→replica), ApplyScope + authority gate prevent loops.
  Known cosmetic: P2's own melee hits may show doubled particles (local
  prediction + broadcast) — revisit only if visible.
- **Catmull-Rom interpolation** (CoopInterp.cpp): positions curve through
  four samples when the beat is steady (gap guard ×3), straight-line
  fallback otherwise — circling creatures stop cutting micro-corners.
- **Desync self-healing** (MsgWorldAudit): every 5s the guest reports its
  replicated world (pos/life/show/flags per entity); the host diffs with
  tolerances (250 units for the interpolation past-lag, 2 life), logs every
  divergence as [coop-sync], evicts diverged entities from the sent-state
  cache (forcing resend) and repeats destruction orders for ghosts.
  Divergence is now impossible to miss and repairs itself.

## Problem 2 root fix (round 7, protocol 9): timeline-exact animation

Animation bookkeeping is no longer copied late — it is performed on time.
Every entity replicates all four animation layers as (clip index, variant,
flags, START time on the authority's clock), derived from packet stamp minus
playhead. Because the replica draws ~150ms in the past and news arrives
within ~50ms, every clip start is known BEFORE the drawn timeline reaches
it, so `driveReplicatedAnim` (CoopWorld.cpp) performs each clip at its exact
moment: same clip, same variant, same phase (loops wrapped modulo duration,
one-shots clamped), corrections only past 60ms drift so playback stays
frame-rate smooth. A clip that "has not happened yet" on the drawn timeline
waits its turn — the sword wind-up starts at the same point of the approach
on both screens.

## Co-op features (round 6)

- **Partner revive**: while the other player still stands, death holds short
  of the menu; partner standing over the body for 2s revives at 50% health
  (updateReviveOpportunity in CoopPlayer.cpp, hooked in
  ARX_PLAYER_Manage_Death; death camera clamped in handlePlayerDeath).
  A solo death (or both down) still ends the game.
- **Compression**: ENet range coder + CRC32 on every packet, both ends.
- **Ping readout** on the partner HUD label.
- **Container loot fix**: reportPickup now reports items taken from OTHER
  entities' inventories (corpse, chest) and only skips the player's own
  bag-to-bag moves — the root cause of once-per-player looting.

## Open shared-world issues (from testing)

1. ~~Enemy can still overlap P2 in melee (lag-chase)~~ — root fix applied:
   guest movement ignores replicated creature cylinders (client prediction
   rule), and the authority eases any creature overlapping the partner body
   back out via forcedmove. Awaiting live test.
2. NPCs vanishing at distance on P2's screen — probe fields added, awaiting a
   reproducing session log.
3. Highlight replication reported not working — needs re-verification against
   what SETINTERACTIVITY actually toggles.
4. ~~Containers/corpses not shared~~ — root cause found and fixed (round 6):
   reportPickup treated an item inside a corpse/chest inventory as
   "already carried" and never reported the take. Awaiting live test.
   Remaining known gap: STASHING an item INTO a container only exists on the
   stasher's machine (needs an item-spawn path to mirror; rare in play).
5. Highlight replication: investigated — setinteractivity is a pure
   GFLAG_INTERACTIVITY toggle and that flag IS replicated (and now delta
   snapshots ship it within 50ms of the change). Needs a fresh repro with
   the exact object that failed before this can go further.

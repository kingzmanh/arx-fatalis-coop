/*
 * Copyright 2026 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARX_NET_COOPNET_H
#define ARX_NET_COOPNET_H

#include <set>
#include <string>
#include <vector>
#include <string_view>

#include "game/GameTypes.h"
#include "gui/Speech.h"
#include "graphics/BaseGraphicsTypes.h"
#include "math/Angle.h"
#include "math/Vector.h"
#include "platform/Platform.h"

class Entity;

/*!
 * Two player online co-op.
 *
 * The shape of it
 * ---------------
 * Both machines run the whole game. Neither is a thin client: each one loads
 * levels, animates, renders and plays sound for itself. What is shared is the
 * *world*: the entities in it, the story progress, and the two bodies walking
 * around in it.
 *
 * Authority is per area rather than global, because the players are allowed to
 * be in different parts of the fortress at the same time. The rule is:
 *
 *  - When both players are in the same area, the host simulates that area's
 *    entities and the guest applies what it is sent. One shared set of enemies,
 *    one shared set of doors.
 *  - When the players are in different areas, each machine simulates its own
 *    area on its own. Neither can see the other, so there is nothing to agree
 *    about beyond the story flags.
 *  - Crossing between the two is a handover: the machine leaving an area sends
 *    that area's saved state to the host, which folds it into the shared
 *    savegame, and the machine entering an area asks the host for its state
 *    first. That is the same push/pop the engine already performs on every
 *    level change, so an area looks the same no matter who walked through it.
 *
 * What each machine owns outright
 * -------------------------------
 * Its own character. Health, mana, experience, level, skills, gold, inventory
 * and equipment live on the machine of the player they belong to and are never
 * overwritten by the other side. Only a compact summary is replicated, enough
 * to draw the other player's body and show their health. That is also what
 * makes each player's progress save on their own machine.
 */
namespace coop {

enum class Role {
	Offline,
	Host,
	Guest
};

//! What the session is doing right now, for the menu and the HUD.
enum class Status {
	Offline,
	Listening,   //!< host is waiting for someone to join
	Connecting,  //!< guest has sent a connection request
	Syncing,     //!< the world is being transferred
	Playing,
	Failed       //!< see statusText() for what went wrong
};

/*!
 * Start listening for one joining player.
 *
 * \return false if the port could not be bound, which in practice means another
 *         copy of the game is already hosting on this machine.
 */
bool startHost(unsigned short port = 27100);

/*!
 * Connect to a host.
 *
 * The address may carry a port, as in "192.168.1.50:27100"; without one the
 * default is used. Returns false only for an address that cannot be resolved at
 * all - a host that is simply not there fails later, in poll(), because the
 * connection attempt is asynchronous.
 */
bool startClient(const std::string & address);

//! Leave the session and release the network stack. Safe to call when offline.
void stop();

/*!
 * Pump the network once per frame.
 *
 * Called from the main loop before the world is updated, so that everything
 * received is already applied by the time this frame simulates and draws.
 */
void poll();

//! Send anything that has to leave this frame. Called after the world updated.
void flush();

/*!
 * Feed one clock stamp just read from a remote packet, in the remote
 * machine's own milliseconds.
 *
 * Every snapshot-channel packet carries the sender's clock. From those this
 * machine keeps a running estimate of what the other machine's clock reads
 * right now, which is what lets replicated entities be drawn a fixed step in
 * the past of THEIR timeline rather than a jittery step in the past of ours.
 */
void noteRemoteClock(u32 stampMs);

//! Best estimate of the other machine's clock right now; 0 until the first stamp.
s64 estimatedRemoteNowMs();

//! How far in the past replicated entities are drawn; adapts to measured jitter.
s64 entityInterpDelayMs();

//! Likewise for the other player's body, which reports more often.
s64 bodyInterpDelayMs();

//! Smoothed round-trip time to the other machine, in ms; 0 when offline.
u32 pingMs();

/*!
 * The shared story ledger.
 *
 * A one-shot story sequence a player has lived through is a FACT of this
 * playthrough, for both players: recorded when it really plays, broadcast to
 * the other machine, persisted by the host, wiped by a new game. A script
 * that tries to play a ledgered sequence completes instantly instead - its
 * own follow-up runs the same frame, so nothing is ever left waiting on a
 * cutscene that will not come.
 */
bool isCutsceneSeen(std::string_view name);
void reportCutsceneSeen(std::string_view name);
void clearStoryLedger();

/*!
 * Effects on the partner BODY belong to the partner PLAYER, one machine away.
 *
 * The body standing here is a puppet: its health and stats are display
 * copies. A heal, a food conjure, a status effect landing on it must travel
 * to the machine that owns the real player. Heals accumulate and flush in
 * batches so a per-frame healing aura does not become sixty messages a
 * second.
 */
enum PartnerEffectKind : u8 {
	PartnerFxHeal = 0,
	PartnerFxMana = 1,
	PartnerFxFillHunger = 2,
};
void reportPartnerHeal(float amount);
void reportPartnerEffect(u8 kind, float value);

//! Host: read the persisted ledger once at hosting start.
void loadStoryLedger();

/*!
 * The playthrough's identity: a random id minted at New Quest on the host,
 * persisted beside the story ledger, handed to every guest in the welcome.
 * The guest keys its saved character to it, which is what makes "join the
 * same game again, your progress still exists" true.
 */
const std::string & playthroughId();

//! One host answering on the local network.
struct DiscoveredHost {
	std::string name;
	std::string address; //!< "ip:port", ready to join
	int level = 0;
};

/*!
 * LAN discovery. Hosting broadcasts a small beacon every two seconds;
 * calling discoveredHosts() (the co-op menu does, every frame it is open)
 * listens for those beacons and returns who is out there right now, pruned
 * of anyone silent for five seconds. Click a name, join their game.
 */
std::vector<DiscoveredHost> discoveredHosts();

/*!
 * Both players watch story moments together, the way every co-op RPG shows
 * its cutscenes. When a cinematic or locking speech starts on one machine,
 * the other receives a VIEWER COPY: same speaker, same line, same camera -
 * but no script continuation, because the story itself advances only where
 * the sequence really runs. The viewer is held (controls, borders) exactly
 * as long as the show lasts.
 */
void reportCutscenePlay(const std::string & speakerId, const std::string & data,
                        long mood, u32 flags, const CinematicSpeech & cine);
//! Poll-side: release the viewer hold once the show is over.
void updateCutsceneViewer();

/*!
 * Guest only: the local player is pressing against this replicated creature.
 *
 * Blocking is local (the creature's cylinder is solid on this machine), but
 * CONTACT - SM_COLLIDE_NPC script events, touch damage from fiery or spiked
 * creatures - is a world ruling, so the host runs the real handleNpcCollision
 * between its avatar body and the creature. Results return as normal state.
 */
void reportNpcTouch(Entity & npc);

//! Sync plumbing for the ledger (used by the world-state blob).
const std::set<std::string, std::less<>> & seenCutsceneNames();
void adoptSeenCutscene(std::string name);

//! ARX_COOP_DEBUG=1: trace every event, watched command and player-lock change.
bool debugTrace();

//! Order the other machine to destroy an entity by id (audit repair path).
void sendEntityGone(std::string_view id);

/*!
 * Authority only: mirror a combat sound or particle burst to the guest.
 *
 * The simulation runs on one machine, and the sound and blood of a fight are
 * born inside that simulation - without these, a battle the other player is
 * watching is half silent. Each call is a fire-and-forget event; the guest
 * decides audibility with its own camera the way it would for local effects.
 */
void broadcastCollisionMats(u8 mat1, u8 mat2, float volume, float power, const Vec3f & pos);
void broadcastCollisionNames(std::string_view name1, std::string_view name2, float volume,
                             float power, const Vec3f & pos);
void broadcastBlood(const Vec3f & pos, float dmgs, std::string_view sourceId);
void broadcastBlood2(const Vec3f & pos, float dmgs, u32 color, std::string_view targetId);

[[nodiscard]] Role role();
[[nodiscard]] Status status();

//! Human readable one liner for the menu, e.g. "CONNECTED - Player 2".
[[nodiscard]] const char * statusText();

//! In a session at all, whether or not the other player has finished joining.
[[nodiscard]] inline bool isActive() { return role() != Role::Offline; }

[[nodiscard]] inline bool isHost() { return role() == Role::Host; }
[[nodiscard]] inline bool isGuest() { return role() == Role::Guest; }

//! Both players are connected and the world has been synchronised.
[[nodiscard]] bool isPlaying();

//! The other player's current area, or a default AreaId when not playing.
[[nodiscard]] AreaId remoteArea();

//! Both players are standing in the same area, so they can see each other.
[[nodiscard]] bool sharingArea();

/*!
 * True when this machine must not simulate the world around it: entity AI,
 * physics and scripts belong to the host for this area and arrive as
 * replicated state instead.
 *
 * Only ever true on the guest, and only while both players share an area.
 * A guest exploring on its own simulates normally.
 */
[[nodiscard]] bool isReplica();

/*!
 * True when this machine decides what happens in the area it is in.
 *
 * The host always is. A guest is too, but only while it is somewhere the host
 * is not.
 */
[[nodiscard]] inline bool hasWorldAuthority() { return !isReplica(); }

// -- outgoing world mutations -------------------------------------------------
//
// Each of these returns true when the request was handed to the host, which
// means the caller must *not* also perform the action locally: doing both is
// how an item ends up picked up twice.

//! Ask the authority to run an entity's action script on this player's behalf.
bool requestAction(const Entity & target);

//! Tell the authority this player took an item, so it leaves the shared world.
bool requestTake(const Entity & item);

/*!
 * Say that this player has just put an item down in the world.
 *
 * An announcement, not a request. Both machines already have the entity - they
 * loaded the same world - so the other side moves the copy it already has,
 * keyed by id, and the item stays the one thing it always was.
 *
 * The earlier design had a guest ask the host to place the item and then throw
 * its own copy away. That is what made an object a guest moved snap back: the
 * original never moved, the host spawned a second one, and the guest was left
 * watching the untouched original where it had always been.
 */
/*!
 * \param velocity the impulse the object was released with, or zero when it
 *        was simply placed. Physics runs on the authority alone, so a throw
 *        has to travel as a throw: given only a position, the other machine
 *        can do nothing but set the object down in mid-air, where it stays,
 *        because nobody is left to make it fall.
 */
void reportItemDropped(const Entity & item, const Vec3f & at, const Vec3f & velocity);

//! Ask the authority to apply a hit this player landed on a world entity.
bool requestHit(const Entity & target, float damage, u32 damageType);

/*!
 * Say that a world entity has ceased to exist.
 *
 * Burnt, smashed, consumed by a script - however it went, the other machine
 * has its own copy and no way of knowing. Left untold, that copy stays solid
 * forever: the classic symptom is a set of bars one player has destroyed and
 * the other still cannot walk through.
 *
 * Announced by whoever owns the area, and only for entities that are part of
 * the shared world in the first place.
 */
void reportEntityDestroyed(const Entity & entity);

/*!
 * Tell the guest about an entity that has just appeared in the shared world.
 *
 * Without this a dropped item would exist only on the machine that dropped it,
 * and every later message about it - "it was taken", "it moved" - would name an
 * entity the other side has never heard of. The instance number is carried
 * along so both machines end up calling it by the same name.
 */
void announceSpawn(const Entity & entity);

// -- outgoing notifications ----------------------------------------------------
//
// These are things that already happened locally and the other side is simply
// told about. They do not gate the local action.

//! Damage this player dealt to the *other player's* body.
void reportPlayerDamage(float damage, u32 damageType);

//! This player just died / respawned, so the other one is told.
void reportDeath();
void reportRevive();

//! A spell this player cast, so the other machine can show and apply it.
/*!
 * \param casterId empty means "the sender's own player" (resolved to the
 *                 partner body on arrival); otherwise an entity id, so a
 *                 creature's relayed cast comes from the same creature on
 *                 both machines. A targetId of "player" resolves to the
 *                 receiver's own player.
 */
void reportSpell(int spellType, float level, u32 flags, s64 durationMs,
                 const std::string & targetId, const std::string & casterId = std::string());
void reportSpellEnd(int spellType);

/*!
 * Experience and gold this player earned.
 *
 * Both are granted to the other player as well: the two of them progress
 * together no matter which one landed the killing blow or opened the chest.
 */
void reportReward(long xp, long gold);

//! A quest entry or keyring key was gained, which is shared story progress.
void reportQuest(std::string_view questKey);
void reportKey(std::string_view key);
void reportMapMarker(float x, float y, int level, std::string_view name);

//! Short line of text shown to the other player.
void notifyOther(std::string_view text);

//! Tell the other player which area this one is now in.
void reportAreaChange(AreaId area);

/*!
 * True once, right after a joining player has loaded the host's world.
 *
 * The guest is standing exactly where the host was standing when the world was
 * captured, which would leave the two of them inside each other. The caller
 * consumes this to move the guest clear.
 */
bool takeJoinNudge();

/*!
 * Called by the level loader after an area finished loading, so the session can
 * hand over authority and ask for or publish that area's state.
 */
void onAreaLoaded(AreaId area);

//! Called just before an area is left, so its state can be published.
void onAreaLeaving(AreaId from, AreaId to, std::string_view target = std::string_view());

/*!
 * Order the other player's machine to run a level transition, exactly the way
 * a door orders the first player's.
 *
 * A door script says `teleport -l <level> <marker>`, which writes this
 * machine's travel state - the travel state of whoever is playing HERE. When
 * the second player is the one standing in the door, that write must happen on
 * their machine instead, with the same three facts the first player would get:
 * destination area, destination marker, facing. Their engine then runs the
 * identical stock transition, confirmation icon and all.
 */
void sendTravelOrder(u32 area, std::string_view target, long angle, bool confirm);

/*!
 * Host: tell the guest a travel sequence has just begun for their player.
 *
 * The stock hole-transitions assume the faller stops being simulated almost
 * immediately - the fade and the load freeze the host's player mid-air. The
 * guest's travel needs a round trip plus the script's own delay, and their
 * machine keeps honestly simulating the fall the whole time, which is how
 * they finish a fall nobody was ever meant to finish and land with damage.
 * The hold closes that window: sent the instant the zone script starts its
 * fade in the guest's name.
 */
void sendTravelHold(s32 fadeMs);

//! Guest: whether the local player is suspended mid-air awaiting a travel.
bool travelHoldActive();

/*!
 * A moment of calm after a loading screen, the way every co-op game shields
 * an arrival: for 2.5s the freshly arrived player takes no damage and is
 * invisible to creature sight (existing aggro is untouched).
 */
bool localPlayerArrivalProtected();
bool partnerArrivalProtected();

//! The other player left the doorway before travelling; withdraw the offer.
void sendTravelCancel();

//! A moment of captured speech, on its own unreliable channel.
void sendVoice(const u8 * data, size_t size);

/*!
 * Report the local player's zone, once per frame, while this machine is a
 * replica.
 *
 * A replica's own zone checks are muted - the host runs the world - but only
 * this machine knows the exact frame its player crossed a trigger. Reporting
 * the crossing lets the host fire it immediately instead of noticing the
 * replicated body a few hundred milliseconds later, which was the difference
 * between the second player's doors firing mid-fall like the first player's
 * and firing after they had already hit the ground.
 */
void reportLocalZone();

} // namespace coop

#endif // ARX_NET_COOPNET_H

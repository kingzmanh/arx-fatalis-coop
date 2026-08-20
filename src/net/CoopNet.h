/*
 * Copyright 2026 kingzmanh
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
#include "io/fs/FilePath.h"
#include "platform/Platform.h"

class Entity;
class Zone;

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
	PartnerFxGold = 3,       //!< value is a gold delta; negative is a payment made in their name
};
void reportPartnerHeal(float amount);
void reportPartnerEffect(u8 kind, float value);

//! Host: read the persisted ledger once at hosting start.
void loadStoryLedger();

/*!
 * Keep co-op's own memory with the savegame it belongs to - which sequences
 * have been watched, and what the guest is carrying. Without this, loading an
 * older save rolls the world back but not the story, and quests that were
 * already spoken through can never be spoken through again.
 */
void saveSideState(const fs::path & saveFolder);
void loadSideState(const fs::path & saveFolder);

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

/*!
 * Whether a story moment starting right now belongs on THIS screen.
 *
 * Asked by the handful of commands that make a cutscene a cutscene rather than
 * a conversation: the controls going dead, the bars coming down, the hands
 * going away. Answered on whichever machine is running the script, which in a
 * shared area is always the host.
 *
 * Apart, the question does not arise - each of them owns the ground they stand
 * on and plays their own scenes - so this says yes and stays out of the way.
 */
[[nodiscard]] bool presentsCutscene();

/*!
 * Whether the other player should be sent this scene to watch.
 *
 * Their copy is a performance, not a script: it holds them for exactly as long
 * as the show and lets them go by itself. That is what makes it safe to give a
 * guest a cutscene at all - the machinery that would end a real one over there
 * is muted, and a lock with no key is how a player ends up staring at black
 * bars until the watchdog cuts them loose.
 */
[[nodiscard]] bool relaysCutscene();

/*!
 * Remember that a scene is running here for the other player, not for us.
 *
 * What tells the engine it is inside a story moment rather than a passing
 * remark is that the controls are dead and the bars are down. Declining both
 * on their behalf takes that signal away with them - and the signal is also
 * what decides that a line is worth sending them at all, so without this the
 * scene would be suppressed here and never sent anywhere.
 */
void noteCutsceneForPartner(bool active);

//! Whether a scene is running here for the other player right now.
[[nodiscard]] bool isPartnerCutscene();

/*!
 * Point the other player's eyes at the camera this scene is shot through.
 *
 * The lines and the bars were already being sent; without this the scene they
 * are watching is framed from wherever they happen to be standing, which for a
 * conversation staged around a doorway is usually a wall. An empty name gives
 * them their own eyes back.
 */
void reportCutsceneCamera(const Entity * camera);

/*!
 * The camera a scene of theirs is being shot through, if any.
 *
 * Cameras are normally left out of the snapshot - nobody looks through the
 * other machine's camera, so it has no visible state worth sending. One handed
 * to the other player is the exception, and the exception matters: a camera
 * that does not move is the whole scene not moving.
 */
[[nodiscard]] const Entity * partnerCameraEntity();

/*!
 * Whether a scene of ours is being performed on the other machine right now.
 *
 * While it is, the screen is not ours to redress: our own copy of the same
 * script - zones run on both machines - would otherwise raise the bars the
 * moment they came down.
 */
[[nodiscard]] bool isSceneHeld();

/*!
 * Ask the machine performing our scene to move it along.
 *
 * Skip belongs to whoever is watching, and the scene is not theirs to end:
 * the bars go up when the script reaches the end of its line, and that
 * script is running one machine away.
 */
void reportSceneSkip();

/*!
 * Tell the other player their scene has begun, or that it is over.
 *
 * Standing still under the bars is the rest of what a cutscene is, and it is
 * the one part they cannot run for themselves: the events that would end it
 * are queued over here. So both ends are sent, and their machine gives them a
 * deadline in case the second one is lost.
 */
void reportSceneHold(bool active);

/*!
 * Forget every story moment this playthrough has lived through.
 *
 * A scene plays once and is written down, which is right for playing and
 * hopeless for testing it: the first attempt consumes the very thing the next
 * attempt needs. Reload a save from before it and this lets it happen again.
 */
void forgetCutscenes();

//! Ask the authority to run an entity's action script on this player's behalf.
bool requestAction(const Entity & target);

//! Tell the authority this player took an item, so it leaves the shared world.
bool requestTake(const Entity & item);

/*!
 * Ask the authority to give one of this player's items to a world entity.
 *
 * This is how nearly every quest in the game moves: a form handed to a goblin,
 * a badge to a guard, a gem to a dealer. The script that decides what it means
 * lives with the world, so the give has to be made there - run locally by a
 * guest it convinces nobody but the guest, and the portcullis stays shut on
 * the host's screen while their partner walks into it.
 *
 * The item itself is in their pack, which means this machine destroyed its own
 * copy when they picked it up. The host makes a stand-in bearing the same id
 * and class - the two things a script asks of what it is handed, ISCLASS and a
 * name to DESTROY - and answers whether it was kept.
 */
bool requestCombine(const Entity & source, const Entity & target);

/*!
 * Guest: hand gold to an entity through the host, where the quest lives.
 *
 * The purse balance travels with the request: on the host the script's wallet
 * check (^player_gold) must answer with the GIVER's gold, not the host's, and
 * the amount in the giver's pack at the moment of the click is the truth.
 * Returns false when not a guest sharing the area - the caller then runs the
 * stock local path.
 */
bool requestCombineGold(const Entity & target);

/*!
 * A script running in the partner's name is taking gold. Charge their purse
 * across the wire and leave ours alone. Returns false outside partner context
 * (or for non-payments) - the caller then applies the change locally as ever.
 */
bool chargePartner(long delta);

/*!
 * The giver's purse, while their gold-payment script runs here. Set from the
 * amount their machine sent with the request; ^player_gold answers with it so
 * the script gates on the wallet that will actually pay.
 */
void setPartnerPurse(long gold);
void clearPartnerPurse();
[[nodiscard]] bool partnerPurse(long & gold);

/*!
 * Ask the authority to let this creature talk to us.
 *
 * What an NPC says depends on where its script has got to, and that lives
 * in variables on the entity - who it has met, what it has been given, how
 * far through its dialogue it is. Those belong to the machine that runs it.
 * A guest asking its own copy gets the answers of a goblin who has never
 * met anybody, which is silence.
 */
bool requestChat(const Entity & npc);

//! Answer a give: whoever it was offered to kept it, so it leaves their pack.
void reportCombineTaken(std::string_view sourceId);

/*!
 * Hand an item a script just produced to the other player instead of this one.
 *
 * Only true while running a script on their behalf. "Give it to the player" in
 * a script means the player who earned it, and when they are the one who did
 * the handing over, the reward is not ours to keep.
 */
bool giveToPartner(Entity * item);

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

//! A spell asks the other machine to raise its player.
void askPartnerRevive();

//! A spell asks the other player to be moved to where we stand.
void askPartnerHere(const Vec3f & where);

//! Stand the local player at a spot, if a player can stand there.
bool placePlayerAt(const Vec3f & where);

//! Where a summon asked us to stand in this area, asked once.
bool takeSummonSpot(Vec3f & out);

/*!
 * True once, when the player's next zone crossing should not count.
 *
 * A summoned player appears inside whatever the spell was aimed at, and
 * some of those places are zones that teleport whoever walks in. They did
 * not walk in.
 */
bool takePlayerZoneSwallow();

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

/*!
 * A line of chat typed by this player.
 *
 * Travels as MsgChat and appears on both screens through the game's own
 * notification text, prefixed with the name we introduced ourselves with.
 */
void sendChat(std::string_view text);
//! Tell the other player every rune we know; they keep the union of both.
void reportRunes();
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
 * One-shot travel holes, second player edition.
 *
 * The jail hole's script disarms its own trigger zone after the first jump
 * (UNSET_CONTROLLED_ZONE) - correct for one hero, fatal for the second: when
 * the partner jumps first, the host runs that script in their name and the
 * disarm lands HERE, leaving our own player a hole that triggers nothing.
 *
 * So: a zone disarmed during a partner-context run is remembered
 * (noteZoneDisarmed). If that same run then issues a travel order, the zone
 * has proven itself a travel funnel (noteTravelFunnel) - story zones never
 * send travel orders and stay dead, which is what the cutscene soft-lock
 * taught us. When our own player later steps into a remembered funnel,
 * rearmOwedZone restores the controller so the stock script runs once for
 * them too.
 */
/*!
 * Give the other player their screen back after a scene that never ended.
 *
 * A story chain whose lines are all skipped by the ledger never reaches the
 * block that releases the camera and lifts the bars, so the player it was
 * being performed for is stranded looking through it. Called once the run
 * that built the scene is over; a no-op when nothing is held.
 */
void releasePartnerSceneIfHeld();

void noteZoneDisarmed(std::string_view zoneName, std::string_view controller);
void noteTravelFunnel(const Entity * mover);
void rearmOwedZone(Zone & zone);

/*!
 * Does a forced move by this script owner carry the partner along?
 *
 * The story's forced teleports - captures, the snake-women sending you below,
 * the endgame - assume one hero, and in co-op they would strand the other
 * player wherever the story left them. So: a player-teleport run by an NPC's
 * script (or by meteor_akbaa, the one non-NPC that moves the player for the
 * story) moves BOTH players. Doors, levers, elevators and arrival markers are
 * not NPCs and keep their per-player behaviour; the other player can always
 * operate those for themselves.
 */
[[nodiscard]] bool partyFollowsMover(const Entity * mover);

/*!
 * A story script just teleported this machine's player within the level;
 * order the partner to the same spot. The destination is the script's own
 * marker - a position the level designers placed a player on - never a
 * computed one.
 */
void reportPartyTeleport(const Entity * mover, const Vec3f & pos);

/*!
 * A scene's same-level `teleport -p`, when the scene belongs to the partner.
 *
 * The move (and the facing, when the command carries one) is part of THEIR
 * scene - it must not touch this machine's player, who may be mid-fight and
 * is not even watching. Returns true when the move was sent to their machine
 * instead; the caller then leaves the local player alone. Story movers are
 * not scenes and return false here - the party case teleports locally and
 * reports through reportPartyTeleport().
 */
[[nodiscard]] bool redirectPartnerTeleport(const Entity * mover, const Vec3f & pos, long angle);

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

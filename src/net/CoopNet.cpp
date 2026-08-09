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

#include "net/CoopNet.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include <enet/enet.h>

#include "ai/Paths.h"
#include "core/Core.h"
#include "core/GameTime.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Item.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "graphics/effects/Fade.h"
#include "input/Input.h"
#include "input/Keyboard.h"
#include "physics/Collisions.h"
#include "scene/Interactive.h"
#include "scene/Light.h"
#include "math/Random.h"
#include "scene/GameSound.h"
#include "cinematic/CinematicController.h"
#include "gui/CinematicBorder.h"
#include "gui/Menu.h"
#include "gui/Notification.h"
#include "gui/Speech.h"
#include "io/log/Logger.h"
#include "net/CoopPlayer.h"
#include "net/CoopPortMap.h"
#include "net/CoopVoice.h"
#include "net/CoopProtocol.h"
#include "net/CoopWorld.h"
#include "platform/ProgramOptions.h"
#include "platform/Time.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "scene/ChangeLevel.h"
#include "scene/Scene.h"

namespace coop {

namespace {

/*!
 * How often each machine publishes its own body.
 *
 * 20 Hz is enough for a walking human: the receiving side interpolates between
 * updates, so the body moves smoothly regardless. Sending every frame would
 * triple the traffic for movement nobody can see.
 */
constexpr PlatformDuration AvatarInterval = 33ms;

//! How often the authority publishes the entities around it.
constexpr PlatformDuration SnapshotInterval = 50ms;

/*!
 * Give up on a connection attempt that has not completed in this long.
 *
 * Generous, because the host may be deep inside a level load when the join
 * request arrives, and it cannot answer until the load ends. Eight seconds
 * proved shorter than a cold-cache save load; a joiner giving up while the
 * host is merely busy reads as "no answer" when the answer was coming.
 */
constexpr PlatformDuration ConnectTimeout = 30s;

struct Session {

	ENetHost * host = nullptr;
	ENetPeer * peer = nullptr;

	Role role = Role::Offline;
	Status status = Status::Offline;

	std::string statusText = "OFFLINE";

	//! Set once the other side has acknowledged the handshake.
	bool handshaken = false;

	//! Set on the guest once the host's story state has arrived.
	bool synchronised = false;

	//! Consumed by the level code to move a freshly joined guest off the host.
	bool joinNudge = false;

	/*!
	 * Set once when a guest joins, cleared as soon as they have travelled.
	 *
	 * Joining should put the two players in the same room, not merely in the
	 * same session. It is a one-shot rather than a standing rule, because the
	 * whole point of the rest of the design is that they are then free to walk
	 * off in different directions.
	 */
	bool travelToHost = false;

	AreaId remoteArea;

	//! When this machine last finished loading an area, for the arrival snap.
	PlatformInstant lastAreaLoad = 0;

	/*!
	 * The transition now underway follows a door's destination marker, so the
	 * arrival position is exact and the beside-the-host snap must stay out of
	 * the way. Stamped when the transition starts, consumed when it lands.
	 */
	bool pendingDoorArrival = false;
	bool lastArrivalWasDoor = false;

	//! The zone this machine's player last reported to the authority.
	std::string lastReportedZone;
	
	//! Last zone ENTERED while a replica, in case its travel never comes back.
	std::string lastEnteredZone;
	PlatformInstant lastEnteredZoneAt = 0;

	PlatformInstant connectStart = 0;
	PlatformInstant lastAvatar = 0;
	PlatformInstant lastSnapshot = 0;
	
	//! Remote clock minus local clock, in ms; meaningful once clockValid.
	s64 clockOffsetMs = 0;
	bool clockValid = false;
	
	//! Newest applied snapshot-channel stamps, for dropping late arrivals.
	u32 newestEntitiesStamp = 0;
	u32 newestAvatarStamp = 0;
	
	//! Worst recently seen delivery lateness per stream; pads the draw delay.
	float entityLatenessMs = 0.f;
	float bodyLatenessMs = 0.f;
	PlatformInstant lastLatenessDecay = 0;
	
	//! Difference-only snapshots counting toward the next carry-everything one.
	u32 snapshotTick = 0;
	bool wasSharing = false;
	
	PlatformInstant lastNetLog = 0;
	PlatformInstant lastAudit = 0;
	
	//! Guest-side auto-reconnect after an unexpected drop.
	bool resuming = false;
	ENetAddress reconnectTarget = {};
	PlatformInstant nextReconnectAt = 0;
	PlatformInstant reconnectDeadline = 0;
	
	//! Host: the guest asked for the world during a cutscene; answer after it.
	bool worldRequestDeferred = false;

	//! The current playthrough's identity (host mints it, guest receives it).
	std::string playthroughId;

	//! The port this host listens on, repeated in every discovery beacon.
	u16 listenPort = 0;
	PlatformInstant lastBeacon = 0;

	//! Healing done to the partner body, waiting to travel in one batch.
	float partnerHealAccum = 0.f;
	PlatformInstant partnerHealSince = 0;

	//! Guest: top vitals up once the received world finishes loading.
	bool freshSpawnVitals = false;

	//! Guest: suspended mid-air while a travel completes.
	bool travelHold = false;
	PlatformInstant travelHoldDeadline = 0;
	
	//! Arrival-protection clocks: own last load, partner's last appearance.
	PlatformInstant selfArrivedAt = 0;
	PlatformInstant partnerPresentSince = 0;
	
	//! How long the panic-rescue key has been held.
	PlatformInstant rescueHoldStart = 0;
	
	//! The authority's current snapshot beat, as told in each MsgEntities.
	u8 remoteSnapshotIntervalMs = 50;
	PlatformInstant lastKeyframe = 0;

	std::string localName = "Player";

	//! Reassembly buffer for the story state, which does not fit in one packet.
	std::vector<u8> incoming;
	size_t incomingExpected = 0;

};

Session g_session;

//! ENet is global and must be torn down exactly once.
bool g_enetReady = false;

/*
 * Command line requests, applied on the first frame rather than immediately.
 *
 * Options are parsed long before there is a window, a renderer or a world, and
 * a session that opened at that point would have nothing to be a session about.
 * These hold the request until the game is actually running.
 */
bool g_pendingHost = false;
unsigned short g_pendingPort = DefaultPort;


std::string g_pendingJoin;
std::string g_pendingName;

bool ensureEnet() {

	if(g_enetReady) {
		return true;
	}

	if(enet_initialize() != 0) {
		LogError << "[coop] could not initialise the network stack";
		return false;
	}

	g_enetReady = true;
	return true;
}

void setStatus(Status status, std::string text) {
	g_session.status = status;
	g_session.statusText = std::move(text);
	LogInfo << "[coop] " << g_session.statusText;
}

/*!
 * Pretend-bad-network knobs, read once from the environment.
 *
 * ARX_COOP_LAG_MS delays every outgoing packet by that many milliseconds and
 * ARX_COOP_LOSS_PCT eats that percentage of snapshot-channel packets, so bad
 * connections can be rehearsed on one desk instead of hoped about. Reliable
 * channels are delayed but never dropped - dropping those only makes ENet
 * resend, which is not what real loss looks like to the game.
 */
struct NetSimulator {
	bool initialised = false;
	int lagMs = 0;
	int lossPct = 0;
};
NetSimulator g_netSim;

struct DelayedPacket {
	PlatformInstant releaseAt;
	Channel channel;
	std::vector<u8> bytes;
};
std::deque<DelayedPacket> g_delayedPackets;

//! Everything received gets appended here when ARX_COOP_RECORD is set.
std::FILE * g_recordFile = nullptr;
PlatformInstant g_recordStart = 0;

void initNetSimulator() {
	
	if(g_netSim.initialised) {
		return;
	}
	g_netSim.initialised = true;
	
	if(const char * lag = std::getenv("ARX_COOP_LAG_MS")) {
		g_netSim.lagMs = std::atoi(lag);
	}
	if(const char * loss = std::getenv("ARX_COOP_LOSS_PCT")) {
		g_netSim.lossPct = std::atoi(loss);
	}
	
	if(g_netSim.lagMs > 0 || g_netSim.lossPct > 0) {
		LogWarning << "[coop-net] SIMULATING a bad network: +" << g_netSim.lagMs
		           << "ms latency, " << g_netSim.lossPct << "% snapshot loss";
	}
	
}

void startRecorder(const char * roleName) {
	
	if(g_recordFile) {
		return;
	}
	
	const char * env = std::getenv("ARX_COOP_RECORD");
	if(!env || *env == '\0' || *env == '0') {
		return;
	}
	
	char name[64];
	std::snprintf(name, sizeof(name), "coop-record-%s.bin", roleName);
	g_recordFile = std::fopen(name, "wb");
	g_recordStart = platform::getTime();
	
	if(g_recordFile) {
		LogWarning << "[coop-net] recording every received packet to " << name;
	}
	
}

void recordPacket(u8 channel, const u8 * data, size_t size) {
	
	if(!g_recordFile || size > 0xffff) {
		return;
	}
	
	u32 offset = u32(toMsi(platform::getTime() - g_recordStart));
	u16 length = u16(size);
	std::fwrite(&offset, sizeof(offset), 1, g_recordFile);
	std::fwrite(&channel, sizeof(channel), 1, g_recordFile);
	std::fwrite(&length, sizeof(length), 1, g_recordFile);
	std::fwrite(data, 1, size, g_recordFile);
	std::fflush(g_recordFile);
	
}

void rawSend(const u8 * data, size_t size, Channel channel) {

	if(!g_session.peer) {
		return;
	}

	enet_uint32 flags = (channel == ChannelSnapshot) ? ENET_PACKET_FLAG_UNSEQUENCED
	                                                 : ENET_PACKET_FLAG_RELIABLE;

	ENetPacket * packet = enet_packet_create(data, size, flags);
	if(!packet) {
		return;
	}

	if(enet_peer_send(g_session.peer, channel, packet) < 0) {
		enet_packet_destroy(packet);
	}

}

void pumpDelayedPackets() {
	while(!g_delayedPackets.empty()
	      && platform::getTime() >= g_delayedPackets.front().releaseAt) {
		DelayedPacket & delayed = g_delayedPackets.front();
		rawSend(delayed.bytes.data(), delayed.bytes.size(), delayed.channel);
		g_delayedPackets.pop_front();
	}
}

void send(const Writer & writer, Channel channel) {

	if(!g_session.peer) {
		return;
	}

	if(g_netSim.lossPct > 0 && channel == ChannelSnapshot
	   && Random::getf(0.f, 100.f) < float(g_netSim.lossPct)) {
		return; // eaten by the pretend bad wire
	}

	if(g_netSim.lagMs > 0) {
		DelayedPacket delayed;
		delayed.releaseAt = platform::getTime() + std::chrono::milliseconds(g_netSim.lagMs);
		delayed.channel = channel;
		delayed.bytes.assign(writer.data(), writer.data() + writer.size());
		g_delayedPackets.push_back(std::move(delayed));
		return;
	}

	rawSend(writer.data(), writer.size(), channel);

}

/*!
 * Which level lights are burning, and telling the other player when that changes.
 *
 * A fireplace is not an entity - it is a light the level places, carrying
 * EXTRAS_SPAWNFIRE, and the flames you see are its flare. Lighting one sets
 * m_ignitionStatus on that light, and nothing about it was ever sent anywhere.
 * The guest saw the glow, because the torch entity beside it has an ignition of
 * its own and that IS replicated, but the fire itself never lit.
 *
 * Sent as changes rather than a full list: a level holds hundreds of these and
 * almost none of them ever change, but the handful that do - a fireplace, a
 * brazier, a torch on a wall - are exactly what a player notices.
 */
std::vector<bool> g_sentLightState;

void pollStaticLights() {

	if(!isPlaying() || !hasWorldAuthority()) {
		return;
	}

	if(g_sentLightState.size() != g_staticLights.size()) {
		g_sentLightState.assign(g_staticLights.size(), false);
		// A fresh level: describe every light that is lit, so the guest starts
		// from the same picture rather than from whatever its own copy decided.
		for(size_t i = 0; i < g_staticLights.size(); i++) {
			g_sentLightState[i] = g_staticLights[i].m_ignitionStatus;
			if(g_staticLights[i].m_ignitionStatus) {
				Writer writer(MsgLightIgnite);
				writer.put(u16(i));
				writer.put(true);
				send(writer, ChannelEvent);
			}
		}
		return;
	}

	for(size_t i = 0; i < g_staticLights.size(); i++) {
		bool lit = g_staticLights[i].m_ignitionStatus;
		if(lit != g_sentLightState[i]) {
			g_sentLightState[i] = lit;
			Writer writer(MsgLightIgnite);
			writer.put(u16(i));
			writer.put(lit);
			send(writer, ChannelEvent);
		}
	}

}

void applyLightIgnite(u16 index, bool lit) {

	if(index >= g_staticLights.size()) {
		return;
	}

	EERIE_LIGHT & light = g_staticLights[index];
	if(light.m_ignitionStatus == lit) {
		return;
	}

	light.m_ignitionStatus = lit;
	if(!lit) {
		// Put out properly rather than just marking it dark, or the light it
		// already placed in the world stays behind burning nothing.
		lightHandleDestroy(light.m_ignitionLightHandle);
	}

}

/*!
 * Stop the local player cold, the way a level load does.
 *
 * With controls blocked the engine itself refuses to apply damage to the
 * player, and with the movement pass skipped (see ARX_PLAYER_Manage_Movement)
 * there is no motion, no landing and no fall-damage calculation at all. The
 * deadline is a safety net: if the promised travel never arrives, the player
 * simply resumes falling, unharmed by the code.
 */
void engageTravelHold(s32 fadeMs) {
	if(!g_session.travelHold) {
		LogInfo << "[coop] travel hold: suspended while the level change begins";
	}
	g_session.travelHold = true;
	g_session.travelHoldDeadline = platform::getTime() + std::chrono::milliseconds(fadeMs + 4000);
	BLOCK_PLAYER_CONTROLS = true;
	player.physics.velocity = Vec3f(0.f);
	player.physics.forces = Vec3f(0.f);
}

void releaseTravelHold(bool fadeBackIn) {
	if(!g_session.travelHold) {
		return;
	}
	g_session.travelHold = false;
	BLOCK_PLAYER_CONTROLS = (player.lifePool.current <= 0.f);
	if(fadeBackIn) {
		fadeRequestStart(FadeType_In, 500ms);
	}
}

//! Convenience for the many messages that are a type and nothing else.
void sendBare(MessageType type, Channel channel = ChannelEvent) {
	Writer writer(type);
	send(writer, channel);
}

/*!
 * Break a blob into packets ENet is happy with and send it.
 *
 * The story state is small in a fresh game and grows with the playthrough; by
 * the end it is comfortably larger than a datagram. Chunking it keeps the
 * transfer working at any size without depending on ENet's fragmentation
 * limits.
 */
void sendBlob(const std::vector<u8> & blob) {

	constexpr size_t ChunkSize = 8192;

	{
		Writer begin(MsgWorldBegin);
		begin.put(u32(blob.size()));
		send(begin, ChannelControl);
	}

	for(size_t offset = 0; offset < blob.size(); offset += ChunkSize) {
		size_t count = std::min(ChunkSize, blob.size() - offset);
		Writer chunk(MsgWorldChunk);
		chunk.put(u16(count));
		chunk.putRaw(blob.data() + offset, count);
		send(chunk, ChannelControl);
	}

	sendBare(MsgWorldEnd, ChannelControl);

}

void sendGlobalState() {

	/*
	 * The joining player receives the WORLD ITSELF: the host's playthrough
	 * written to a savegame and shipped whole. A save is the complete truth -
	 * position, levels, script state, consumed one-shot story moments - so
	 * the guest loads it exactly as if it had always been its own game.
	 * Joining needs no menu and no new game, and can never replay a story
	 * moment the host has already lived, because the world it receives has
	 * already lived it.
	 */
	fs::path savefile = fs::getUserDir() / "coop_sync" / "gsave.sav";
	if(!fs::exists(savefile.parent())) {
		fs::create_directories(savefile.parent());
	}
	if(!ARX_CHANGELEVEL_Save("coop", savefile)) {
		LogError << "[coop] could not write the sync save";
		return;
	}

	std::string bytes = fs::read(savefile);
	if(bytes.empty()) {
		LogError << "[coop] could not read back the sync save";
		return;
	}

	LogInfo << "[coop] sending the world: " << bytes.size() << " bytes of savegame";
	std::vector<u8> blob(bytes.begin(), bytes.end());
	sendBlob(blob);

	// The story ledger rides along, one fact per message.
	for(const std::string & name : seenCutsceneNames()) {
		Writer writer(MsgCutsceneSeen);
		writer.put(std::string_view(name));
		send(writer, ChannelEvent);
	}

}

void sendAvatar() {

	Avatar local;
	captureLocalAvatar(local);

	Writer writer(MsgAvatar);
	writer.put(u32(toMsi(platform::getTime())));
	writer.put(u32(local.area.handleData()));
	writer.put(local.pos);
	writer.put(local.angle);
	writer.put(local.life);
	writer.put(local.maxLife);
	writer.put(local.mana);
	writer.put(local.maxMana);
	writer.put(s16(local.level));
	writer.put(local.anim0);
	writer.put(local.anim1);
	writer.put(local.anim3);
	writer.put(local.anim3Flags);
	writer.put(local.anim3Time);
	writer.put(local.anim0Flags);
	writer.put(local.anim1Flags);
	writer.put(local.anim0Time);
	writer.put(local.anim1Time);
	writer.put(local.dead);
	writer.put(local.combat);
	writer.put(local.invisibility);
	writer.put(local.skin);
	writer.put(std::string_view(local.weapon));
	writer.put(std::string_view(local.helmet));
	writer.put(std::string_view(local.armour));
	writer.put(std::string_view(local.leggings));
	writer.put(std::string_view(local.shield));

	send(writer, ChannelSnapshot);

}

/*!
 * Track where the other player is, and notice the moment their departure
 * makes this machine the authority of the area it is standing in.
 *
 * Zone triggers are edge-triggered: they fire the frame a body crosses in.
 * If our player crossed in while the other machine was still the authority,
 * the edge fired over there - and died there when their level load tore the
 * area down. By the time authority lands on us we are standing INSIDE the
 * zone and no new edge will ever come. So on handover, forget the zone under
 * our feet: next frame our own engine sees a fresh entry and runs the door
 * itself, as it is now entitled to. This is what makes the game stop caring
 * who jumps into the hole first.
 */
void noteRemoteArea(AreaId area) {

	bool wasSharing = g_currentArea && g_session.remoteArea == g_currentArea;
	g_session.remoteArea = area;

	if(wasSharing && area != g_currentArea && isGuest() && entities.player()) {
		// Fresh edges for whatever we stand in now...
		entities.player()->inzone = nullptr;
		// ...and justice for the zone we crossed while the world was still
		// theirs. Its entry edge fired over there and died with their area;
		// if no travel came back for it, the door never answered anyone.
		// Fire it here, now that it answers to us.
		if(!g_session.lastEnteredZone.empty()
		   && platform::getTime() - g_session.lastEnteredZoneAt < 10000ms) {
			if(Zone * zone = getZoneByName(g_session.lastEnteredZone)) {
				LogInfo << "[coop] authority handover: replaying zone " << zone->name;
				ARX_PATH_EntityEnterZone(entities.player(), zone);
			}
		}
		g_session.lastEnteredZone.clear();
	}

}

void receiveAvatar(Reader & reader) {

	u32 stamp = reader.getU32();
	noteRemoteClock(stamp);

	/*
	 * Snapshot-channel packets are unsequenced: one that took the long way
	 * round the network can arrive after its successor. Applying yesterday's
	 * position over today's reads on screen as a glitch backwards, so anything
	 * older than what has already been applied is dropped whole.
	 */
	if(g_session.newestAvatarStamp && s32(stamp - g_session.newestAvatarStamp) <= 0) {
		return;
	}
	g_session.newestAvatarStamp = stamp;

	float lateness = float(estimatedRemoteNowMs() - s64(stamp));
	if(lateness > g_session.bodyLatenessMs) {
		g_session.bodyLatenessMs = lateness;
	}

	Avatar & remote = mutableAvatar();

	AreaId area = AreaId(reader.getU32());
	Vec3f pos = reader.getVec3f();
	Anglef angle = reader.getAnglef();
	float life = reader.getFloat();
	float maxLife = reader.getFloat();
	float mana = reader.getFloat();
	float maxMana = reader.getFloat();
	s16 level = reader.getS16();
	u8 anim0 = reader.getU8();
	u8 anim1 = reader.getU8();
	u8 anim3 = reader.getU8();
	u16 anim3Flags = reader.getU16();
	s32 anim3Time = reader.getS32();
	u16 anim0Flags = reader.getU16();
	u16 anim1Flags = reader.getU16();
	s32 anim0Time = reader.getS32();
	s32 anim1Time = reader.getS32();
	bool dead = reader.getBool();
	bool combat = reader.getBool();
	float invisibility = reader.getFloat();
	u8 skin = reader.getU8();
	std::string weapon = reader.getString();
	std::string helmet = reader.getString();
	std::string armour = reader.getString();
	std::string leggings = reader.getString();
	std::string shield = reader.getString();

	if(!reader.ok()) {
		return;
	}

	remote.valid = true;
	remote.area = area;
	remote.pos = pos;
	remote.angle = angle;
	remote.life = life;
	remote.maxLife = std::max(1.f, maxLife);
	remote.mana = mana;
	remote.maxMana = std::max(1.f, maxMana);
	remote.level = level;
	remote.anim0 = anim0;
	remote.anim1 = anim1;
	remote.anim3 = anim3;
	remote.anim3Flags = anim3Flags;
	remote.anim3Time = anim3Time;
	remote.anim0Flags = anim0Flags;
	remote.anim1Flags = anim1Flags;
	remote.anim0Time = anim0Time;
	remote.anim1Time = anim1Time;
	remote.dead = dead;
	remote.combat = combat;
	remote.invisibility = invisibility;
	remote.skin = skin;
	remote.weapon = std::move(weapon);
	remote.helmet = std::move(helmet);
	remote.armour = std::move(armour);
	remote.leggings = std::move(leggings);
	remote.shield = std::move(shield);
	remote.lastUpdate = u32(toMsi(g_gameTime.now()));

	pushAvatarSample(stamp, remote.pos, remote.angle);

	noteRemoteArea(area);
	bool nowPresent = (area == g_currentArea);
	if(nowPresent && !remote.present) {
		g_session.partnerPresentSince = platform::getTime();
	}
	remote.present = nowPresent;

}

/*!
 * Top the local player's vitals up as the session becomes live.
 *
 * The starting save leaves the character nearly dead - the story opens with
 * them beaten up in a cell - and beginning a co-op session at one hit point
 * means standing around waiting for regeneration before anyone can play.
 * A session start is a spawn, and a spawn starts fresh.
 */
static void restoreVitalsForSpawn() {

	if(player.lifePool.max > 0.f) {
		player.lifePool.current = player.lifePool.max;
	}
	if(player.manaPool.max > 0.f) {
		player.manaPool.current = player.manaPool.max;
	}

}

void onHandshakeComplete() {

	g_session.handshaken = true;
	setStatus(Status::Playing, "CO-OP: PLAYING WITH " + avatar().name);

	if(isHost()) {
		/*
		 * The host is ready to play immediately - it is its own story that is
		 * being shared. It does not push that story here: the guest asks for it
		 * as soon as it has processed the welcome, and answering twice would
		 * have the guest apply it twice and be placed beside us twice.
		 */
		g_session.synchronised = true;
	}

	restoreVitalsForSpawn();

	// Magic is shared, so say what we know as soon as there is someone to tell.
	// Both sides do this and both keep the union, so it does not matter who
	// arrived with which runes, or who was first.
	reportRunes();

	reportAreaChange(g_currentArea);

}

static bool g_cutsceneViewer = false;
static PlatformInstant g_transitTraceUntil = 0;

static void applyCutscenePlay(Reader & reader) {

	std::string speakerId = reader.getString();
	std::string data = reader.getString();
	s32 mood = reader.getS32();
	u32 flags = reader.getU32();
	CinematicSpeech cine;
	cine.type = CinematicSpeechMode(reader.getS32());
	cine.startangle = reader.getAnglef();
	cine.endangle = reader.getAnglef();
	cine.startpos = reader.getFloat();
	cine.endpos = reader.getFloat();
	cine.m_startdist = reader.getFloat();
	cine.m_enddist = reader.getFloat();
	cine.m_heightModifier = reader.getFloat();
	std::string otherId = reader.getString();
	cine.pos1 = reader.getVec3f();
	cine.pos2 = reader.getVec3f();

	if(!reader.ok() || !isPlaying()) {
		return;
	}

	Entity * speaker = entities.getById(speakerId);
	if(!speaker) {
		speaker = avatarEntity();
	}
	if(!speaker) {
		return;
	}

	if(Entity * other = entities.getById(otherId)) {
		cine.ionum = other->index();
	} else {
		cine.ionum = EntityHandle_Player;
	}

	Speech * speech = ARX_SPEECH_AddSpeech(*speaker, data,
	                                       long(mood), SpeechFlags::load(flags));
	if(!speech) {
		return;
	}
	speech->cine = cine;

	// The viewer sits in the audience: held for exactly as long as the show.
	if(cine.type != ARX_CINE_SPEECH_NONE) {
		g_cutsceneViewer = true;
		BLOCK_PLAYER_CONTROLS = true;
		cinematicBorder.set(true, true);
	}

}

static void applyNpcTouch(const std::string & id) {

	Entity * npc = entities.getById(id);
	Entity * body = avatarEntity();
	if(!npc || !body || !(npc->ioflags & IO_NPC) || !npc->_npcdata) {
		return;
	}
	if(npc->_npcdata->lifePool.current <= 0.f) {
		return;
	}
	// A touch happens at arm's length, not across the map.
	if(fartherThan(npc->pos, body->pos, 350.f)) {
		return;
	}
	handleNpcCollision(*body, *npc);

}

void handleMessage(const u8 * data, size_t size) {

	if(size == 0) {
		return;
	}

	Reader reader(data + 1, size - 1);
	MessageType type = MessageType(data[0]);

	switch(type) {

		case MsgHello: {
			u32 version = reader.getU32();
			std::string name = reader.getString();
			u8 resuming = reader.getU8();
			if(!reader.ok()) {
				break;
			}
			if(version != ProtocolVersion) {
				Writer reject(MsgReject);
				reject.put(u8(RejectVersion));
				reject.put(std::string_view("The other player is running a different version"));
				send(reject, ChannelControl);
				enet_peer_disconnect_later(g_session.peer, 0);
				setStatus(Status::Failed, "JOIN REFUSED - VERSION MISMATCH");
				break;
			}
			mutableAvatar().name = name.empty() ? "Player 2" : name;
			{
				Writer welcome(MsgWelcome);
				welcome.put(ProtocolVersion);
				welcome.put(std::string_view(g_session.localName));
				welcome.put(u32(g_currentArea.handleData()));
				welcome.put(std::string_view(g_session.playthroughId));
				send(welcome, ChannelControl);
			}
			onHandshakeComplete();
			if(resuming) {
				// They never left the world, only the wire did. Resend everything
				// once and play on; the audit heals whatever drifted meanwhile.
				resetReplication();
				notification_add(avatar().name + " reconnected");
			} else {
				notification_add(avatar().name + " joined the game");
			}
			break;
		}

		case MsgWelcome: {
			u32 version = reader.getU32();
			std::string name = reader.getString();
			u32 area = reader.getU32();
			std::string playthrough = reader.getString();
			if(!reader.ok()) {
				break;
			}
			g_session.playthroughId = playthrough;
			if(version != ProtocolVersion) {
				setStatus(Status::Failed, "JOIN FAILED - VERSION MISMATCH");
				stop();
				break;
			}
			mutableAvatar().name = name.empty() ? "Player 1" : name;
			g_session.remoteArea = AreaId(area);
			if(g_session.resuming) {
				// Already standing in the world: no reload, no teleport. A fresh
				// keyframe plus the audit repairs whatever moved during the gap.
				g_session.resuming = false;
				g_session.handshaken = true;
				g_session.synchronised = true;
				resetReplication();
				setStatus(Status::Playing, "CO-OP: PLAYING WITH " + avatar().name);
				notification_add("Reconnected");
				reportAreaChange(g_currentArea);
				break;
			}
			setStatus(Status::Syncing, "CO-OP: SYNCHRONISING");
			g_session.handshaken = true;
			// Ask for the story state; play does not really start until it lands.
			sendBare(MsgWorldRequest, ChannelControl);
			reportAreaChange(g_currentArea);
			break;
		}

		case MsgReject: {
			u8 reason = reader.getU8();
			std::string text = reader.getString();
			(void) reason;
			setStatus(Status::Failed, text.empty() ? "JOIN REFUSED" : text);
			break;
		}

		case MsgWorldRequest: {
			if(isHost()) {
				if(!g_currentArea || BLOCK_PLAYER_CONTROLS || isInCinematic()
				   || cinematicBorder.isActive()) {
					/*
					 * The world is mid-sequence - a fresh game's intro, a
					 * dream, a scripted scene. Serialising THAT hands the
					 * guest a player locked by timers their machine mutes,
					 * which reads as being trapped in a cutscene forever.
					 * Answer once the sequence lets go; a clean world is
					 * worth a few seconds at the loading screen.
					 */
					LogInfo << "[coop] holding the world sync until this machine is ready";
					if(!g_currentArea) {
						notification_add("The other player is waiting - start or load a game");
					}
					g_session.worldRequestDeferred = true;
				} else {
					sendGlobalState();
				}
			}
			break;
		}

		case MsgWorldBegin: {
			g_session.incomingExpected = reader.getU32();
			g_session.incoming.clear();
			g_session.incoming.reserve(g_session.incomingExpected);
			break;
		}

		case MsgWorldChunk: {
			size_t count = reader.getU16();
			const u8 * chunk = reader.getRaw(count);
			if(chunk) {
				g_session.incoming.insert(g_session.incoming.end(), chunk, chunk + count);
			}
			break;
		}

		case MsgWorldEnd: {
			if(g_session.incoming.size() != g_session.incomingExpected) {
				LogWarning << "[coop] the world arrived truncated, ignoring it";
				g_session.incoming.clear();
				break;
			}
			fs::path savefile = fs::getUserDir() / "coop_join" / "gsave.sav";
			if(!fs::exists(savefile.parent())) {
				fs::create_directories(savefile.parent());
			}
			std::string_view bytes(reinterpret_cast<const char *>(g_session.incoming.data()),
			                       g_session.incoming.size());
			if(!fs::write(savefile, bytes)) {
				LogError << "[coop] could not store the received world";
				g_session.incoming.clear();
				break;
			}
			LogInfo << "[coop] world received (" << g_session.incoming.size()
			        << " bytes); loading it";
			g_session.incoming.clear();
			g_session.synchronised = true;
			g_session.joinNudge = true;
			g_session.freshSpawnVitals = true;
			/*
			 * The save's own idea of "where the player is" lags reality - it
			 * records transitions, not the live present. Never trust it for
			 * placement: after the world loads, walk to the host's LIVE area
			 * (streamed thirty times a second) and land on the host exactly.
			 */
			g_session.travelToHost = true;
			ARX_RequestLoadSaveFile(savefile);
			setStatus(Status::Playing, "CO-OP: PLAYING WITH " + avatar().name);
			break;
		}

		case MsgAvatar: {
			receiveAvatar(reader);
			break;
		}

		case MsgEntities: {
			u32 stamp = reader.getU32();
			u8 beatMs = reader.getU8();
			noteRemoteClock(stamp);
			if(beatMs) {
				g_session.remoteSnapshotIntervalMs = beatMs;
			}
			if(g_session.newestEntitiesStamp && s32(stamp - g_session.newestEntitiesStamp) <= 0) {
				break; // took the long way round the network; the world has moved on
			}
			g_session.newestEntitiesStamp = stamp;
			float lateness = float(estimatedRemoteNowMs() - s64(stamp));
			if(lateness > g_session.entityLatenessMs) {
				g_session.entityLatenessMs = lateness;
			}
			// Only trust entity state from the authority for the area we are in.
			if(isReplica()) {
				ApplyScope scope;
				readEntitySnapshot(reader, stamp);
			}
			break;
		}

		case MsgAction: {
			std::string id = reader.getString();
			if(reader.ok()) {
				applyActionRequest(id);
			}
			break;
		}

		case MsgVoice: {
			u16 length = reader.getU16();
			const u8 * audio = reader.getRaw(length);
			if(reader.ok() && audio) {
				voice::onPacket(audio, length);
			}
			break;
		}

		case MsgLightIgnite: {
			u16 index = reader.getU16();
			bool lit = reader.getBool();
			if(reader.ok()) {
				ApplyScope scope;
				applyLightIgnite(index, lit);
			}
			break;
		}

		case MsgCombine: {
			std::string sourceId = reader.getString();
			std::string sourceClass = reader.getString();
			std::string targetId = reader.getString();
			if(reader.ok()) {
				ApplyScope scope;
				applyCombineRequest(sourceId, sourceClass, targetId);
			}
			break;
		}

		case MsgCombineTaken: {
			std::string sourceId = reader.getString();
			if(reader.ok()) {
				ApplyScope scope;
				applyCombineTaken(sourceId);
			}
			break;
		}

		case MsgGiveItem: {
			std::string classPath = reader.getString();
			s16 count = reader.getS16();
			if(reader.ok()) {
				ApplyScope scope;
				applyGiveItem(classPath, count);
			}
			break;
		}

		case MsgTake: {
			std::string id = reader.getString();
			if(reader.ok()) {
				ApplyScope scope;
				applyTakeRequest(id);
			}
			break;
		}

		case MsgDrop: {
			std::string id = reader.getString();
			std::string classPath = reader.getString();
			s16 count = reader.getS16();
			float durability = reader.getFloat();
			Vec3f at = reader.getVec3f();
			float yaw = reader.getFloat();
			Vec3f velocity = reader.getVec3f();
			if(reader.ok()) {
				ApplyScope scope;
				applyItemDropped(id, classPath, count, durability, at, yaw, velocity);
			}
			break;
		}

		case MsgEntityGone: {
			std::string id = reader.getString();
			if(reader.ok()) {
				ApplyScope scope;
				applyEntityGone(id);
			}
			break;
		}

		case MsgEntitySpawn: {
			std::string classPath = reader.getString();
			s32 instance = reader.getS32();
			Vec3f at = reader.getVec3f();
			float yaw = reader.getFloat();
			s16 count = reader.getS16();
			float durability = reader.getFloat();
			if(reader.ok()) {
				ApplyScope scope;
				applyEntitySpawn(classPath, instance, at, yaw, count, durability);
			}
			break;
		}

		case MsgWorldAudit: {
			if(hasWorldAuthority()) {
				applyWorldAudit(reader);
			}
			break;
		}

		case MsgWorldFx: {
			if(isReplica()) {
				ApplyScope scope;
				applyWorldFx(reader);
			}
			break;
		}

		case MsgPlayerTouchNpc: {
			std::string id = reader.getString();
			if(reader.ok() && !isReplica()) {
				applyNpcTouch(id);
			}
			break;
		}

		case MsgHitEntity: {
			std::string id = reader.getString();
			float damage = reader.getFloat();
			u32 damageType = reader.getU32();
			if(reader.ok()) {
				applyHitRequest(id, damage, damageType);
			}
			break;
		}

		case MsgDamagePlayer: {
			float damage = reader.getFloat();
			u32 damageType = reader.getU32();
			if(reader.ok()) {
				applyRemoteDamage(damage, damageType);
			}
			break;
		}

		case MsgPlayerDied: {
			mutableAvatar().dead = true;
			notification_add(avatar().name + " has fallen");
			break;
		}

		case MsgPlayerRevive: {
			mutableAvatar().dead = false;
			notification_add(avatar().name + " is back on their feet");
			break;
		}

		case MsgSpellCast: {
			int spellType = reader.getS32();
			float level = reader.getFloat();
			u32 flags = reader.getU32();
			s64 duration = reader.getS64();
			std::string target = reader.getString();
			std::string caster = reader.getString();
			if(reader.ok()) {
				ApplyScope scope;
				applyRemoteSpell(spellType, level, flags, duration, target, caster);
			}
			break;
		}

		case MsgSpellEnd: {
			int spellType = reader.getS32();
			if(reader.ok()) {
				ApplyScope scope;
				applyRemoteSpellEnd(spellType);
			}
			break;
		}

		case MsgReward: {
			s32 xp = reader.getS32();
			s32 gold = reader.getS32();
			if(!reader.ok()) {
				break;
			}
			ApplyScope scope;
			if(xp > 0) {
				ARX_PLAYER_Modify_XP(xp);
			}
			if(gold > 0) {
				ARX_PLAYER_AddGold(gold);
			}
			break;
		}

		case MsgQuest: {
			std::string quest = reader.getString();
			if(reader.ok() && !quest.empty()) {
				ApplyScope scope;
				ARX_PLAYER_Quest_Add(quest);
			}
			break;
		}

		case MsgRunes: {
			u32 theirs = reader.getU32();
			if(reader.ok()) {
				ApplyScope scope;
				RuneFlags known = RuneFlags::load(theirs);
				bool learned = false;
				for(size_t i = 0; i < RUNE_COUNT; i++) {
					RuneFlag rune = RuneFlag(1 << i);
					if((known & rune) && !(player.rune_flags & rune)) {
						// One at a time, through the engine's own function, so
						// the book lights up for a spell that has just become
						// castable exactly as it would had we found the rune.
						ARX_Player_Rune_Add(rune);
						learned = true;
					}
				}
				if(learned) {
					LogInfo << "[coop] learned runes from the other player";
				}
			}
			break;
		}

		case MsgKeyring: {
			std::string key = reader.getString();
			if(reader.ok() && !key.empty()) {
				ApplyScope scope;
				ARX_KEYRING_Add(key);
			}
			break;
		}

		case MsgNotify:
		case MsgChat: {
			std::string text = reader.getString();
			if(reader.ok() && !text.empty()) {
				notification_add(avatar().name + ": " + text);
			}
			break;
		}

		case MsgAreaRequest: {
			/*
			 * The other player told us which area they are in; that decides who
			 * simulates what from here on.
			 *
			 * Only recorded here, never acted on. updateAvatar() is the one
			 * place that creates and retires the body, and it reads this every
			 * frame. Tearing it down here as well gave two pieces of code an
			 * opinion about whether the body should exist, and joining made
			 * them disagree for a moment - long enough to destroy a body that
			 * had just been built and build it again.
			 */
			noteRemoteArea(AreaId(reader.getU32()));
			mutableAvatar().area = g_session.remoteArea;
			{
				bool nowPresent = (g_session.remoteArea == g_currentArea);
				if(nowPresent && !mutableAvatar().present) {
					g_session.partnerPresentSince = platform::getTime();
				}
				mutableAvatar().present = nowPresent;
			}

			/*
			 * The two sides load at different speeds, so "we are in the same
			 * area now" can become true a moment after our own load finished
			 * rather than during it. If it does, and our arrival was recent,
			 * the arrival snap above still applies - but only then. A guest
			 * who has been exploring this area for minutes is not yanked to
			 * the door because the host wandered in.
			 */
			if(isGuest() && g_session.remoteArea == g_currentArea
			   && g_session.lastAreaLoad != 0
			   && !g_session.lastArrivalWasDoor
			   && platform::getTime() - g_session.lastAreaLoad < 20s) {
				g_session.joinNudge = true;
			}
			break;
		}

		case MsgCutscenePlay: {
			applyCutscenePlay(reader);
			break;
		}

		case MsgPartnerEffect: {
			u8 kind = reader.getU8();
			float value = reader.getFloat();
			if(!reader.ok() || !isPlaying()) {
				break;
			}
			switch(kind) {
				case PartnerFxHeal: {
					if(!BLOCK_PLAYER_CONTROLS && player.lifePool.current > 0.f) {
						player.lifePool.current = std::min(player.lifePool.current + value,
						                                   player.lifePool.max);
					}
					break;
				}
				case PartnerFxMana: {
					player.manaPool.current = std::min(player.manaPool.current + value,
					                                   player.manaPool.max);
					break;
				}
				case PartnerFxFillHunger: {
					player.hunger = std::min(100.f, std::max(player.hunger, value));
					break;
				}
				default: break;
			}
			break;
		}

		case MsgCutsceneSeen: {
			std::string name = reader.getString();
			if(reader.ok() && !name.empty()) {
				LogInfo << "[coop] story ledger (from partner): '" << name << "'";
				adoptSeenCutscene(std::move(name));
			}
			break;
		}

		case MsgTravelHold: {
			s32 fadeMs = reader.getS32();
			if(!reader.ok() || !isGuest() || !isPlaying()) {
				break;
			}
			// The other machine's script has begun a travel in our name. Run our
			// own curtain and stand still inside it until the order lands.
			fadeSetColor(Color3f(0.f, 0.f, 0.f));
			fadeRequestStart(FadeType_Out, std::chrono::milliseconds(fadeMs));
			engageTravelHold(fadeMs);
			break;
		}

		case MsgTravel: {
			u32 area = reader.getU32();
			std::string target = reader.getString();
			s32 angle = reader.getS32();
			bool confirm = reader.getBool();
			if(!reader.ok() || !isGuest() || !isPlaying()) {
				break;
			}
			/*
			 * Exactly what the door script would have written had it run for
			 * this machine's own player. The engine takes it from here - the
			 * confirmation icon, the quick-transition setting, the load, the
			 * marker placement - all of it the stock path.
			 */
			if(g_teleportToArea) {
				// Our own door already wrote this travel; the host's echo of
				// the same door must not restart or redirect it.
				break;
			}
			g_teleportToArea = AreaId(area);
			TELEPORT_TO_POSITION = target;
			TELEPORT_TO_ANGLE = (angle == -1) ? long(player.angle.getYaw()) : long(angle);
			CHANGE_LEVEL_ICON = confirm ? ConfirmChangeLevel : ChangeLevelNow;
			g_session.lastEnteredZone.clear();
			if(!confirm) {
				// The load begins within a frame or two; stand still until it
				// does. A confirmed travel is a doorway prompt - no fall risk,
				// and the player needs their controls to answer it.
				engageTravelHold(1500);
			}
			LogInfo << "[coop] a door offers travel to area " << area << " target " << target;
			break;
		}

		case MsgZoneEnter: {
			std::string name = reader.getString();
			if(!reader.ok() || isReplica()) {
				break;
			}
			// Their machine says their body crossed into this zone THIS frame.
			// Fire it now, in their name, rather than waiting to notice their
			// lag-smoothed body arrive.
			Entity * body = avatarEntity();
			Zone * zone = getZoneByName(name);
			if(body && zone) {
				ARX_PATH_EntityEnterZone(body, zone);
			}
			break;
		}

		case MsgZoneLeave: {
			std::string name = reader.getString();
			if(!reader.ok() || isReplica()) {
				break;
			}
			Entity * body = avatarEntity();
			Zone * zone = getZoneByName(name);
			if(body && zone) {
				ARX_PATH_EntityLeaveZone(body, zone);
			}
			break;
		}

		case MsgTravelCancel: {
			if(!isGuest()) {
				break;
			}
			releaseTravelHold(true);
			// Only while it is still an offer; a transition already running is
			// not interrupted.
			if(CHANGE_LEVEL_ICON == ConfirmChangeLevel) {
				CHANGE_LEVEL_ICON = NoChangeLevel;
				g_teleportToArea = { };
				TELEPORT_TO_POSITION.clear();
			}
			break;
		}

		case MsgPing: {
			sendBare(MsgPong, ChannelControl);
			break;
		}

		case MsgBye: {
			notification_add(avatar().name + " left the game");
			break;
		}

		default: break;

	}

}

void optionHost(u32 port) {
	g_pendingHost = true;
	g_pendingPort = (port > 0 && port < 65536) ? static_cast<unsigned short>(port) : DefaultPort;
}

void optionJoin(const std::string & address) {
	g_pendingJoin = address;
}

void optionName(const std::string & name) {
	g_pendingName = name;
}

} // anonymous namespace

ARX_PROGRAM_OPTION_ARG("host", "", "Host a co-op game on the given port", &optionHost, "PORT")
ARX_PROGRAM_OPTION_ARG("join", "", "Join a co-op game at the given address", &optionJoin, "ADDRESS")
ARX_PROGRAM_OPTION_ARG("coop-name", "", "Name shown to the other player", &optionName, "NAME")

// ---------------------------------------------------------------------------

bool startHost(unsigned short port) {

	stop();

	if(!ensureEnet()) {
		setStatus(Status::Failed, "HOST FAILED - NO NETWORK");
		return false;
	}

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;

	g_session.host = enet_host_create(&address, 1, ChannelCount, 0, 0);
	if(!g_session.host) {
		setStatus(Status::Failed, "HOST FAILED - PORT IN USE");
		return false;
	}

	/*
	 * Every packet shrinks through ENet's built-in range coder and carries a
	 * CRC32. Both machines run this same build, so both ends always agree on
	 * the format, and a corrupted packet is dropped instead of applied.
	 */
	enet_host_compress_with_range_coder(g_session.host);
	g_session.host->checksum = enet_crc32;

	initNetSimulator();
	startRecorder("host");
	loadStoryLedger();
	g_session.listenPort = port;

	/*
	 * Ask the router to forward the port. This returns at once and is answered
	 * later - hosting works regardless, this only saves the host from setting
	 * up port forwarding by hand.
	 */
	portmap::open(port);

	g_session.role = Role::Host;
	g_session.localName = "Player 1";
	mutableAvatar().name = "Player 2";

	char text[64];
	std::snprintf(text, sizeof(text), "CO-OP: WAITING ON PORT %u", unsigned(port));
	setStatus(Status::Listening, text);

	return true;
}

bool startClient(const std::string & address) {

	stop();

	if(!ensureEnet()) {
		setStatus(Status::Failed, "JOIN FAILED - NO NETWORK");
		return false;
	}

	std::string hostPart = address;
	unsigned short port = DefaultPort;

	// Accept "1.2.3.4" and "1.2.3.4:27100" alike. Anything after the last colon
	// that parses as a number is the port; a bare address keeps the default.
	size_t colon = address.rfind(':');
	if(colon != std::string::npos && colon + 1 < address.size()) {
		char * end = nullptr;
		long parsed = std::strtol(address.c_str() + colon + 1, &end, 10);
		if(end && *end == '\0' && parsed > 0 && parsed < 65536) {
			hostPart = address.substr(0, colon);
			port = static_cast<unsigned short>(parsed);
		}
	}

	// Trim whitespace the player may have typed around the address.
	while(!hostPart.empty() && std::isspace(static_cast<unsigned char>(hostPart.front()))) {
		hostPart.erase(hostPart.begin());
	}
	while(!hostPart.empty() && std::isspace(static_cast<unsigned char>(hostPart.back()))) {
		hostPart.pop_back();
	}

	if(hostPart.empty()) {
		setStatus(Status::Failed, "JOIN FAILED - NO ADDRESS");
		return false;
	}

	g_session.host = enet_host_create(nullptr, 1, ChannelCount, 0, 0);
	if(!g_session.host) {
		setStatus(Status::Failed, "JOIN FAILED - NO SOCKET");
		return false;
	}

	/*
	 * Every packet shrinks through ENet's built-in range coder and carries a
	 * CRC32. Both machines run this same build, so both ends always agree on
	 * the format, and a corrupted packet is dropped instead of applied.
	 */
	enet_host_compress_with_range_coder(g_session.host);
	g_session.host->checksum = enet_crc32;

	ENetAddress target;
	if(enet_address_set_host(&target, hostPart.c_str()) != 0) {
		enet_host_destroy(g_session.host);
		g_session.host = nullptr;
		setStatus(Status::Failed, "JOIN FAILED - UNKNOWN ADDRESS");
		return false;
	}
	target.port = port;
	g_session.reconnectTarget = target;

	g_session.peer = enet_host_connect(g_session.host, &target, ChannelCount, 0);
	if(!g_session.peer) {
		enet_host_destroy(g_session.host);
		g_session.host = nullptr;
		setStatus(Status::Failed, "JOIN FAILED - NO SOCKET");
		return false;
	}

	// Same patience as the host grants us: survive each other's level loads.
	enet_peer_timeout(g_session.peer, 1024, 60000, 180000);

	initNetSimulator();
	startRecorder("guest");

	g_session.role = Role::Guest;
	g_session.localName = "Player 2";
	mutableAvatar().name = "Player 1";
	g_session.connectStart = platform::getTime();

	setStatus(Status::Connecting, "CO-OP: CONNECTING");

	return true;
}

void stop() {

	saveGuestProfileIfDue(true);
	releaseTravelHold(false);

	// Hand the port back rather than leaving it forwarded after the game ends.
	portmap::close();

	if(g_session.peer) {
		sendBare(MsgBye, ChannelControl);
		if(g_session.host) {
			enet_host_flush(g_session.host);
		}
		enet_peer_disconnect_now(g_session.peer, 0);
		g_session.peer = nullptr;
	}

	if(g_session.host) {
		enet_host_destroy(g_session.host);
		g_session.host = nullptr;
	}

	destroyAvatarEntity();
	resetAvatar();

	g_session.role = Role::Offline;
	g_session.handshaken = false;
	g_session.synchronised = false;
	g_session.joinNudge = false;
	g_session.remoteArea = AreaId();
	g_session.clockOffsetMs = 0;
	g_session.clockValid = false;
	g_session.newestEntitiesStamp = 0;
	g_session.newestAvatarStamp = 0;
	g_session.entityLatenessMs = 0.f;
	g_session.bodyLatenessMs = 0.f;
	g_session.lastLatenessDecay = 0;
	g_session.snapshotTick = 0;
	g_session.wasSharing = false;
	g_session.lastAudit = 0;
	g_session.resuming = false;
	g_session.partnerHealAccum = 0.f;
	g_session.freshSpawnVitals = false;
	g_session.worldRequestDeferred = false;
	g_session.lastEnteredZone.clear();
	g_session.remoteSnapshotIntervalMs = 50;
	g_session.lastKeyframe = 0;
	g_delayedPackets.clear();
	if(g_recordFile) {
		std::fclose(g_recordFile);
		g_recordFile = nullptr;
	}
	g_session.incoming.clear();
	g_session.incomingExpected = 0;
	g_session.status = Status::Offline;
	g_session.statusText = "OFFLINE";

}

void poll() {

	// Collect anything the router has said back about the port. Costs nothing
	// when no mapping was ever asked for.
	portmap::update();

	// Send what was said and play what was heard.
	voice::update();

	// And which fires are burning.
	pollStaticLights();

	// Honour --host / --join now that there is a game for them to attach to.
	if((g_pendingHost || !g_pendingJoin.empty()) && !isActive()) {
		bool wantHost = g_pendingHost;
		std::string address = g_pendingJoin;
		g_pendingHost = false;
		g_pendingJoin.clear();
		if(wantHost) {
			startHost(g_pendingPort);
		} else {
			startClient(address);
		}
		if(!g_pendingName.empty()) {
			g_session.localName = g_pendingName;
		}
	}

	if(!g_session.host) {
		return;
	}

	if(g_session.status == Status::Connecting
	   && platform::getTime() - g_session.connectStart > ConnectTimeout) {
		LogWarning << "[coop] no answer from the host";
		stop();
		setStatus(Status::Failed, "JOIN FAILED - NO ANSWER");
		return;
	}

	/*
	 * A guest that has just joined is standing in its own game, which may be
	 * anywhere. Walk it to the host once, using the engine's own level change
	 * so that everything a normal transition does still happens. After this the
	 * two are free to separate again; the flag only ever fires on arrival.
	 */
	if(g_session.travelToHost) {
		static PlatformInstant lastGateLog = 0;
		PlatformInstant gateNow = platform::getTime();
		if(gateNow - lastGateLog >= 2000ms) {
			lastGateLog = gateNow;
			LogWarning << "[coop-place] travel gate: playing=" << isPlaying()
			           << " ingame=" << (ARXmenu.mode() == Mode_InGame)
			           << " area=" << (g_currentArea ? long(g_currentArea.handleData()) : -1)
			           << " remote=" << (g_session.remoteArea ? long(g_session.remoteArea.handleData()) : -1)
			           << " teleportPending=" << (g_teleportToArea ? 1 : 0);
		}
	}

	/*
	 * Standing nowhere is a reason to travel, not a reason to wait.
	 *
	 * This used to insist the guest already be in an area of its own before it
	 * would walk to the host. Usually it is - but if the join finishes before
	 * the guest's own level has loaded, it never will be, and then the one
	 * condition being waited for is the one that can no longer happen. The
	 * result was a black screen: connected, in game, no level, no way out of
	 * it but to close the game and join again.
	 *
	 * Knowing where the host is, is enough. The comparison below still skips
	 * the trip when both are already in the same place, and an area that does
	 * not exist never compares equal to one that does.
	 */
	if(g_session.travelToHost && isPlaying() && ARXmenu.mode() == Mode_InGame
	   && g_session.remoteArea && !g_teleportToArea) {
		g_session.travelToHost = false;
		if(g_session.remoteArea != g_currentArea) {
			LogInfo << "[coop] travelling to the host's area " << g_session.remoteArea;
			g_teleportToArea = g_session.remoteArea;
			TELEPORT_TO_POSITION.clear();
			TELEPORT_TO_ANGLE = 0;
			CHANGE_LEVEL_ICON = ChangeLevelNow;
		}
	}

	if(debugTrace() && entities.player()) {
		static bool pBlock = false, pBorder = false, pReplica = false, pCine = false;
		bool block = BLOCK_PLAYER_CONTROLS;
		bool border = cinematicBorder.isActive();
		bool replica = isReplica();
		bool cine = isInCinematic();
		if(block != pBlock || border != pBorder || replica != pReplica || cine != pCine) {
			LogWarning << "[coop-debug] state: controls_blocked=" << block
			           << " cinema_border=" << border << " replica=" << replica
			           << " cinematic=" << cine << " travelHold=" << g_session.travelHold
			           << " area=" << (g_currentArea ? long(g_currentArea.handleData()) : -1);
			pBlock = block; pBorder = border; pReplica = replica; pCine = cine;
		}
	}

	updateCutsceneViewer();

	if(g_transitTraceUntil != PlatformInstant(0) && entities.player()) {
		PlatformInstant tnow = platform::getTime();
		static PlatformInstant lastTransit = 0;
		if(tnow > g_transitTraceUntil) {
			g_transitTraceUntil = 0;
		} else if(tnow - lastTransit >= 250ms) {
			lastTransit = tnow;
			LogWarning << "[coop-transit] pos=" << player.pos.x << ',' << player.pos.y
			           << ',' << player.pos.z << " falling=" << player.falling
			           << " onfirmground=" << player.onfirmground
			           << " block=" << BLOCK_PLAYER_CONTROLS;
		}
	}

	if(g_session.travelHold && platform::getTime() > g_session.travelHoldDeadline) {
		// The promised travel never came; wake up and resume the fall unharmed.
		LogWarning << "[coop] travel hold timed out; releasing";
		releaseTravelHold(true);
	}

	/*
	 * Panic rescue: hold H for three seconds to stand beside the other player.
	 * The permanent way out of any stuck geometry, wedged physics or bug -
	 * a modded game owes its players an escape hatch that always works.
	 */
	bool rescueHeld = isPlaying() && ARXmenu.mode() == Mode_InGame
	                  && !BLOCK_PLAYER_CONTROLS && avatar().valid
	                  && GInput && GInput->isKeyPressed(Keyboard::Key_H);
	if(!rescueHeld) {
		g_session.rescueHoldStart = 0;
	} else if(g_session.rescueHoldStart == PlatformInstant(0)) {
		g_session.rescueHoldStart = platform::getTime();
	} else if(platform::getTime() - g_session.rescueHoldStart > 3000ms) {
		g_session.rescueHoldStart = 0;
		Entity * body = avatarEntity();
		if(avatar().present && body && entities.player()) {
			// Beside them, settled onto valid ground by the engine's own rule.
			player.pos = body->pos + Vec3f(40.f, 0.f, 40.f);
			player.pos.y += player.baseHeight();
			player.physics.cyl.origin = player.basePosition();
			IO_PHYSICS phys = player.physics;
			AttemptValidCylinderPos(phys.cyl, entities.player(), CFLAG_RETURN_HEIGHT);
			player.pos.y = phys.cyl.origin.y + player.baseHeight();
			player.falling = false;
			entities.player()->requestRoomUpdate = true;
			notification_add("Rescued to " + avatar().name);
		} else if(isGuest()) {
			// They are in another area: use the same walk-to-them travel that
			// joining uses.
			g_session.travelToHost = true;
			notification_add("Travelling to " + avatar().name);
		} else {
			notification_add(avatar().name + " is in another area");
		}
	}

	if(g_session.resuming && !g_session.peer && g_session.host) {
		PlatformInstant rnow = platform::getTime();
		if(rnow > g_session.reconnectDeadline) {
			g_session.resuming = false;
			setStatus(Status::Failed, "CO-OP: DISCONNECTED");
			notification_add("Could not reconnect");
		} else if(rnow >= g_session.nextReconnectAt) {
			g_session.nextReconnectAt = rnow + 5000ms;
			g_session.connectStart = rnow;
			g_session.peer = enet_host_connect(g_session.host, &g_session.reconnectTarget,
			                                   ChannelCount, 0);
			if(g_session.peer) {
				enet_peer_timeout(g_session.peer, 1024, 60000, 180000);
			}
		}
	}

	pumpDelayedPackets();

	ENetEvent event;
	while(g_session.host && enet_host_service(g_session.host, &event, 0) > 0) {

		switch(event.type) {

			case ENET_EVENT_TYPE_CONNECT: {
				if(isHost()) {
					if(g_session.peer && g_session.peer != event.peer) {
						// One guest at a time. Turn the extra one away politely
						// rather than letting it half-join and confuse the world.
						ENetPacket * packet = enet_packet_create(nullptr, 0, ENET_PACKET_FLAG_RELIABLE);
						if(packet) {
							enet_packet_destroy(packet);
						}
						enet_peer_disconnect(event.peer, RejectFull);
						break;
					}
						g_session.peer = event.peer;
					/*
					 * A level load blocks the game for ten seconds or more,
					 * and a blocked game cannot answer the network. With
					 * ENet's default patience that reads as a dead peer, so
					 * every area change rolled the dice on a silent
					 * disconnect and a messy mid-play rejoin - the log showed
					 * the guest quietly rejoining as "Player 2" after
					 * following the host through a door. Give the connection
					 * minutes, not seconds.
					 */
					enet_peer_timeout(event.peer, 1024, 60000, 180000);
					setStatus(Status::Syncing, "CO-OP: SYNCHRONISING");
				} else {
					// Our connection request was accepted; introduce ourselves.
					Writer hello(MsgHello);
					hello.put(ProtocolVersion);
					hello.put(std::string_view(g_session.localName));
					hello.put(u8(g_session.resuming ? 1 : 0));
					send(hello, ChannelControl);
				}
				break;
			}

			case ENET_EVENT_TYPE_RECEIVE: {
				recordPacket(u8(event.channelID), event.packet->data, event.packet->dataLength);
				handleMessage(event.packet->data, event.packet->dataLength);
				enet_packet_destroy(event.packet);
				break;
			}

			case ENET_EVENT_TYPE_DISCONNECT: {
				if(event.peer == g_session.peer) {
					bool wasPlaying = g_session.handshaken;
					g_session.peer = nullptr;
					g_session.handshaken = false;
					g_session.synchronised = false;
					destroyAvatarEntity();
					mutableAvatar().valid = false;
					mutableAvatar().present = false;
					if(isHost()) {
						// Keep listening: the other player can come back.
						setStatus(Status::Listening, "CO-OP: WAITING FOR A PLAYER");
						notification_add("The other player left");
					} else if(wasPlaying || g_session.resuming) {
						// The wire broke mid-game. The world is still here on both
						// machines - quietly reconnect and pick up where we were.
						PlatformInstant dropNow = platform::getTime();
						if(!g_session.resuming) {
							g_session.resuming = true;
							g_session.reconnectDeadline = dropNow + 120000ms;
							notification_add("Connection lost - reconnecting");
						}
						g_session.nextReconnectAt = dropNow + 2000ms;
						g_session.connectStart = dropNow;
						setStatus(Status::Connecting, "CO-OP: RECONNECTING");
					} else {
						setStatus(Status::Failed, "CO-OP: DISCONNECTED");
					}
				}
				break;
			}

			default: break;

		}

	}

}

void sendEntityGone(std::string_view id) {
	
	if(!isPlaying()) {
		return;
	}
	
	Writer writer(MsgEntityGone);
	writer.put(id);
	send(writer, ChannelEvent);
	
}

//! Whether this machine's simulation is the one whose effects are worth telling.
static bool shouldBroadcastFx() {
	return isPlaying() && hasWorldAuthority() && sharingArea() && !isApplyingRemote();
}

void broadcastCollisionMats(u8 mat1, u8 mat2, float volume, float power, const Vec3f & pos) {
	
	if(!shouldBroadcastFx()) {
		return;
	}
	
	Writer writer(MsgWorldFx);
	writer.put(u8(0)); // FxCollisionMats
	writer.put(mat1);
	writer.put(mat2);
	writer.put(volume);
	writer.put(power);
	writer.put(pos);
	send(writer, ChannelSnapshot);
	
}

void broadcastCollisionNames(std::string_view name1, std::string_view name2, float volume,
                             float power, const Vec3f & pos) {
	
	if(!shouldBroadcastFx()) {
		return;
	}
	
	Writer writer(MsgWorldFx);
	writer.put(u8(1)); // FxCollisionNames
	writer.put(name1);
	writer.put(name2);
	writer.put(volume);
	writer.put(power);
	writer.put(pos);
	send(writer, ChannelSnapshot);
	
}

void broadcastBlood(const Vec3f & pos, float dmgs, std::string_view sourceId) {
	
	if(!shouldBroadcastFx()) {
		return;
	}
	
	Writer writer(MsgWorldFx);
	writer.put(u8(2)); // FxBlood
	writer.put(pos);
	writer.put(dmgs);
	writer.put(sourceId);
	send(writer, ChannelSnapshot);
	
}

void broadcastBlood2(const Vec3f & pos, float dmgs, u32 color, std::string_view targetId) {
	
	if(!shouldBroadcastFx()) {
		return;
	}
	
	Writer writer(MsgWorldFx);
	writer.put(u8(3)); // FxBlood2
	writer.put(pos);
	writer.put(dmgs);
	writer.put(color);
	writer.put(targetId);
	send(writer, ChannelSnapshot);
	
}

void reportCutscenePlay(const std::string & speakerId, const std::string & data,
                        long mood, u32 flags, const CinematicSpeech & cine) {

	if(!isPlaying() || isApplyingRemote()) {
		return;
	}

	Writer writer(MsgCutscenePlay);
	writer.put(std::string_view(speakerId));
	writer.put(std::string_view(data));
	writer.put(s32(mood));
	writer.put(flags);
	writer.put(s32(cine.type));
	writer.put(cine.startangle);
	writer.put(cine.endangle);
	writer.put(cine.startpos);
	writer.put(cine.endpos);
	writer.put(cine.m_startdist);
	writer.put(cine.m_enddist);
	writer.put(cine.m_heightModifier);
	Entity * other = entities.get(cine.ionum);
	writer.put(std::string_view(other ? other->idString() : std::string()));
	writer.put(cine.pos1);
	writer.put(cine.pos2);
	send(writer, ChannelEvent);

}

void updateCutsceneViewer() {

	if(g_cutsceneViewer && !ARX_SPEECH_IsAnyCinematicActive()) {
		g_cutsceneViewer = false;
		if(player.lifePool.current > 0.f && !g_session.travelHold) {
			BLOCK_PLAYER_CONTROLS = false;
		}
		cinematicBorder.reset();
	}

	/*
	 * A way out of a cutscene that never happens.
	 *
	 * Story moments hold the player still and then play a line; the line
	 * ending is what gives control back. In co-op a line can fail to play at
	 * all - already lived through by the other player, skipped because this
	 * machine is not the stage, or lost with the script that would have
	 * resumed it - and then the hold is never lifted. Both players stand under
	 * the black bars waiting for something that is not coming, and the only
	 * way out is to close the game.
	 *
	 * So: locked, with no speech, no cinematic, alive, not travelling, for
	 * three continuous seconds - that is not a cutscene, that is a hang. Three
	 * seconds is longer than any gap between lines in the game and short
	 * enough that a player has not yet decided the mod is broken.
	 */
	static PlatformInstant lockedSince = 0;

	bool locked = BLOCK_PLAYER_CONTROLS || cinematicBorder.isActive();
	bool waitingOnSomething = ARX_SPEECH_IsAnyCinematicActive()
	                          || ARX_SPEECH_IsAnySpeechActive()
	                          || isInCinematic()
	                          || g_session.travelHold
	                          || g_cutsceneViewer
	                          || player.lifePool.current <= 0.f;

	if(!isPlaying() || !locked || waitingOnSomething) {
		lockedSince = 0;
		return;
	}

	PlatformInstant now = platform::getTime();
	if(lockedSince == PlatformInstant(0)) {
		lockedSince = now;
		return;
	}

	if(now - lockedSince > 3000ms) {
		lockedSince = 0;
		LogWarning << "[coop] a cutscene locked the player and never finished; releasing";
		BLOCK_PLAYER_CONTROLS = false;
		cinematicBorder.reset();
	}

}

void reportNpcTouch(Entity & npc) {

	if(!isPlaying() || !isReplica() || isApplyingRemote()) {
		return;
	}

	Writer writer(MsgPlayerTouchNpc);
	writer.put(std::string_view(npc.idString()));
	send(writer, ChannelEvent);

}

void reportPartnerEffect(u8 kind, float value) {
	
	if(!isPlaying()) {
		return;
	}
	
	Writer writer(MsgPartnerEffect);
	writer.put(kind);
	writer.put(value);
	send(writer, ChannelEvent);
	
}

void reportPartnerHeal(float amount) {
	
	if(!isPlaying() || amount <= 0.f) {
		return;
	}
	
	if(g_session.partnerHealAccum == 0.f) {
		g_session.partnerHealSince = platform::getTime();
	}
	g_session.partnerHealAccum += amount;
	
}


void noteRemoteClock(u32 stampMs) {
	
	s64 sample = s64(stampMs) - toMsi(platform::getTime());
	
	if(!g_session.clockValid || sample > g_session.clockOffsetMs) {
		// The quickest packet ever seen is the closest look at their clock:
		// network delay only ever makes the sample read lower, never higher.
		g_session.clockOffsetMs = sample;
		g_session.clockValid = true;
	} else {
		// Ease back down over time, in case the quickest packet was a fluke or
		// their clock simply ticks a hair slower than ours.
		g_session.clockOffsetMs += (sample - g_session.clockOffsetMs) / 64;
	}
	
}

s64 estimatedRemoteNowMs() {
	return g_session.clockValid ? toMsi(platform::getTime()) + g_session.clockOffsetMs : 0;
}

/*!
 * How far in the past to draw a stream: measured, not guessed.
 *
 * The floor is two send beats - there must normally be a report on either
 * side of the drawn moment - plus the worst delivery lateness seen recently.
 * A jittery connection automatically buys itself more safety; a clean LAN
 * automatically runs closer to the present. The lateness figure decays in
 * flush(), so one bad minute does not pad the delay forever.
 */
s64 entityInterpDelayMs() {
	// The floor rides the authority's CURRENT beat: when a fight speeds the
	// snapshots up, the delay tightens with them.
	float beat = float(g_session.remoteSnapshotIntervalMs ? g_session.remoteSnapshotIntervalMs : 50);
	float delay = 2.f * beat + 25.f + g_session.entityLatenessMs;
	if(delay < 60.f) {
		delay = 60.f;
	} else if(delay > 450.f) {
		delay = 450.f;
	}
	return s64(delay);
}

bool debugTrace() {
	static int enabled = -1;
	if(enabled < 0) {
		const char * env = std::getenv("ARX_COOP_DEBUG");
		enabled = (env && *env && *env != '0') ? 1 : 0;
		if(enabled == 1) {
			LogWarning << "[coop-debug] trace enabled";
		}
	}
	return enabled == 1;
}

//! Story sequences consumed this playthrough, by localisation key.
static std::set<std::string, std::less<>> g_seenCutscenes;
static bool g_ledgerLoaded = false;

/*
 * Where co-op keeps the things a savegame does not hold.
 *
 * Two of them: which story sequences have already been lived through, and the
 * id of this playthrough. Both used to be opened by bare filename, which means
 * the folder the game happened to be started from - so launching it from
 * somewhere else quietly gave you a different memory. They live beside the
 * saves now, and are copied into a save when one is written; see
 * saveSideState().
 */
static fs::path storyLedgerFile() {
	return fs::getUserDir() / "coop-story.txt";
}

static fs::path playthroughIdFile() {
	return fs::getUserDir() / "coop-guid.txt";
}

static void persistStoryLedger() {
	if(!isHost()) {
		return;
	}
	if(std::FILE * f = std::fopen(storyLedgerFile().string().c_str(), "wb")) {
		for(const std::string & name : g_seenCutscenes) {
			std::fprintf(f, "%s\n", name.c_str());
		}
		std::fclose(f);
	}
}

static void loadOrMintPlaythroughId() {

	g_session.playthroughId.clear();
	if(std::FILE * f = std::fopen(playthroughIdFile().string().c_str(), "rb")) {
		char line[64];
		if(std::fgets(line, sizeof(line), f)) {
			std::string id(line);
			while(!id.empty() && (id.back() == '\n' || id.back() == '\r')) {
				id.pop_back();
			}
			g_session.playthroughId = id;
		}
		std::fclose(f);
	}

	if(g_session.playthroughId.empty()) {
		char buffer[48];
		std::snprintf(buffer, sizeof(buffer), "%08x%08x%08x",
		              unsigned(Random::get(0, 0x7fffffff)),
		              unsigned(Random::get(0, 0x7fffffff)),
		              unsigned(Random::get(0, 0x7fffffff)));
		g_session.playthroughId = buffer;
		if(std::FILE * f = std::fopen(playthroughIdFile().string().c_str(), "wb")) {
			std::fprintf(f, "%s\n", g_session.playthroughId.c_str());
			std::fclose(f);
		}
		LogInfo << "[coop] minted playthrough id " << g_session.playthroughId;
	}

}

const std::string & playthroughId() {
	return g_session.playthroughId;
}

constexpr enet_uint16 DiscoveryPort = 27016;

static ENetSocket g_beaconSocket = ENET_SOCKET_NULL;
static ENetSocket g_discoverySocket = ENET_SOCKET_NULL;

struct DiscoveredEntry {
	DiscoveredHost host;
	PlatformInstant lastSeen = 0;
};
static std::vector<DiscoveredEntry> g_discovered;

static void sendDiscoveryBeacon() {

	if(g_beaconSocket == ENET_SOCKET_NULL) {
		g_beaconSocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
		if(g_beaconSocket == ENET_SOCKET_NULL) {
			return;
		}
		enet_socket_set_option(g_beaconSocket, ENET_SOCKOPT_BROADCAST, 1);
		enet_socket_set_option(g_beaconSocket, ENET_SOCKOPT_NONBLOCK, 1);
	}

	char payload[160];
	int len = std::snprintf(payload, sizeof(payload), "ARXCOOP1\n%s\n%u\n%ld\n",
	                        g_session.localName.c_str(), unsigned(g_session.listenPort),
	                        g_currentArea ? long(g_currentArea.handleData()) : 0L);
	if(len <= 0) {
		return;
	}

	ENetAddress destination;
	destination.host = ENET_HOST_BROADCAST;
	destination.port = DiscoveryPort;

	ENetBuffer buffer;
	buffer.data = payload;
	buffer.dataLength = size_t(len);
	enet_socket_send(g_beaconSocket, &destination, &buffer, 1);

}

std::vector<DiscoveredHost> discoveredHosts() {

	if(g_discoverySocket == ENET_SOCKET_NULL) {
		g_discoverySocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
		if(g_discoverySocket != ENET_SOCKET_NULL) {
			enet_socket_set_option(g_discoverySocket, ENET_SOCKOPT_REUSEADDR, 1);
			enet_socket_set_option(g_discoverySocket, ENET_SOCKOPT_NONBLOCK, 1);
			ENetAddress local;
			local.host = ENET_HOST_ANY;
			local.port = DiscoveryPort;
			if(enet_socket_bind(g_discoverySocket, &local) != 0) {
				enet_socket_destroy(g_discoverySocket);
				g_discoverySocket = ENET_SOCKET_NULL;
			}
		}
	}

	PlatformInstant now = platform::getTime();

	if(g_discoverySocket != ENET_SOCKET_NULL) {
		for(int i = 0; i < 8; i++) {
			char data[256];
			ENetAddress from;
			ENetBuffer buffer;
			buffer.data = data;
			buffer.dataLength = sizeof(data) - 1;
			int received = enet_socket_receive(g_discoverySocket, &from, &buffer, 1);
			if(received <= 0) {
				break;
			}
			data[received] = '\0';
			std::string text(data);
			if(text.compare(0, 9, "ARXCOOP1\n") != 0) {
				continue;
			}
			size_t a = 9;
			size_t b = text.find('\n', a);
			if(b == std::string::npos) {
				continue;
			}
			std::string name = text.substr(a, b - a);
			size_t c = text.find('\n', b + 1);
			if(c == std::string::npos) {
				continue;
			}
			unsigned port = unsigned(std::atoi(text.substr(b + 1, c - b - 1).c_str()));
			int level = std::atoi(text.substr(c + 1).c_str());
			if(!port) {
				continue;
			}
			char ip[64];
			if(enet_address_get_host_ip(&from, ip, sizeof(ip)) != 0) {
				continue;
			}
			char addr[96];
			std::snprintf(addr, sizeof(addr), "%s:%u", ip, port);
			bool known = false;
			for(DiscoveredEntry & entry : g_discovered) {
				if(entry.host.address == addr) {
					entry.host.name = name;
					entry.host.level = level;
					entry.lastSeen = now;
					known = true;
					break;
				}
			}
			if(!known) {
				DiscoveredEntry entry;
				entry.host.name = name.empty() ? "Player" : name;
				entry.host.address = addr;
				entry.host.level = level;
				entry.lastSeen = now;
				g_discovered.push_back(entry);
			}
		}
	}

	std::vector<DiscoveredHost> result;
	for(size_t i = 0; i < g_discovered.size(); ) {
		if(now - g_discovered[i].lastSeen > 5000ms) {
			g_discovered.erase(g_discovered.begin() + long(i));
		} else {
			result.push_back(g_discovered[i].host);
			i++;
		}
	}
	return result;

}

/*
 * Co-op's own memory, kept with the save it belongs to.
 *
 * A savegame restores the world; it knows nothing about which story sequences
 * the two players have watched, or what the guest is carrying, because those
 * live in files of their own. Loading an older save therefore rolled the world
 * back and left co-op's memory where it was - so a conversation already had
 * stayed had, and the NPC simply would not speak. The camera would start the
 * cutscene and then stick, waiting for speech that had been skipped.
 *
 * Saves are folders, so the answer is to put a copy inside one when it is
 * written and take it back out when it is loaded.
 */
void saveSideState(const fs::path & saveFolder) {

	if(saveFolder.empty()) {
		return;
	}

	persistStoryLedger();
	saveGuestProfileIfDue(true);

	fs::copy_file(storyLedgerFile(), saveFolder / "coop-story.txt", true);
	fs::copy_file(playthroughIdFile(), saveFolder / "coop-guid.txt", true);
	fs::copy_file(guestProfileFile(), saveFolder / "coop-profile.bin", true);

}

void loadSideState(const fs::path & saveFolder) {

	if(saveFolder.empty()) {
		return;
	}

	/*
	 * A save written before any of this existed carries none of these files.
	 * Clearing rather than keeping is deliberate: an old save is from before
	 * those conversations happened, so remembering them is precisely the bug.
	 * Better to offer a sequence twice than to lock a quest that cannot be
	 * finished.
	 */
	if(fs::exists(saveFolder / "coop-story.txt")) {
		fs::copy_file(saveFolder / "coop-story.txt", storyLedgerFile(), true);
	} else {
		fs::remove(storyLedgerFile());
		LogInfo << "[coop] this save predates the story ledger; starting it empty";
	}

	if(fs::exists(saveFolder / "coop-guid.txt")) {
		fs::copy_file(saveFolder / "coop-guid.txt", playthroughIdFile(), true);
	}

	/*
	 * The guest's belongings are only ever restored, never cleared.
	 *
	 * A save written before this existed carries no copy of them, and the
	 * tempting reading - "then they had nothing yet" - is wrong in the way that
	 * matters: it throws away everything the second player owns, permanently,
	 * every time anyone loads an older save. Keeping items they might have
	 * picked up slightly later is a blemish. Deleting a player's inventory is
	 * not recoverable, and no amount of correctness is worth that.
	 */
	if(fs::exists(saveFolder / "coop-profile.bin")) {
		fs::copy_file(saveFolder / "coop-profile.bin", guestProfileFile(), true);
	}

	// Read again on the next session rather than keeping what is in memory.
	g_ledgerLoaded = false;
	g_seenCutscenes.clear();

}

void loadStoryLedger() {
	if(g_ledgerLoaded) {
		return;
	}
	g_ledgerLoaded = true;
	loadOrMintPlaythroughId();
	g_seenCutscenes.clear();
	if(std::FILE * f = std::fopen(storyLedgerFile().string().c_str(), "rb")) {
		char line[256];
		while(std::fgets(line, sizeof(line), f)) {
			std::string name(line);
			while(!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
				name.pop_back();
			}
			if(!name.empty()) {
				g_seenCutscenes.insert(std::move(name));
			}
		}
		std::fclose(f);
		LogInfo << "[coop] story ledger: " << g_seenCutscenes.size() << " sequences already lived";
	}
}

bool isCutsceneSeen(std::string_view name) {
	return isPlaying() && g_seenCutscenes.find(name) != g_seenCutscenes.end();
}

void reportCutsceneSeen(std::string_view name) {
	
	if(!isActive() || name.empty()) {
		return;
	}
	
	if(!g_seenCutscenes.insert(std::string(name)).second) {
		return; // already ledgered
	}
	
	LogInfo << "[coop] story ledger: '" << name << "' is now lived for both players";
	persistStoryLedger();
	
	Writer writer(MsgCutsceneSeen);
	writer.put(name);
	send(writer, ChannelEvent);
	
}

void clearStoryLedger() {
	if(isGuest() && isActive()) {
		// The ledger is the host's playthrough; the guest's throwaway
		// travel-from world must not erase what has been lived.
		return;
	}
	std::remove(playthroughIdFile().string().c_str());
	g_session.playthroughId.clear();
	loadOrMintPlaythroughId();
	if(!g_seenCutscenes.empty()) {
		LogInfo << "[coop] new playthrough: story ledger wiped";
	}
	g_seenCutscenes.clear();
	persistStoryLedger();
}

const std::set<std::string, std::less<>> & seenCutsceneNames() {
	return g_seenCutscenes;
}

void adoptSeenCutscene(std::string name) {
	if(!name.empty() && g_seenCutscenes.insert(std::move(name)).second) {
		persistStoryLedger();
	}
}

u32 pingMs() {
	return g_session.peer ? g_session.peer->roundTripTime : 0;
}

s64 bodyInterpDelayMs() {
	float delay = 2.f * 33.f + 25.f + g_session.bodyLatenessMs;
	if(delay < 80.f) {
		delay = 80.f;
	} else if(delay > 450.f) {
		delay = 450.f;
	}
	return s64(delay);
}

void flush() {

	pumpDelayedPackets();

	if(g_session.host && isHost()) {
		// The beacon exists to be heard BEFORE anyone joins; it must not sit
		// behind the have-a-peer gate.
		PlatformInstant beaconNow = platform::getTime();
		if(beaconNow - g_session.lastBeacon >= 2000ms) {
			g_session.lastBeacon = beaconNow;
			sendDiscoveryBeacon();
		}
	}

	if(!g_session.host || !g_session.peer || !g_session.handshaken) {
		if(g_session.host) {
			enet_host_flush(g_session.host);
		}
		return;
	}

	PlatformInstant now = platform::getTime();

	// Let the lateness figures breathe back down (10ms of pad per second), so
	// the draw delay tightens again after a bad stretch.
	if(g_session.lastLatenessDecay != PlatformInstant(0)) {
		float decay = float(toMsi(now - g_session.lastLatenessDecay)) * 0.01f;
		g_session.entityLatenessMs = (g_session.entityLatenessMs > decay)
		                             ? g_session.entityLatenessMs - decay : 0.f;
		g_session.bodyLatenessMs = (g_session.bodyLatenessMs > decay)
		                           ? g_session.bodyLatenessMs - decay : 0.f;
	}
	g_session.lastLatenessDecay = now;

	if(g_session.worldRequestDeferred && isHost() && g_currentArea
	   && !BLOCK_PLAYER_CONTROLS && !isInCinematic() && !cinematicBorder.isActive()) {
		g_session.worldRequestDeferred = false;
		LogInfo << "[coop] cutscene over; sending the world now";
		sendGlobalState();
	}

	saveGuestProfileIfDue(false);

	// [coop-item] A roll call of this player's belongings, so the two logs can
	// be laid side by side when something disappears out of someone's hands.
	{
		static PlatformInstant lastRollCall = 0;
		if(isPlaying() && now - lastRollCall >= 5000ms) {
			lastRollCall = now;
			logOwnBelongings(isGuest() ? "guest roll call" : "host roll call");
		}
	}

	if(g_session.partnerHealAccum > 0.f
	   && (g_session.partnerHealAccum >= 2.f || now - g_session.partnerHealSince >= 400ms)) {
		reportPartnerEffect(PartnerFxHeal, g_session.partnerHealAccum);
		g_session.partnerHealAccum = 0.f;
	}

	if(now - g_session.lastAvatar >= AvatarInterval) {
		g_session.lastAvatar = now;
		sendAvatar();
	}

	// Only the machine that owns the area publishes what is in it, and only
	// when the other player is actually there to see it. Most snapshots carry
	// only what changed; every fifteenth carries everything, so one lost
	// packet can never hide a change for longer than that.
	bool sharing = hasWorldAuthority() && sharingArea();
	if(sharing) {
		/*
		 * A fight near either player deserves a faster beat: sixty snapshots a
		 * second instead of twenty, which also lets the other machine draw
		 * closer to the present. The difference-only gate keeps the extra beats
		 * nearly free - a creature that did not move adds nothing to a packet.
		 */
		bool combat = false;
		Entity * partner = avatarEntity();
		for(Entity & npc : entities.inScene(IO_NPC)) {
			if(isAvatarEntity(&npc) || !npc._npcdata) {
				continue;
			}
			if(!(npc._npcdata->behavior & BEHAVIOUR_FIGHT)) {
				continue;
			}
			if((entities.player() && glm::distance(npc.pos, entities.player()->pos) < 1200.f)
			   || (partner && glm::distance(npc.pos, partner->pos) < 1200.f)) {
				combat = true;
				break;
			}
		}
		PlatformDuration interval = combat ? 16ms : SnapshotInterval;
		if(now - g_session.lastSnapshot >= interval) {
			g_session.lastSnapshot = now;
			bool full = !g_session.wasSharing || now - g_session.lastKeyframe >= 750ms;
			if(full) {
				g_session.lastKeyframe = now;
			}
			Writer writer(MsgEntities);
			writer.put(u32(toMsi(now)));
			writer.put(u8(toMsi(interval)));
			writeEntitySnapshot(writer, full);
			send(writer, ChannelSnapshot);
		}
	}
	g_session.wasSharing = sharing;

	if(isReplica() && now - g_session.lastAudit >= 5000ms) {
		g_session.lastAudit = now;
		Writer writer(MsgWorldAudit);
		writeWorldAudit(writer);
		send(writer, ChannelEvent);
	}

	if(now - g_session.lastNetLog >= 5000ms) {
		g_session.lastNetLog = now;
		LogInfo << "[coop-net] ping " << (g_session.peer ? g_session.peer->roundTripTime : 0)
		        << "ms, world delay " << entityInterpDelayMs() << "ms (jitter "
		        << s64(g_session.entityLatenessMs) << "ms), body delay "
		        << bodyInterpDelayMs() << "ms (jitter " << s64(g_session.bodyLatenessMs) << "ms)";
	}

	enet_host_flush(g_session.host);

}

Role role() {
	return g_session.role;
}

Status status() {
	return g_session.status;
}

const char * statusText() {
	return g_session.statusText.c_str();
}

bool isPlaying() {
	return g_session.role != Role::Offline && g_session.peer != nullptr
	       && g_session.handshaken && g_session.synchronised;
}

AreaId remoteArea() {
	return g_session.remoteArea;
}

bool sharingArea() {
	return isPlaying() && g_currentArea && g_session.remoteArea == g_currentArea;
}

bool isReplica() {
	return isGuest() && sharingArea();
}

bool takeJoinNudge() {
	bool nudge = g_session.joinNudge;
	g_session.joinNudge = false;
	return nudge;
}

// -- outgoing world mutations -------------------------------------------------

bool requestAction(const Entity & target) {

	if(!isReplica()) {
		return false;
	}

	Writer writer(MsgAction);
	writer.put(std::string_view(target.idString()));
	send(writer, ChannelEvent);

	/*
	 * Acknowledge the touch this very frame. The RESULT is the authority's to
	 * decide and arrives a ping later - scripts are not reversible, so real
	 * rollback prediction is off the table - but the click itself must never
	 * feel swallowed. This is the same trick every MMO plays.
	 */
	ARX_SOUND_PlayInterface(g_snd.MENU_CLICK);

	return true;
}

bool requestTake(const Entity & item) {

	// Only meaningful while both players are looking at the same floor. Sent
	// from somewhere else it would name an entity id that happens to exist in
	// the other player's area and belongs to something entirely different.
	if(!sharingArea() || isApplyingRemote()) {
		LogDebug("[coop] not reporting pickup of " << item.idString()
		         << " (sharingArea=" << sharingArea() << " applyingRemote=" << isApplyingRemote() << ")");
		return false;
	}

	LogInfo << "[coop] took " << item.idString() << " out of the shared world";

	// Ours now, not scenery: it leaves the shared-world registry with us, or
	// the next audit would ask the authority about an item the authority has
	// already handed over - and be told to destroy it.
	forgetReplicatedEntity(item.idString());

	Writer writer(MsgTake);
	writer.put(std::string_view(item.idString()));
	send(writer, ChannelEvent);

	// Taking is announced, not asked: the item is already in our pack. The other
	// side simply removes it from the shared world so it cannot be taken twice.
	return false;
}

bool requestCombine(const Entity & source, const Entity & target) {

	if(!isReplica() || !sharingArea() || isApplyingRemote()) {
		return false;
	}

	/*
	 * The class path travels with the id because the host has very probably
	 * thrown its own copy of this item away: picking it up destroyed it there,
	 * which is exactly right for an item that is now in a pack the host does
	 * not own. Given the class it can make a stand-in to hand over.
	 */
	Writer writer(MsgCombine);
	writer.put(std::string_view(source.idString()));
	writer.put(std::string_view(source.classPath().string()));
	writer.put(std::string_view(target.idString()));
	send(writer, ChannelEvent);

	LogInfo << "[coop] offering " << source.idString() << " to " << target.idString();

	return true;
}

void reportCombineTaken(std::string_view sourceId) {

	if(!isPlaying()) {
		return;
	}

	Writer writer(MsgCombineTaken);
	writer.put(sourceId);
	send(writer, ChannelEvent);

}

bool giveToPartner(Entity * item) {

	if(!item || !isPartnerScriptContext() || !isPlaying()) {
		return false;
	}

	Writer writer(MsgGiveItem);
	writer.put(std::string_view(item->classPath().string()));
	writer.put(s16((item->ioflags & IO_ITEM) && item->_itemdata ? item->_itemdata->count : 1));
	send(writer, ChannelEvent);

	LogInfo << "[coop] " << item->classPath().string() << " was earned by the other player";

	/*
	 * They have it now, so this machine must not.
	 *
	 * An item the script has just conjured never belonged to the shared world
	 * and leaves without a word. One lifted out of the world does belong to it,
	 * and its removal is news: destroy() records it in the savegame and tells
	 * the other machine, so nobody is left looking at a reward twice.
	 */
	if(item->scriptload) {
		delete item;
	} else {
		item->destroy();
	}

	return true;
}

void reportItemDropped(const Entity & item, const Vec3f & at, const Vec3f & velocity) {

	if(!sharingArea() || isApplyingRemote()) {
		return;
	}

	// The id is what the other side acts on; the class path is only a fallback
	// for the case where they no longer have this entity at all, which happens
	// when it was carried across from somewhere they never saw it.
	Writer writer(MsgDrop);
	writer.put(std::string_view(item.idString()));
	writer.put(std::string_view(item.classPath().string()));
	writer.put(s16((item.ioflags & IO_ITEM) && item._itemdata ? item._itemdata->count : 1));
	writer.put(item.durability);
	writer.put(at);
	writer.put(item.angle.getYaw());
	writer.put(velocity);
	send(writer, ChannelEvent);

	LogInfo << "[coop] put " << item.idString() << " down in the shared world";

	/*
	 * A placed object is ours for a moment: hold off the snapshot that still
	 * describes it lying where it used to be, or it jumps back.
	 *
	 * A THROWN object is the opposite. The authority is about to simulate its
	 * whole flight, and those positions are the only true ones - refusing them
	 * for two seconds would show the object hanging where it left the hand and
	 * then teleporting to wherever it had long since come to rest.
	 */
	if(velocity == Vec3f(0.f)) {
		noteLocalEdit(item.idString());
	}

}

bool requestHit(const Entity & target, float damage, u32 damageType) {

	if(!isReplica() || isApplyingRemote()) {
		return false;
	}

	Writer writer(MsgHitEntity);
	writer.put(std::string_view(target.idString()));
	writer.put(damage);
	writer.put(damageType);
	send(writer, ChannelEvent);

	return true;
}

void announceSpawn(const Entity & entity) {

	// Only the machine that owns the area gets to say what is in it, and only
	// when the other player is standing in it to see.
	if(!isPlaying() || !hasWorldAuthority() || !sharingArea()) {
		return;
	}

	Writer writer(MsgEntitySpawn);
	writer.put(std::string_view(entity.classPath().string()));
	writer.put(s32(entity.instance()));
	writer.put(entity.pos);
	writer.put(entity.angle.getYaw());
	writer.put(s16((entity.ioflags & IO_ITEM) && entity._itemdata ? entity._itemdata->count : 1));
	writer.put(entity.durability);
	send(writer, ChannelEvent);

}

// -- outgoing notifications ----------------------------------------------------

void reportEntityDestroyed(const Entity & entity) {

	if(!sharingArea() || isApplyingRemote()) {
		return;
	}

	// Destroying things in a shared area is the authority's business. A guest
	// echoing it back would ask the host to destroy something twice.
	if(isReplica()) {
		return;
	}

	// The player, the other player's body, and anything else that exists only
	// on this machine are not part of the world we share.
	if(entity.index() == EntityHandle_Player || isAvatarEntity(&entity)
	   || (entity.ioflags & IO_NOSAVE)) {
		return;
	}

	Writer writer(MsgEntityGone);
	writer.put(std::string_view(entity.idString()));
	send(writer, ChannelEvent);

	LogInfo << "[coop] " << entity.idString() << " is gone from the shared world";

}

void reportPlayerDamage(float damage, u32 damageType) {

	if(!isPlaying() || damage <= 0.f) {
		return;
	}

	Writer writer(MsgDamagePlayer);
	writer.put(damage);
	writer.put(damageType);
	send(writer, ChannelEvent);

}

void reportDeath() {
	if(isPlaying()) {
		sendBare(MsgPlayerDied, ChannelEvent);
	}
}

void reportRevive() {
	if(isPlaying()) {
		sendBare(MsgPlayerRevive, ChannelEvent);
	}
}

void reportSpell(int spellType, float level, u32 flags, s64 durationMs,
                 const std::string & targetId, const std::string & casterId) {

	if(!isPlaying() || isApplyingRemote()) {
		return;
	}

	Writer writer(MsgSpellCast);
	writer.put(s32(spellType));
	writer.put(level);
	writer.put(flags);
	writer.put(s64(durationMs));
	writer.put(std::string_view(targetId));
	writer.put(std::string_view(casterId));
	send(writer, ChannelEvent);

}

void reportSpellEnd(int spellType) {

	if(!isPlaying() || isApplyingRemote()) {
		return;
	}

	Writer writer(MsgSpellEnd);
	writer.put(s32(spellType));
	send(writer, ChannelEvent);

}

void reportReward(long xp, long gold) {

	if(!isPlaying() || isApplyingRemote() || (xp <= 0 && gold <= 0)) {
		return;
	}

	Writer writer(MsgReward);
	writer.put(s32(xp));
	writer.put(s32(gold));
	send(writer, ChannelEvent);

}

void reportQuest(std::string_view questKey) {

	if(!isPlaying() || isApplyingRemote() || questKey.empty()) {
		return;
	}

	Writer writer(MsgQuest);
	writer.put(questKey);
	send(writer, ChannelEvent);

}

/*!
 * Share what we can cast.
 *
 * A rune is knowledge, not an object - learning one takes nothing away from
 * anybody, so both players simply keep the union of the two sets. The whole set
 * goes over rather than the one rune just learned: it costs four bytes, it is
 * the same message whether it is sent on learning one or on meeting for the
 * first time, and a lost packet repairs itself the next time either of them
 * learns anything.
 */
void reportRunes() {

	if(!isPlaying() || isApplyingRemote()) {
		return;
	}

	Writer writer(MsgRunes);
	writer.put(u32(player.rune_flags));
	send(writer, ChannelEvent);

}

void reportKey(std::string_view key) {

	if(!isPlaying() || isApplyingRemote() || key.empty()) {
		return;
	}

	Writer writer(MsgKeyring);
	writer.put(key);
	send(writer, ChannelEvent);

}

void reportMapMarker(float x, float y, int level, std::string_view name) {

	if(!isPlaying() || isApplyingRemote()) {
		return;
	}

	Writer writer(MsgMapMarker);
	writer.put(x);
	writer.put(y);
	writer.put(s32(level));
	writer.put(name);
	send(writer, ChannelEvent);

}

void notifyOther(std::string_view text) {

	if(!isPlaying() || text.empty()) {
		return;
	}

	Writer writer(MsgNotify);
	writer.put(text);
	send(writer, ChannelEvent);

}

void sendTravelHold(s32 fadeMs) {

	if(!isPlaying()) {
		return;
	}

	Writer writer(MsgTravelHold);
	writer.put(fadeMs);
	send(writer, ChannelControl);

}

bool travelHoldActive() {
	return g_session.travelHold;
}

bool localPlayerArrivalProtected() {
	return isPlaying() && g_session.selfArrivedAt != PlatformInstant(0)
	       && platform::getTime() - g_session.selfArrivedAt < 2500ms;
}

bool partnerArrivalProtected() {
	return isPlaying() && g_session.partnerPresentSince != PlatformInstant(0)
	       && platform::getTime() - g_session.partnerPresentSince < 2500ms;
}

void sendTravelOrder(u32 area, std::string_view target, long angle, bool confirm) {

	if(!isPlaying()) {
		return;
	}

	Writer writer(MsgTravel);
	writer.put(area);
	writer.put(target);
	writer.put(s32(angle));
	writer.put(confirm);
	send(writer, ChannelControl);

	LogInfo << "[coop] sent travel order: area " << area << " target " << target;

}

void sendTravelCancel() {

	if(!isPlaying()) {
		return;
	}

	sendBare(MsgTravelCancel, ChannelControl);

}

void sendVoice(const u8 * data, size_t size) {

	// Only worth sending when they are close enough to hear it at all - and
	// sharingArea() is also what guarantees there is a body to speak from.
	if(!isPlaying() || !sharingArea() || !data || size == 0) {
		return;
	}

	Writer writer(MsgVoice);
	writer.put(u16(size));
	writer.putRaw(data, size);
	send(writer, ChannelVoice);

}

void reportLocalZone() {

	if(!isReplica() || !isPlaying()) {
		if(!g_session.lastReportedZone.empty()) {
			g_session.lastReportedZone.clear();
		}
		return;
	}

	Zone * zone = ARX_PATH_GetPlayerZone();
	std::string name = zone ? zone->name : std::string();

	if(name == g_session.lastReportedZone) {
		return;
	}

	if(!g_session.lastReportedZone.empty()) {
		Writer writer(MsgZoneLeave);
		writer.put(std::string_view(g_session.lastReportedZone));
		send(writer, ChannelEvent);
	}

	if(!name.empty()) {
		Writer writer(MsgZoneEnter);
		writer.put(std::string_view(name));
		send(writer, ChannelEvent);
		// Remembered past leaving it again: a faller crosses a door zone in
		// well under a second, and the answer may have to come from us.
		g_session.lastEnteredZone = name;
		g_session.lastEnteredZoneAt = platform::getTime();
	}

	g_session.lastReportedZone = std::move(name);

}

void reportAreaChange(AreaId area) {

	if(!isActive() || !g_session.peer) {
		return;
	}

	Writer writer(MsgAreaRequest);
	writer.put(u32(area.handleData()));
	send(writer, ChannelControl);

}

void onAreaLoaded(AreaId area) {

	releaseTravelHold(false);
	g_session.selfArrivedAt = platform::getTime();
	g_transitTraceUntil = g_session.selfArrivedAt + 10000ms;
	g_session.lastEnteredZone.clear();

	if(g_session.freshSpawnVitals) {
		g_session.freshSpawnVitals = false;
		/*
		 * The received world made this player a CLONE of the host's -
		 * stats, gear, pockets, everything. That clone is stripped and
		 * replaced: the character saved for this playthrough if one
		 * exists, a fresh hero if not.
		 */
		logOwnBelongings("guest, world received (host's clone)");
		applyGuestIdentity();
		logOwnBelongings("guest, own character installed");
		restoreVitalsForSpawn();
	}

	if(isPlaying() && BLOCK_PLAYER_CONTROLS && player.lifePool.current > 0.f) {
		// An arrival must never leave the player locked: whatever sequence
		// state rode in with the load, its driving timers may be muted here
		// and would never let go. Controls and borders only - nothing else
		// is touched, so the AI cannot be collateral damage this time.
		LogInfo << "[coop] arrival was control-locked; releasing";
		BLOCK_PLAYER_CONTROLS = false;
		cinematicBorder.reset();
	}

	if(isGuest() && isPlaying() && entities.player()) {
		/*
		 * The guest arrives ALREADY INSIDE whatever zone surrounds its spawn -
		 * judged here, at the load itself, because the shared-area state the
		 * replica check reads settles a few frames later, and in that window
		 * the vanilla zone pass fired the starting cell's vision trap locally:
		 * controls off, cutscene bars up, and the unlock waiting at the end
		 * of a speech a muted replica never plays. No entry edge, no trap.
		 * While together, the AUTHORITY decides zones for this player - and
		 * a trap the host already consumed simply no longer exists there.
		 */
		entities.player()->inzone = ARX_PATH_GetPlayerZone();
	}

	/*
	 * NO arrival "repair". The game's own arrival markers may legally hang
	 * in mid-air - the hole to level 15 drops you from a shaft ABOVE the
	 * room's ceiling, and gravity carries you through. A validator that
	 * catches that fall and sets the player on the nearest standable
	 * surface plants them on the ROOF, in the void. Arrivals are the level
	 * designer's business and gravity's; not ours.
	 */

	if(!isActive()) {
		return;
	}

	reportAreaChange(area);

	/*
	 * Everything we were tracking belonged to the area we just left. The body
	 * among it: loading a level frees every entity, so the handle we are
	 * holding now names a slot that has already been reused by whatever the new
	 * area put there. Forget it without deleting - the teardown did that - or
	 * we would end up driving, and eventually destroying, a stranger.
	 */
	resetAvatar();
	resetReplication();
	mutableAvatar().present = (g_session.remoteArea == area);

	g_session.lastAreaLoad = platform::getTime();

	g_session.lastArrivalWasDoor = g_session.pendingDoorArrival;
	g_session.pendingDoorArrival = false;
	g_session.lastReportedZone.clear();

	/*
	 * A guest arriving in the area the host already occupies takes its place
	 * beside them, exactly as it does on joining - unless this arrival came
	 * through a door, in which case the door's destination marker is exact
	 * and is respected, the same as it is for the first player. The snap
	 * remains the safety net for every arrival that has no marker to trust:
	 * joining, loading a save, or any path we have not thought of.
	 */
	if(isGuest() && g_session.handshaken && g_session.remoteArea == area
	   && !g_session.lastArrivalWasDoor) {
		g_session.joinNudge = true;
	}

}

void onAreaLeaving(AreaId from, AreaId to, std::string_view target) {

	(void) from;

	if(!isActive()) {
		return;
	}

	/*
	 * Announce the destination NOW, before the ten-second load begins, because
	 * a loading game cannot speak. Announced only on arrival, the other player
	 * spends the whole load still believing we share their area - which keeps
	 * their world muted as a replica, so the door they then jump into does
	 * nothing until our load ends. The stall is exactly our load time.
	 */
	if(to) {
		reportAreaChange(to);
		if(g_session.host) {
			/*
			 * Written is not sent. Without this flush the announcement sits
			 * in the outgoing queue while the load blocks the game, and the
			 * other player spends our whole load still muted as a replica -
			 * falling THROUGH door zones that cannot fire for them.
			 */
			enet_host_flush(g_session.host);
		}
	}

	// A transition that names a destination marker will land exactly; remember
	// that so the arrival does not get second-guessed by the snap.
	g_session.pendingDoorArrival = !target.empty();

	destroyAvatarEntity();

}

} // namespace coop

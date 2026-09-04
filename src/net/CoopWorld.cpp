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

#include "net/CoopWorld.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "animation/Animation.h"
#include "core/Core.h"
#include "core/GameTime.h"
#include "game/Damage.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "game/Spells.h"
#include "game/magic/Spell.h"
#include "graphics/particle/ParticleEffects.h"
#include "physics/Physics.h"
#include "gui/Dragging.h"
#include "gui/MiniMap.h"
#include "io/log/Logger.h"
#include "io/resource/ResourcePath.h"
#include "net/CoopInterp.h"
#include "net/CoopNet.h"
#include "net/CoopPlayer.h"
#include "platform/Time.h"
#include "scene/GameSound.h"
#include "scene/Interactive.h"
#include "script/Script.h"
#include "script/ScriptEvent.h"

namespace coop {

namespace {

/*!
 * Nesting depth of "we are applying something the other machine told us".
 *
 * A counter rather than a flag because applying one change can legitimately
 * trigger another - dropping an item runs its script, which may move a third
 * thing - and a flag would be cleared too early by the inner scope.
 */
int g_applyDepth = 0;

/*!
 * Upper bound on how many entities go in one snapshot.
 *
 * A busy room holds well under this; the cap exists so that a pathological
 * level cannot produce a packet large enough to stall the connection. When it
 * bites, the entities left out simply keep their last known state for a frame.
 */
constexpr size_t MaxSnapshotEntities = 192;

/*!
 * The last few places the authority said each replicated entity was.
 *
 * Keyed by id string, because entity indices are recycled and would silently
 * start pointing at a different creature. Entries for entities that no longer
 * exist are dropped as they are encountered rather than swept, since the map
 * only ever holds one area's worth. The history, the blending and the brief
 * guessing across a lost packet all live in MotionTrack; see CoopInterp.h.
 */
/*!
 * One animation layer as the authority plays it, pinned to the moment on the
 * authority's clock when the clip began.
 *
 * The playhead itself is never copied across - copying a playhead is how a
 * swing stutters and starts a beat late. Because the replica draws the world
 * a step in the past, news of a clip starting always arrives BEFORE the
 * drawn timeline reaches that moment, so every clip can be performed exactly
 * on time: the wind-up begins at the same point of the approach on both
 * screens, the same variant of the same animation, in the same phase.
 */
struct ReplicaLayer {
	u8 index = 0xff;
	u8 alt = 0;
	u16 flags = 0;
	s64 startMs = 0;
	//! The playhead exactly as it arrived, kept only to spot the clip starting over.
	s64 playhead = 0;
	/*!
	 * The authority has begun this same clip again, and we must follow it back.
	 *
	 * Idle breathing is not a looping animation. The engine plays it once and,
	 * when it ends, starts it again by hand - same clip, same flags, playhead
	 * back to zero. Nothing about that is visible in what the clip IS, only in
	 * where its playhead went, so this is the one thing that says it happened.
	 *
	 * It matters because a replica may otherwise never wind a one-shot back
	 * (see the note further down about settled doors). Without this, a creature
	 * breathes once on the other player's screen and then holds its breath for
	 * the rest of the game.
	 */
	bool restarted = false;
	bool valid = false;
};

struct ReplicaState {
	MotionTrack track;
	ReplicaLayer layers[MAX_ANIM_LAYERS];
	//! Where it was when its room was last worked out; see the use below.
	Vec3f roomPos = Vec3f(0.f);
	bool roomKnown = false;
};

std::map<std::string, ReplicaState, std::less<>> g_replicaTracks;

/*!
 * Perform one replicated layer at its true place on the drawn timeline.
 *
 * Between corrections the clip advances at this machine's own frame rate;
 * the playhead is only moved when it drifts more than a blink from where
 * the timeline says it must be, so playback stays butter-smooth AND true.
 */
void driveReplicatedAnim(Entity & entity, size_t layerIndex, ReplicaLayer & state,
                         s64 renderTime) {

	if(!state.valid) {
		return;
	}

	AnimLayer & layer = entity.animlayer[layerIndex];

	if(state.index == 0xff || state.index >= MAX_ANIMS) {
		layer.cur_anim = nullptr;
		return;
	}

	ANIM_HANDLE * anim = entity.anims[state.index];
	if(!anim || anim->anims.empty()) {
		return;
	}

	s64 elapsed = renderTime - state.startMs;
	if(elapsed < 0) {
		// The authority started this after the moment being drawn: on the
		// drawn timeline it has not happened yet. Let whatever came before it
		// finish; this clip begins the instant the timeline reaches it.
		return;
	}

	size_t alt = std::min(size_t(state.alt), anim->anims.size() - 1);

	bool restarted = state.restarted;
	if(layer.cur_anim != anim) {
		AcquireLastAnim(&entity);
		ResetAnim(layer);
		layer.cur_anim = anim;
		restarted = false; // a fresh clip, not the same one going round again
	} else if(restarted) {
		// Begin it again exactly as the authority did, and only now that the
		// drawn timeline has reached the moment it began over there.
		AcquireLastAnim(&entity);
		ResetAnim(layer);
	}
	state.restarted = false;
	layer.altidx_cur = alt;
	layer.flags = AnimUseType::load(state.flags);

	// Where must the playhead be? Real time since the clip began, wrapped for
	// loops so both machines are on the same step of the same walk cycle,
	// clamped for one-shots so a finished swing holds its final frame.
	EERIE_ANIM * variant = anim->anims[alt];
	s64 durationMs = variant ? toMsi(variant->anim_time) : 0;
	s64 target = elapsed;
	if(durationMs > 0) {
		if(layer.flags & EA_LOOP) {
			target = elapsed % durationMs;
		} else if(target > durationMs) {
			target = durationMs;
		}
	}
	if(std::abs(toMsi(layer.ctime) - target) > 60) {
		if(!(layer.flags & EA_LOOP) && !restarted && toMsi(layer.ctime) > target) {
			/*
			 * A one-shot may NEVER be corrected backwards. A finished door
			 * animation holds its final frame while the authority's clock
			 * keeps running, which makes the clip look ever more recently
			 * started - and correcting for that replayed the last slice of
			 * every settled door and lever on the replica's screen, forever.
			 * Forward corrections only; the past stays finished.
			 */
		} else {
			layer.ctime = std::chrono::milliseconds(target);
		}
	}

}

/*!
 * What was last put on the wire for each entity, so the next snapshot can
 * leave out everything the other machine already knows.
 *
 * Only differences travel. Position and facing use small thresholds rather
 * than equality, so a creature settling by a hundredth of a unit does not
 * chatter; animation playback time is deliberately NOT compared, because it
 * advances every frame and the receiver runs the animation itself anyway,
 * resynchronising only when a full second off. Every fifteenth snapshot
 * ignores this cache and carries everything, so one lost packet can never
 * hide a change for longer than that.
 */
struct SentState {
	Vec3f pos = Vec3f(0.f);
	float yaw = 0.f;
	float pitch = 0.f;
	float roll = 0.f;
	u8 show = 0;
	float life = 0.f;
	u8 anim[MAX_ANIM_LAYERS] = {};
	u8 alt[MAX_ANIM_LAYERS] = {};
	u16 animFlags[MAX_ANIM_LAYERS] = {};
	s32 animTime[MAX_ANIM_LAYERS] = {};
	float invisibility = 0.f;
	float ignition = 0.f;
	u16 gameFlags = 0;
	u32 ioFlags = 0;
	/*
	 * The glow a script puts on things that matter.
	 *
	 * HALO -o is how the game says "this one is worth your attention" - a quest
	 * item, the thing a character has just asked you to find. Scripts set it,
	 * scripts run on the authority, and it was never in the snapshot: the other
	 * player walked past lit-up things seeing plain scenery. Only SOME items
	 * looked wrong, because anything already glowing when the level loaded came
	 * across in the savegame and was fine.
	 */
	u16 haloFlags = 0;
	Color3f haloColor = Color3f::black;
	float haloRadius = 0.f;
};

std::map<std::string, SentState, std::less<>> g_sentStates;

/*
 * A number for every entity, so its name is spelled out once instead of forty
 * times a second. Handed out by the authority and remembered by both sides
 * until the area changes.
 */
/*
 * What the other machine has confirmed, and what is still in the air.
 *
 * Differences are measured against the confirmed picture, so a snapshot that
 * never arrives cannot leave a field wrong: the next one still sees that field
 * as unconfirmed and sends it again.
 */
std::map<std::string, SentState, std::less<>> g_ackedStates;
std::map<u32, std::vector<std::pair<std::string, SentState>>> g_inFlight;

u64 g_snapEntities = 0;      //!< entity records written since the last report
u64 g_snapWouldHave = 0;     //!< what those records used to cost

std::map<std::string, u16, std::less<>> g_handles;
std::map<u16, std::string> g_handleNames;
u16 g_nextHandle = 1;

u16 handleFor(const std::string & id, bool & isNew) {
	auto known = g_handles.find(id);
	if(known != g_handles.end()) {
		isNew = false;
		return known->second;
	}
	isNew = true;
	u16 handle = g_nextHandle++;
	g_handles[id] = handle;
	g_handleNames[handle] = id;
	return handle;
}

void forgetHandles() {
	g_handles.clear();
	g_handleNames.clear();
	g_nextHandle = 1;
	g_ackedStates.clear();
	g_inFlight.clear();
}

bool sentStateDiffers(const SentState & a, const SentState & b) {
	for(size_t l = 0; l < MAX_ANIM_LAYERS; l++) {
		if(a.anim[l] != b.anim[l] || a.alt[l] != b.alt[l]
		   || a.animFlags[l] != b.animFlags[l]) {
			return true;
		}
		/*
		 * How far through a clip is deliberately not compared - it changes
		 * every frame and the other machine runs the clip itself. But a
		 * playhead that has gone BACKWARDS is not playback, it is the clip
		 * being started again, and that is the only trace such a restart
		 * leaves. Idle breathing is exactly this: one play at a time, begun
		 * again by hand each time it ends. Miss it and creatures stop
		 * breathing on the other player's screen.
		 */
		if(b.animTime[l] + 20 < a.animTime[l]) {
			return true;
		}
	}
	return glm::distance(a.pos, b.pos) > 0.5f
	    || glm::abs(MAKEANGLE(a.yaw - b.yaw + 180.f) - 180.f) > 0.5f
	    || a.show != b.show
	    || glm::abs(a.life - b.life) > 0.25f
	    || glm::abs(a.invisibility - b.invisibility) > 0.01f
	    || glm::abs(a.ignition - b.ignition) > 0.25f
	    || a.gameFlags != b.gameFlags
	    || a.ioFlags != b.ioFlags
	    || a.haloFlags != b.haloFlags
	    || a.haloColor != b.haloColor
	    || glm::abs(a.haloRadius - b.haloRadius) > 0.01f;
}

/*!
 * Entities this machine changed a moment ago, and when.
 *
 * A snapshot describes the world as the authority saw it before our change
 * reached them. Applying it verbatim is what makes an item a player has just
 * moved jump back to where it was, in front of them. Holding these back for
 * long enough to cover a round trip lets the two sides agree quietly instead.
 */
std::map<std::string, PlatformInstant, std::less<>> g_localEdits;

//! Generous enough to cover a bad connection, short enough to self-heal.
constexpr PlatformDuration LocalEditGrace = 2s;

/*!
 * The entity flags worth sending, and the only ones ever written on receipt.
 *
 * Deliberately not the whole word. Most of what lives in ioflags is the
 * entity's type - item, NPC, prop - which is decided when it is created, is the
 * same on both machines, and is what the destructor consults to know which
 * member of a union to free. Copying a type bit that had somehow drifted would
 * turn a bad packet into a crash. These three are the ones a script can change
 * while the game runs, and they are the ones that decide whether a player can
 * walk through something.
 */
constexpr EntityFlags ReplicatedEntityFlags = IO_NO_COLLISIONS | IO_INVULNERABILITY
                                              | IO_NO_NPC_COLLIDE;

/*!
 * Likewise for the game flags a script can toggle mid-play.
 *
 * GFLAG_INTERACTIVITY is the one the cursor consults: it is what makes a thing
 * highlight and answer to a click. Scripts grant it when the story makes
 * something usable - bendable bars, a corpse worth searching - and without it
 * in the snapshot those stayed usable on one screen and scenery on the other.
 */
constexpr GameFlags ReplicatedGameFlags = GFLAG_INVISIBILITY | GFLAG_MEGAHIDE
                                          | GFLAG_NOCOMPUTATION | GFLAG_NO_PHYS_IO_COL
                                          | GFLAG_INTERACTIVITY;

//! Which entities are worth telling the other machine about.
bool shouldReplicate(const Entity & entity) {

	if(entity.index() == EntityHandle_Player) {
		return false; // each player's own body is sent as an avatar, not as an entity
	}

	if(isAvatarEntity(&entity)) {
		return false; // that is the other player's own body coming back at them
	}

	/*
	 * A camera the other player is watching a scene through is the exception.
	 *
	 * Cameras are otherwise worth nothing on the wire - nobody looks through
	 * ours - but one handed over IS the scene: it flies a path, and if its
	 * position never leaves this machine the watcher sits in a locked view of
	 * wherever the level happened to park it. Sent before the treat-zone and
	 * visibility tests, both of which a camera fails by nature.
	 */
	if(&entity == partnerCameraEntity()) {
		return true;
	}

	if(entity.ioflags & (IO_CAMERA | IO_MARKER)) {
		return false; // no visible state to speak of
	}

	if(!(entity.gameFlags & GFLAG_ISINTREATZONE)) {
		return false; // far enough away that the other player cannot see it either
	}

	if(entity.show != SHOW_FLAG_IN_SCENE && entity.show != SHOW_FLAG_MEGAHIDE) {
		return false; // in someone's pack or linked to something else
	}

	return true;
}

//! Find which animation slot a handle sits in; both machines share the set.
u8 animIndexOf(const Entity & entity, const ANIM_HANDLE * anim) {

	if(!anim) {
		return 0xff;
	}

	for(size_t i = 0; i < MAX_ANIMS && i < 0xff; i++) {
		if(entity.anims[i] == anim) {
			return u8(i);
		}
	}

	return 0xff;
}

} // anonymous namespace

size_t inFlightCount() {
	return g_inFlight.size();
}

size_t ackedCount() {
	return g_ackedStates.size();
}

void unconfirm(u16 handle) {

	// Look up what that number stands for and forget we ever got it across.
	auto named = g_handleNames.find(handle);
	if(named != g_handleNames.end()) {
		g_ackedStates.erase(named->second);
	}

}

void ackSnapshot(u32 beat) {

	// Everything sent at or before that beat has landed, so it becomes the
	// picture the next differences are measured against.
	for(auto it = g_inFlight.begin(); it != g_inFlight.end(); ) {
		if(it->first > beat) {
			break;
		}
		for(const auto & entry : it->second) {
			g_ackedStates[entry.first] = entry.second;
		}
		it = g_inFlight.erase(it);
	}

}

void takeSnapshotCost(u64 & records, u64 & wouldHave) {
	records = g_snapEntities;
	wouldHave = g_snapWouldHave;
	g_snapEntities = 0;
	g_snapWouldHave = 0;
}


// ---------------------------------------------------------------------------

bool isApplyingRemote() {
	return g_applyDepth > 0;
}

ApplyScope::ApplyScope() {
	g_applyDepth++;
}

ApplyScope::~ApplyScope() {
	g_applyDepth--;
}

// -- entity snapshots ----------------------------------------------------------

void writeEntitySnapshot(Writer & writer, bool full, u32 beat) {

	if(full) {
		// Rebuilt from scratch, so entries for entities that no longer exist
		// do not linger - and the other side is about to be told every name
		// again anyway.
		g_sentStates.clear();
		forgetHandles();
	}

	struct Entry {
		Entity * entity;
		u16 handle;
		u16 mask;
		SentState state;
	};

	std::vector<Entry> entries;
	entries.reserve(64);

	for(Entity & entity : entities) {

		if(!shouldReplicate(entity)) {
			continue;
		}

		/*
		 * Only what somebody could plausibly notice.
		 *
		 * A rat on the far side of the level was being described forty-five
		 * times a second to a player who cannot see it, will not hear it, and
		 * whose screen it never touches. Anything beyond both players gets its
		 * news when they come near - and because differences are measured
		 * against what was acknowledged, the first update they get on
		 * approach is the whole truth, not a stale fragment.
		 *
		 * Generous on purpose: twice the view distance, so nothing pops.
		 */
		{
			const float reach = 6000.f;
			Entity * mine = entities.player();
			Entity * theirs = avatarEntity();
			bool near = (mine && glm::distance(entity.pos, mine->pos) < reach)
			            || (theirs && glm::distance(entity.pos, theirs->pos) < reach);
			if(!near) {
				continue;
			}
		}

		SentState state;
		state.pos = entity.pos;
		state.yaw = entity.angle.getYaw();
		state.pitch = entity.angle.getPitch();
		state.roll = entity.angle.getRoll();
		state.show = u8(entity.show);
		state.life = (entity.ioflags & IO_NPC) ? entity._npcdata->lifePool.current : 0.f;
		for(size_t l = 0; l < MAX_ANIM_LAYERS; l++) {
			state.anim[l] = animIndexOf(entity, entity.animlayer[l].cur_anim);
			state.alt[l] = u8(entity.animlayer[l].altidx_cur);
			state.animFlags[l] = u16(entity.animlayer[l].flags);
			state.animTime[l] = s32(toMsi(entity.animlayer[l].ctime));
		}
		state.invisibility = entity.invisibility;
		state.ignition = entity.ignition;
		state.gameFlags = u16(entity.gameFlags & ReplicatedGameFlags);
		state.ioFlags = u32(entity.ioflags & ReplicatedEntityFlags);
		state.haloFlags = u16(entity.halo_native.flags);
		state.haloColor = entity.halo_native.color;
		state.haloRadius = entity.halo_native.radius;

		std::string id(entity.idString());
		bool isNew = false;
		u16 handle = handleFor(id, isNew);

		auto known = g_ackedStates.find(id);
		const bool first = isNew || known == g_ackedStates.end();
		u16 mask = 0;

		if(first) {
			// everything, and the name with it
			mask = SnapName | SnapPos | SnapYaw | SnapShow | SnapLife | SnapAnim
			       | SnapGlow | SnapFlags | SnapHalo;
			if(entity.ioflags & IO_NPC) {
				mask |= SnapNpc;
			}
		} else {
			const SentState & was = known->second;
			if(was.pos != state.pos) {
				mask |= SnapPos;
			}
			if(was.yaw != state.yaw || was.pitch != state.pitch
			   || was.roll != state.roll) {
				mask |= SnapYaw;
			}
			if(was.show != state.show) {
				mask |= SnapShow;
			}
			if(was.life != state.life) {
				mask |= SnapLife;
			}
			for(size_t l = 0; l < MAX_ANIM_LAYERS; l++) {
				/*
				 * The playhead is not news. It advances every frame by itself,
				 * and the other machine advances its own copy the same way -
				 * so sending it says nothing they could not work out, and it
				 * was setting this bit on every entity that was moving at all,
				 * every tick, at thirty two bytes a time.
				 *
				 * What IS news: a different clip, different flags, or a
				 * playhead that went backwards, which means the clip started
				 * again and they need to know when.
				 */
				if(was.anim[l] != state.anim[l] || was.alt[l] != state.alt[l]
				   || was.animFlags[l] != state.animFlags[l]
				   || state.animTime[l] < was.animTime[l]) {
					mask |= SnapAnim;
					break;
				}
			}
			if(was.invisibility != state.invisibility || was.ignition != state.ignition) {
				mask |= SnapGlow;
			}
			if(was.gameFlags != state.gameFlags || was.ioFlags != state.ioFlags) {
				mask |= SnapFlags;
			}
			if(was.haloFlags != state.haloFlags || was.haloColor != state.haloColor
			   || was.haloRadius != state.haloRadius) {
				mask |= SnapHalo;
			}
		}

		if(!mask) {
			continue;                  // nothing they do not already know
		}

		// TEMPORARY: what the authority says about things that are not people
		if((mask & SnapPos) && !(entity.ioflags & IO_NPC)) {
			LogWarning << "[item-trace] host sends " << id << " at "
			           << entity.pos.x << "," << entity.pos.y << "," << entity.pos.z
			           << (first ? " (first time)" : "");
		}

		entries.push_back({ &entity, handle, mask, state });
		if(entries.size() >= MaxSnapshotEntities) {
			break;
		}

	}

	// what this cost, against what the old format would have: a name of about
	// sixteen bytes plus a hundred and eighteen of record, every entity, every
	// time
	g_snapEntities += entries.size();
	g_snapWouldHave += entries.size() * 134;

	if(!entries.empty()) {
		auto & flight = g_inFlight[beat];
		flight.reserve(entries.size());
		for(const Entry & entry : entries) {
			flight.emplace_back(std::string(entry.entity->idString()), entry.state);
		}
		// A partner that never acknowledges must not grow this forever.
		while(g_inFlight.size() > 400) {
			g_inFlight.erase(g_inFlight.begin());
		}
	}

	writer.put(u16(entries.size()));

	for(const Entry & entry : entries) {

		Entity * entity = entry.entity;
		writer.put(entry.handle);
		writer.put(entry.mask);

		if(entry.mask & SnapName) {
			writer.put(std::string_view(entity->idString()));
		}
		if(entry.mask & SnapPos) {
			// quarter-unit precision, which is a twentieth of a footstep
			writer.put(u16(glm::clamp(entity->pos.x, 0.f, 16383.f) * SnapPosScale));
			writer.put(s16(glm::clamp(entity->pos.y, -8000.f, 8000.f) * SnapPosScale));
			writer.put(u16(glm::clamp(entity->pos.z, 0.f, 16383.f) * SnapPosScale));
		}
		if(entry.mask & SnapYaw) {
			// all three: a thrown object or a tilted prop uses the other two,
			// and the old full record carried them
			writer.put(u16(MAKEANGLE(entity->angle.getYaw()) * 182.f));
			writer.put(u16(MAKEANGLE(entity->angle.getPitch()) * 182.f));
			writer.put(u16(MAKEANGLE(entity->angle.getRoll()) * 182.f));
		}
		if(entry.mask & SnapShow) {
			writer.put(u8(entity->show));
		}
		if(entry.mask & SnapLife) {
			writer.put(entry.state.life);
		}
		if(entry.mask & SnapAnim) {
			for(size_t l = 0; l < MAX_ANIM_LAYERS; l++) {
				const AnimLayer & layer = entity->animlayer[l];
				writer.put(animIndexOf(*entity, layer.cur_anim));
				writer.put(u8(layer.altidx_cur));
				writer.put(u16(layer.flags));
				writer.put(s32(toMsi(layer.ctime)));
			}
		}
		if(entry.mask & SnapGlow) {
			writer.put(entity->invisibility);
			writer.put(entity->ignition);
		}
		if(entry.mask & SnapFlags) {
			writer.put(u16(entity->gameFlags & ReplicatedGameFlags));
			writer.put(u32(entity->ioflags & ReplicatedEntityFlags));
		}
		if(entry.mask & SnapHalo) {
			writer.put(u16(entity->halo_native.flags));
			writer.put(u8(glm::clamp(entity->halo_native.color.r, 0.f, 1.f) * 255.f));
			writer.put(u8(glm::clamp(entity->halo_native.color.g, 0.f, 1.f) * 255.f));
			writer.put(u8(glm::clamp(entity->halo_native.color.b, 0.f, 1.f) * 255.f));
			writer.put(u16(glm::clamp(entity->halo_native.radius, 0.f, 6000.f)));
		}
		if(entry.mask & SnapNpc) {
			writer.put(entity->_npcdata->lifePool.max);
			writer.put(entity->_npcdata->vvpos);
		}

	}

}

/*
 * The last complete picture we have of each entity the authority described.
 *
 * A snapshot only carries what changed, so this is what the changes are laid
 * on top of. Cleared when the area changes, because the handles are handed
 * out fresh each time.
 */
struct KnownState {
	std::string id;
	Vec3f pos = Vec3f(0.f);
	Anglef angle = Anglef();
	u8 show = 0;
	bool isNpc = false;
	float life = 0.f;
	float maxLife = 0.f;
	float vvpos = 0.f;
	u8 anim[MAX_ANIM_LAYERS] = { };
	u8 alt[MAX_ANIM_LAYERS] = { };
	u16 animFlags[MAX_ANIM_LAYERS] = { };
	s32 animTime[MAX_ANIM_LAYERS] = { };
	/*
	 * When each clip began on their clock.
	 *
	 * Worked out once, when an animation update actually arrives, and kept.
	 * Recomputing it every snapshot from a playhead that is no longer being
	 * sent walks the start forward with the clock and pins the animation to
	 * one frame - which is a creature standing still and shivering.
	 */
	s64 animStart[MAX_ANIM_LAYERS] = { };
	bool animKnown = false;
	float invisibility = 0.f;
	float ignition = 0.f;
	u16 gameFlags = 0;
	u32 ioFlags = 0;
	u16 haloFlags = 0;
	Color3f haloColor = Color3f::black;
	float haloRadius = 0.f;
};
static std::map<u16, KnownState> g_known;

/*
 * Handles whose updates we declined since the last acknowledgement.
 *
 * Reported back so the authority stops counting them as delivered - otherwise
 * a position we refused is remembered as one we have.
 */
static std::vector<u16> g_declined;

void takeDeclined(std::vector<u16> & out) {
	out.swap(g_declined);
	g_declined.clear();
}

void forgetSnapshotHandles() {
	g_known.clear();
}

/*
 * The newest beat we have applied, for the authority to build its next
 * differences against.
 */
static u32 g_appliedBeat = 0;

u32 lastAppliedBeat() {
	return g_appliedBeat;
}

void readEntitySnapshot(Reader & reader, u32 serverTimeMs) {

	g_appliedBeat = serverTimeMs;

	size_t count = reader.getU16();

	for(size_t i = 0; i < count && reader.ok(); i++) {

		u16 handle = reader.getU16();
		u16 mask = reader.getU16();
		if(!reader.ok()) {
			return;
		}

		KnownState & known = g_known[handle];

		if(mask & SnapName) {
			known.id = reader.getString();
		}
		if(mask & SnapPos) {
			known.pos.x = float(reader.getU16()) / SnapPosScale;
			known.pos.y = float(reader.getS16()) / SnapPosScale;
			known.pos.z = float(reader.getU16()) / SnapPosScale;
		}
		if(mask & SnapYaw) {
			known.angle.setYaw(float(reader.getU16()) / 182.f);
			known.angle.setPitch(float(reader.getU16()) / 182.f);
			known.angle.setRoll(float(reader.getU16()) / 182.f);
		}
		if(mask & SnapShow) {
			known.show = reader.getU8();
		}
		if(mask & SnapLife) {
			known.life = reader.getFloat();
		}
		if(mask & SnapAnim) {
			for(size_t l = 0; l < MAX_ANIM_LAYERS; l++) {
				known.anim[l] = reader.getU8();
				known.alt[l] = reader.getU8();
				known.animFlags[l] = reader.getU16();
				known.animTime[l] = reader.getS32();
				known.animStart[l] = s64(serverTimeMs) - s64(known.animTime[l]);
			}
			known.animKnown = true;
		}
		if(mask & SnapGlow) {
			known.invisibility = reader.getFloat();
			known.ignition = reader.getFloat();
		}
		if(mask & SnapFlags) {
			known.gameFlags = reader.getU16();
			known.ioFlags = reader.getU32();
		}
		if(mask & SnapHalo) {
			known.haloFlags = reader.getU16();
			known.haloColor.r = float(reader.getU8()) / 255.f;
			known.haloColor.g = float(reader.getU8()) / 255.f;
			known.haloColor.b = float(reader.getU8()) / 255.f;
			known.haloRadius = float(reader.getU16());
		}
		if(mask & SnapNpc) {
			known.isNpc = true;
			known.maxLife = reader.getFloat();
			known.vvpos = reader.getFloat();
		}

		if(!reader.ok()) {
			return;
		}
		if(known.id.empty()) {
			continue;      // a handle we were never introduced to
		}

		// from here on, exactly the values the old full record carried
		const std::string & id = known.id;
		const Vec3f & pos = known.pos;
		const Anglef & angle = known.angle;
		const u8 show = known.show;
		const bool isNpc = known.isNpc;
		const float life = known.life;
		const float maxLife = known.maxLife;
		const float vvpos = known.vvpos;
		const float invisibility = known.invisibility;
		const float ignition = known.ignition;
		const u16 gameFlags = known.gameFlags;
		const u32 ioFlags = known.ioFlags;
		const u16 haloFlags = known.haloFlags;
		const Color3f haloColor = known.haloColor;
		const float haloRadius = known.haloRadius;

		ReplicaLayer layers[MAX_ANIM_LAYERS];
		for(size_t l = 0; l < MAX_ANIM_LAYERS; l++) {
			layers[l].index = known.anim[l];
			layers[l].alt = known.alt[l];
			layers[l].flags = known.animFlags[l];
			// When this clip began on their clock, kept from the update that
			// said so - not recomputed here, or the clip never advances.
			layers[l].startMs = known.animStart[l];
			layers[l].playhead = s64(serverTimeMs) - known.animStart[l];
			layers[l].valid = known.animKnown;
		}

		Entity * entity = entities.getById(id);
		if(!entity) {
			// The authority has something we do not. That is normal right after
			// a level change, and it resolves itself as soon as our load
			// finishes; there is nothing useful to do about it here.
			continue;
		}

		if(entity->index() == EntityHandle_Player || isAvatarEntity(entity)) {
			continue;
		}

		/*
		 * Anything we are already holding is ours and is no longer part of the
		 * shared floor. The host does not know that yet - our "I took this" is
		 * still crossing the wire - and its snapshot still describes the item
		 * lying where we found it. Applying that would drag the item back out
		 * of the bag we just put it in, which reads on screen as loot that
		 * refuses to be picked up.
		 */
		/*
		 * You own what you touch, until it comes to rest.
		 *
		 * While a player is handling an item it is theirs outright and nothing
		 * corrects it - which is the whole point, because that is what makes it
		 * behave exactly as it does in a single player game, where there is
		 * nobody to argue with about where it is.
		 *
		 * Ownership does not end when the item leaves your hand. A thrown or
		 * dropped item is still falling, and the authority will not know where
		 * it finally lands until its own copy has finished falling too. So it
		 * stays yours until its physics settles, and only then is it handed
		 * back to the shared world.
		 */
		if(ownsLocally(entity)) {
			if(!(entity->ioflags & IO_NPC)) {
				LogWarning << "[item-trace] guest ignores " << id << ": we are holding it";
			}
			g_declined.push_back(handle);
			g_localEdits[std::string(id)] = platform::getTime();
			g_replicaTracks.erase(id);
			continue;
		}

		// Likewise for something we have only just moved: their snapshot was
		// composed before they heard about it.
		auto edit = g_localEdits.find(id);
		if(edit != g_localEdits.end()) {
			if(platform::getTime() - edit->second < LocalEditGrace) {
				if(!(entity->ioflags & IO_NPC)) {
					LogWarning << "[item-trace] guest ignores " << id
					           << ": we moved it a moment ago";
				}
				g_declined.push_back(handle);
				g_replicaTracks.erase(id);
				continue;
			}
			g_localEdits.erase(edit);
		}

		/*
		 * Only what this message actually said.
		 *
		 * The reader keeps a full picture per entity and lays each update on
		 * top, which is how a message can carry three bytes instead of a
		 * hundred - but it means every message can offer a position, whether
		 * or not one was sent. That matters for exactly one case and it is a
		 * case players notice: an item this machine picked up is deliberately
		 * NOT moved by the authority's snapshots, and the position it declined
		 * would otherwise sit in the picture waiting to be applied the next
		 * time anything else about that item changed. Which is loot leaping
		 * out of your bag and back onto the floor a second after you took it.
		 */
		// Record where they are as one more point on their timeline;
		// smoothReplicatedEntities() draws a moment slightly in the past,
		// blending between recorded points.
		auto inserted = g_replicaTracks.emplace(id, ReplicaState());
		ReplicaState & state = inserted.first->second;
		MotionTrack & track = state.track;

		/*
		 * Move it only if this message actually carried a position.
		 *
		 * The reader keeps a full picture per entity and lays each update on
		 * top of it, which is how a message costs three bytes instead of a
		 * hundred - but it means every message can offer a position whether or
		 * not one was sent. That matters for one case players notice at once:
		 * an item this machine picked up is deliberately not moved by the
		 * authority's snapshots, and the position declined then would sit in
		 * the picture waiting to be applied the next time anything else about
		 * that item changed. Which is loot jumping out of the bag you just put
		 * it in.
		 *
		 * Everything below - health, visibility, animation, flags - still
		 * applies, because those are the authority's to state at any time.
		 */
		const bool moved = (mask & SnapPos) != 0;
		if(!(entity->ioflags & IO_NPC)) {
			LogWarning << "[item-trace] guest gets " << id << (moved ? " with" : " without")
			           << " a position; it is at " << entity->pos.x << "," << entity->pos.z
			           << ", told " << pos.x << "," << pos.z
			           << ", track has " << track.count;
		}
		if(moved) {
		/*
		 * A camera we are watching a scene through flies; bodies walk.
		 *
		 * The rule below reads a long step between reports as a teleport and
		 * snaps rather than blends - right for a door slamming across a room,
		 * wrong for a cutscene camera sweeping a corridor, which covers that
		 * much ground honestly and then jerks its way through the scene. Its
		 * motion is always motion.
		 */
		bool watchedCamera = (entity == g_cameraEntity && (entity->ioflags & IO_CAMERA));

		/*
		 * A history nobody has added to in a while is not history, it is a
		 * fossil.
		 *
		 * Positions are only sent when they change, so a barrel that has sat
		 * still for a minute has one point in its track, timestamped a minute
		 * ago. Blending from that towards a fresh one means the render time -
		 * which deliberately lags the newest report - lands nowhere near the
		 * new point, and the barrel does not visibly move until a SECOND
		 * update arrives to bracket it. Which is exactly what "I have to move
		 * everything twice" looks like.
		 *
		 * So a track that has gone quiet is thrown away and started again from
		 * where the thing actually is.
		 */
		const s64 quiet = 400;
		if(track.count != 0 && s64(serverTimeMs) - track.newest().timeMs > quiet) {
			track.clear();
		}

		if(track.count != 0 && !watchedCamera
		   && glm::distance(track.newest().pos, pos) > 300.f) {
			// Moved further than anything walks in a snapshot's time - a
			// teleport or a door slamming across the room. That is not motion
			// to blend through: forget the history and start over there.
			track.clear();
		}
		if(track.count == 0) {

			/*
			 * Nothing is ever teleported into place if it can be walked there
			 * instead.
			 *
			 * This is the only path that moves a replica without blending, and
			 * so the only one that can produce a visible jump. Rather than put
			 * the entity where the authority says, the timeline is started from
			 * where it appears to be RIGHT NOW - one interpolation delay in the
			 * past - and the authority's position pushed on top. The blending
			 * below then carries it across over the next few frames, and what
			 * was a jump becomes a short glide.
			 *
			 * Two things still arrive instantly, because gliding would be wrong:
			 * something genuinely new, which has no believable current position,
			 * and something that has moved further than any glide should cover,
			 * which is a real teleport and should look like one.
			 */
			float gap = glm::distance(entity->pos, pos);
			if(gap > 1.f && gap < 400.f && (entity->show == SHOW_FLAG_IN_SCENE)) {
				track.push(s64(serverTimeMs) - coop::entityInterpDelayMs(),
				           entity->pos, entity->angle);
			} else {
				entity->pos = pos;
				entity->angle = angle;
			}

		}
		track.push(s64(serverTimeMs), pos, angle);
		}

		// The animation layers ride the same clock; smoothReplicatedEntities()
		// performs them at the matching moment of the drawn timeline.
		for(size_t l = 0; l < MAX_ANIM_LAYERS; l++) {
			/*
			 * A playhead only ever moves forward through a clip. When the same
			 * clip comes back with its playhead earlier than last time, it was
			 * begun again over there - which is how idle breathing loops, one
			 * play at a time. Carry the news until it has been performed, so a
			 * snapshot arriving before the drawn timeline gets there cannot
			 * lose it.
			 */
			const ReplicaLayer & before = state.layers[l];
			layers[l].restarted = before.valid && before.index == layers[l].index
			                      && layers[l].playhead + 20 < before.playhead;
			if(before.restarted && before.index == layers[l].index) {
				layers[l].restarted = true;
			}
			state.layers[l] = layers[l];
		}

		entity->show = EntityShowState(show);

		if(isNpc && (entity->ioflags & IO_NPC)) {
			entity->_npcdata->lifePool.current = life;
			entity->_npcdata->lifePool.max = maxLife;
			entity->_npcdata->vvpos = vvpos;
		}

		entity->invisibility = invisibility;
		entity->ignition = ignition;
		entity->gameFlags &= ~ReplicatedGameFlags;
		entity->gameFlags |= GameFlags::load(gameFlags) & ReplicatedGameFlags;

		// Whether this thing still blocks the way is decided where its script
		// runs, which is the other machine. Without this, a grid the host has
		// turned to smoke stays solid here.
		entity->ioflags &= ~ReplicatedEntityFlags;
		entity->ioflags |= EntityFlags::load(ioFlags) & ReplicatedEntityFlags;

		/*
		 * Both copies, not just the native one.
		 *
		 * halo_native is what a script sets; halo is what the renderer actually
		 * looks at, and the two are joined only by ARX_HALO_SetToNative() -
		 * called by the script command, by a couple of spells, and at level
		 * load. None of those run over here for a glow lit on the other
		 * machine, so setting the native value alone changed nothing visible,
		 * until something unrelated happened to touch the same entity and the
		 * glow appeared out of nowhere.
		 */
		entity->halo_native.flags = HaloFlags::load(haloFlags);
		entity->halo_native.color = haloColor;
		entity->halo_native.radius = haloRadius;
		ARX_HALO_SetToNative(entity);

		// The authority is looking at it, so we are too: without this the
		// replicated entities fall out of our own treat zone and stop drawing.
		entity->gameFlags |= GFLAG_ISINTREATZONE;

	}

}

void smoothReplicatedEntities() {

	if(g_replicaTracks.empty()) {
		return;
	}

	// Draw the shared world a step in the past of the authority's clock. How
	// big a step is measured from the connection, not guessed; see
	// entityInterpDelayMs().
	s64 renderTime = coop::estimatedRemoteNowMs() - coop::entityInterpDelayMs();

	for(auto it = g_replicaTracks.begin(); it != g_replicaTracks.end(); ) {

		Entity * entity = entities.getById(it->first);
		if(!entity) {
			it = g_replicaTracks.erase(it);
			continue;
		}

		ReplicaState & state = it->second;

		/*
		 * Never move what this player has hold of.
		 *
		 * A dragged item is on the end of the cursor, and the authority has not
		 * heard about that yet - so its idea of where the item is, is wherever
		 * the item was picked up from. Correcting towards that drags it out of
		 * the player's hand and back onto the floor. It shows up as a snap when
		 * the cursor moves quickly, because a slow drag never gets far enough
		 * from the remembered position for the correction to be visible.
		 */
		if(entity == g_draggedEntity) {
			++it;
			continue;
		}

		Vec3f pos;
		Anglef angle;
		if(sampleSmoothed(state.track, renderTime, g_framedelay, pos, angle)) {

			entity->pos = pos;
			entity->angle = angle;

			/*
			 * Which room it is now standing in has to be worked out again.
			 *
			 * Arx only draws something if its room can be seen from the room the
			 * camera is in, and an entity's room is normally recalculated by the
			 * movement code in NPC.cpp - which never runs here, because on this
			 * machine the creature is a replica whose position simply arrives.
			 * Without this the room is whatever it was when the creature was
			 * first seen, so walking into a cell leaves it clipped away: the
			 * shadow still lands on the floor, drawn by a path that does not ask
			 * about rooms, while the creature itself is not drawn at all.
			 *
			 * Only when it has actually gone somewhere, though. Working out a
			 * room means searching the level geometry around a point, and asking
			 * for that on every replica on every frame costs far more than it is
			 * worth - rooms are the size of rooms, so a step is soon enough.
			 */
			if(!state.roomKnown || glm::distance(state.roomPos, entity->pos) > 40.f) {
				state.roomPos = entity->pos;
				state.roomKnown = true;
				entity->requestRoomUpdate = true;
			}

			if(entity->ioflags & IO_NPC) {
				entity->_npcdata->vvpos = entity->pos.y;
			}

			// The authority is looking at it, so we are too. Kept here rather
			// than only on receipt, because with difference-only snapshots an
			// unmoving entity can go most of a second without being mentioned.
			entity->gameFlags |= GFLAG_ISINTREATZONE;

		}

		// And what it is DOING, performed at the same drawn moment its motion
		// belongs to - which is what makes a sword swing start at the same
		// point of the approach on both screens.
		for(size_t l = 0; l < MAX_ANIM_LAYERS; l++) {
			driveReplicatedAnim(*entity, l, state.layers[l], renderTime);
		}

		++it;
	}

}

/*!
 * Is this item this player's business rather than the shared world's?
 *
 * True while it is being carried or dragged, and onwards until whatever it was
 * thrown into stops moving. For as long as that holds, nothing corrects it and
 * this machine simulates it itself, which is what makes handling an item feel
 * the same as it does in a game with nobody else in it.
 */
bool ownsLocally(const Entity * entity) {

	if(!entity) {
		return false;
	}

	if(locateInInventories(const_cast<Entity *>(entity)) || entity == g_draggedEntity) {
		return true;
	}

	// Only something we were recently holding can still be settling from it.
	if(g_localEdits.find(entity->idString()) == g_localEdits.end()) {
		return false;
	}

	return entity->obj && entity->obj->pbox && entity->obj->pbox->active == 1;

}

//! Is this entity carried or worn by the player at this machine?
static bool isOwnBelonging(const Entity * entity) {

	if(!entity || !entities.player()) {
		return false;
	}

	for(EntityHandle equipped : player.equiped) {
		if(equipped != EntityHandle() && equipped == entity->index()) {
			return true;
		}
	}

	InventoryPos where = locateInInventories(entity);
	return where && where.container == entities.player();

}

void writeWorldAudit(Writer & writer) {

	/*
	 * Where the last audit stopped. A busy area holds more than one audit can
	 * carry, and always starting at the beginning means the entities at the
	 * end are never checked at all - so each round picks up where the last
	 * one left off and the whole cast gets its turn.
	 */
	static size_t cursor = 0;

	std::vector<Entity *> reported;
	reported.reserve(g_replicaTracks.size());

	size_t seen = 0;
	size_t skip = g_replicaTracks.empty() ? 0 : (cursor % g_replicaTracks.size());
	for(const auto & entry : g_replicaTracks) {
		if(seen++ < skip) {
			continue;
		}
		if(Entity * entity = entities.getById(entry.first)) {
			/*
			 * Never audit our own belongings. The audit asks the authority
			 * "do you still have these?", and the authority does not have
			 * what is in this player's pack - it removed it the moment they
			 * picked it up. Reporting it would answer back "destroy it",
			 * which is an item vanishing out of its owner's hands.
			 */
			if(isOwnBelonging(entity)) {
				continue;
			}
			reported.push_back(entity);
			if(reported.size() >= MaxSnapshotEntities) {
				break;
			}
		}
	}
	cursor = skip + reported.size();

	writer.put(u16(reported.size()));

	for(Entity * entity : reported) {
		writer.put(std::string_view(entity->idString()));
		writer.put(entity->pos);
		writer.put((entity->ioflags & IO_NPC) ? entity->_npcdata->lifePool.current : 0.f);
		writer.put(u8(entity->show));
		writer.put(u16(entity->gameFlags & ReplicatedGameFlags));
		writer.put(u32(entity->ioflags & ReplicatedEntityFlags));
	}

	/*
	 * And which of the level's own lights we believe are burning.
	 *
	 * One bit each, so two hundred lights cost twenty-five bytes. Nobody was
	 * comparing these at all, and a light nobody compares can be wrong for as
	 * long as the level lasts - which is how one player walked through a level
	 * where every torch was lit and the other saw them dark.
	 */
	u16 lights = u16(std::min<size_t>(g_staticLights.size(), 4096));
	writer.put(lights);
	u8 bits = 0;
	for(u16 i = 0; i < lights; i++) {
		if(g_staticLights[i].m_ignitionStatus) {
			bits |= u8(1 << (i % 8));
		}
		if(i % 8 == 7 || i + 1 == lights) {
			writer.put(bits);
			bits = 0;
		}
	}

}

void applyWorldAudit(Reader & reader) {

	size_t count = reader.getU16();
	size_t repaired = 0;

	for(size_t i = 0; i < count && reader.ok(); i++) {

		std::string id = reader.getString();
		Vec3f pos = reader.getVec3f();
		float life = reader.getFloat();
		u8 show = reader.getU8();
		u16 gameFlags = reader.getU16();
		u32 ioFlags = reader.getU32();

		if(!reader.ok()) {
			return;
		}

		Entity * entity = entities.getById(id);
		if(!entity) {
			// They still have something that no longer exists here; repeat the
			// destruction order in case the first one was lost.
			LogWarning << "[coop-sync] " << id << " exists only on the other machine; repeating its removal";
			sendEntityGone(id);
			continue;
		}

		/*
		 * Tolerances, not equality. Their view runs a step in the past by
		 * design, so a moving creature legitimately reads a little behind;
		 * only a gap no amount of interpolation explains counts as divergence.
		 */
		const char * field = nullptr;
		if(glm::distance(entity->pos, pos) > 250.f) {
			field = "position";
		} else if((entity->ioflags & IO_NPC)
		          && glm::abs(entity->_npcdata->lifePool.current - life) > 2.f) {
			field = "life";
		} else if(u8(entity->show) != show) {
			field = "show";
		} else if(u16(entity->gameFlags & ReplicatedGameFlags) != gameFlags) {
			field = "game flags";
		} else if(u32(entity->ioflags & ReplicatedEntityFlags) != ioFlags) {
			field = "entity flags";
		}

		if(field) {
			LogWarning << "[coop-sync] " << id << " diverged (" << field << "); resending it";
			g_sentStates.erase(id);
			repaired++;
		}

	}

	// the lights, one bit each
	u16 lights = reader.getU16();
	size_t litFixed = 0;
	u8 bits = 0;
	for(u16 i = 0; i < lights && reader.ok(); i++) {
		if(i % 8 == 0) {
			bits = reader.getU8();
		}
		if(i >= g_staticLights.size()) {
			continue;
		}
		bool theirs = (bits & (1 << (i % 8))) != 0;
		if(theirs != g_staticLights[i].m_ignitionStatus) {
			sendLightState(i, g_staticLights[i].m_ignitionStatus);
			litFixed++;
		}
	}
	if(litFixed) {
		LogWarning << "[coop-sync] " << litFixed
		           << " lights disagreed; the authority's word resent";
	}

	if(repaired) {
		LogWarning << "[coop-sync] queued " << repaired << " diverged entities for repair";
	}

}

//! On-the-wire kinds for MsgWorldFx.
enum WorldFxKind : u8 {
	FxCollisionMats = 0,
	FxCollisionNames = 1,
	FxBlood = 2,
	FxBlood2 = 3,
};

void applyWorldFx(Reader & reader) {

	u8 kind = reader.getU8();

	switch(kind) {

		case FxCollisionMats: {
			u8 mat1 = reader.getU8();
			u8 mat2 = reader.getU8();
			float volume = reader.getFloat();
			float power = reader.getFloat();
			Vec3f pos = reader.getVec3f();
			if(reader.ok()) {
				ARX_SOUND_PlayCollision(Material(mat1), Material(mat2), volume, power, pos, nullptr);
			}
			break;
		}

		case FxCollisionNames: {
			std::string name1 = reader.getString();
			std::string name2 = reader.getString();
			float volume = reader.getFloat();
			float power = reader.getFloat();
			Vec3f pos = reader.getVec3f();
			if(reader.ok()) {
				ARX_SOUND_PlayCollision(name1, name2, volume, power, pos, nullptr);
			}
			break;
		}

		case FxBlood: {
			Vec3f pos = reader.getVec3f();
			float dmgs = reader.getFloat();
			std::string sourceId = reader.getString();
			if(reader.ok()) {
				if(Entity * source = entities.getById(sourceId)) {
					ARX_PARTICLES_Spawn_Blood(pos, dmgs, source->index());
				}
			}
			break;
		}

		case FxBlood2: {
			Vec3f pos = reader.getVec3f();
			float dmgs = reader.getFloat();
			u32 color = reader.getU32();
			std::string targetId = reader.getString();
			if(reader.ok()) {
				if(Entity * target = entities.getById(targetId)) {
					ARX_PARTICLES_Spawn_Blood2(pos, dmgs, Color::fromRGBA(ColorRGBA(color)), target);
				}
			}
			break;
		}

		default: break;
	}

}

void resetReplication() {
	g_replicaTracks.clear();
	g_sentStates.clear();
	// The numbers standing in for names belong to one area's cast. Carrying
	// them across a level change would have the next area's rat answering to
	// the last area's handle.
	forgetHandles();
	forgetSnapshotHandles();
}

void forgetReplicatedEntity(std::string_view entityId) {
	g_replicaTracks.erase(std::string(entityId));
	g_sentStates.erase(std::string(entityId));
}

// -- story state ----------------------------------------------------------------

void writeGlobalState(Writer & writer) {

	writer.put(u32(g_currentArea.handleData()));

	// Global script variables are the story: which doors are unlocked, which
	// conversations have happened, how far along each quest is.
	writer.put(u32(svar.size()));
	for(const SCRIPT_VAR & variable : svar) {
		writer.put(std::string_view(variable.name));
		writer.put(s32(variable.ival));
		writer.put(variable.fval);
		writer.put(std::string_view(variable.text));
	}

	writer.put(u32(g_playerQuestLogEntries.size()));
	for(const std::string & quest : g_playerQuestLogEntries) {
		writer.put(std::string_view(quest));
	}

	writer.put(u32(g_playerKeyring.size()));
	for(const std::string & key : g_playerKeyring) {
		writer.put(std::string_view(key));
	}

	writer.put(u32(g_miniMap.mapMarkerCount()));
	for(size_t i = 0; i < g_miniMap.mapMarkerCount(); i++) {
		MiniMap::MapMarkerData marker = g_miniMap.mapMarkerGet(i);
		writer.put(marker.m_pos.x);
		writer.put(marker.m_pos.y);
		writer.put(u32(marker.m_level.handleData()));
		writer.put(std::string_view(marker.m_name));
	}

	const auto & seen = seenCutsceneNames();
	writer.put(u32(seen.size()));
	for(const std::string & name : seen) {
		writer.put(std::string_view(name));
	}

}

void readGlobalState(Reader & reader) {

	u32 hostArea = reader.getU32();
	(void) hostArea; // the area is tracked separately; this is only informational

	u32 variableCount = reader.getU32();
	if(!reader.ok()) {
		return;
	}

	std::vector<SCRIPT_VAR> received;
	received.reserve(variableCount);

	for(u32 i = 0; i < variableCount && reader.ok(); i++) {
		SCRIPT_VAR variable;
		variable.name = reader.getString();
		variable.ival = reader.getS32();
		variable.fval = reader.getFloat();
		variable.text = reader.getString();
		received.push_back(std::move(variable));
	}

	u32 questCount = reader.getU32();
	std::vector<std::string> quests;
	for(u32 i = 0; i < questCount && reader.ok(); i++) {
		quests.push_back(reader.getString());
	}

	u32 keyCount = reader.getU32();
	std::vector<std::string> keys;
	for(u32 i = 0; i < keyCount && reader.ok(); i++) {
		keys.push_back(reader.getString());
	}

	u32 markerCount = reader.getU32();
	struct Marker {
		float x;
		float y;
		u32 level;
		std::string name;
	};
	std::vector<Marker> markers;
	for(u32 i = 0; i < markerCount && reader.ok(); i++) {
		Marker marker;
		marker.x = reader.getFloat();
		marker.y = reader.getFloat();
		marker.level = reader.getU32();
		marker.name = reader.getString();
		markers.push_back(std::move(marker));
	}

	if(!reader.ok()) {
		LogWarning << "[coop] the story state did not parse, keeping our own";
		return;
	}

	// Replace wholesale rather than merge. A half-merged story - our flag for a
	// door, their flag for the quest behind it - is worse than either one alone.
	svar = std::move(received);

	ARX_PLAYER_Quest_Init();
	for(const std::string & quest : quests) {
		ARX_PLAYER_Quest_Add(quest);
	}

	ARX_KEYRING_Init();
	for(const std::string & key : keys) {
		ARX_KEYRING_Add(key);
	}

	g_miniMap.mapMarkerInit(markers.size());
	for(Marker & marker : markers) {
		g_miniMap.mapMarkerAdd(Vec2f(marker.x, marker.y), MapLevel(marker.level), std::move(marker.name));
	}

	u32 seenCount = reader.getU32();
	for(u32 i = 0; i < seenCount && reader.ok(); i++) {
		adoptSeenCutscene(reader.getString());
	}

	LogInfo << "[coop] story state applied: " << svar.size() << " variables, "
	        << quests.size() << " quest entries, " << keys.size() << " keys, "
	        << seenCount << " lived sequences";

}

// -- world mutations --------------------------------------------------------------

void applyActionRequest(std::string_view entityId) {

	Entity * target = entities.getById(entityId);
	if(!target || !target->script.valid) {
		return;
	}

	/*
	 * Run it in the other player's name. Their body is the sender, so scripts
	 * that ask ^SENDER get "player" as they expect, and the context guard makes
	 * any consequence the script aims at "player" - damage from a trapped
	 * chest, a teleport, a curse - land on the one who actually pulled the
	 * lever. Story-side effects remain shared either way: a lever pulled is a
	 * lever pulled, whoever's hand it was.
	 */
	Entity * cause = avatarEntity();
	ScopedPlayerContext context(cause);
	SendIOScriptEvent(cause ? cause : entities.player(), target, SM_ACTION);

}

void applyTakeRequest(std::string_view entityId) {

	Entity * item = entities.getById(entityId);
	if(!item) {
		LogWarning << "[coop] the other player took " << entityId << ", which we do not have";
		return;
	}

	LogInfo << "[coop] the other player took " << entityId << "; removing it here";

	// It is in the other player's pack now, so it leaves this world. Going
	// through destroy() rather than just hiding it also records the removal in
	// the savegame, so it does not come back when the level is revisited.
	removeFromInventories(item);
	item->destroy();

}

void applyCombineRequest(std::string_view sourceId, std::string_view sourceClass,
                         std::string_view targetId) {

	Entity * target = entities.getById(targetId);
	if(!target || !target->script.valid) {
		LogWarning << "[coop] the other player offered something to " << targetId
		           << ", which we do not have";
		return;
	}

	/*
	 * The item is in their pack, which is why this machine has no copy: taking
	 * it destroyed ours. A script asks two things of what it is handed - what
	 * class it is, and a name to DESTROY or send an event to - and a stand-in
	 * built from the class, wearing their id, answers both.
	 */
	Entity * source = entities.getById(sourceId);
	Entity * standIn = nullptr;
	if(!source) {
		EntityInstance instance = EntityId(sourceId).instance();
		standIn = AddItem(res::path::load(sourceClass), instance > 0 ? instance : -1);
		if(!standIn) {
			LogWarning << "[coop] could not stand in for " << sourceClass;
			return;
		}
		standIn->scriptload = 1;
		standIn->ioflags |= IO_NOSAVE;
		standIn->show = SHOW_FLAG_HIDDEN;
		SendInitScriptEvent(standIn);
		source = standIn;
	}

	{
		// In their name, so a reward goes to the one who did the giving and a
		// trap on it springs on them.
		Entity * cause = avatarEntity();
		ScopedPlayerContext context(cause);
		SendIOScriptEvent(cause ? cause : entities.player(), target, SM_COMBINE,
		                  ScriptParameters(source->idString()));
	}

	/*
	 * Did they keep it? DESTROY is deferred to the end of the frame, so the
	 * question is whether it is queued, not whether it is gone; a script that
	 * pockets it instead has the same answer.
	 */
	bool taken = ARX_INTERACTIVE_IsDestroyPending(source) || bool(locateInInventories(source));

	LogInfo << "[coop] " << targetId << " was offered " << sourceId << " and "
	        << (taken ? "kept it" : "gave it back");

	if(standIn) {
		// A prop, and only ever a prop: the real one is in their pack, and
		// this one leaves without troubling the savegame or the wire.
		ARX_INTERACTIVE_DestroyIOdelayedRemove(standIn);
		removeFromInventories(standIn);
		delete standIn;
	}

	if(taken) {
		reportCombineTaken(sourceId);
	}

}

void applyCombineGold(std::string_view targetId, long giverGold) {

	Entity * target = entities.getById(targetId);
	if(!target || !target->script.valid) {
		LogWarning << "[coop] the other player offered gold to " << targetId
		           << ", which we do not have";
		return;
	}

	/*
	 * In their name, with their purse. The script's ^player_gold check reads
	 * the amount that was in their pack when they clicked, and its ADDGOLD
	 * charges them across the wire - this machine's wallet is never part of
	 * the transaction. The quest state it advances is the world's, which is
	 * the whole point of running it here.
	 */
	Entity * cause = avatarEntity();
	ScopedPlayerContext context(cause);
	setPartnerPurse(giverGold);
	SendIOScriptEvent(cause ? cause : entities.player(), target, SM_COMBINE,
	                  ScriptParameters("gold_coin"));
	clearPartnerPurse();

	LogInfo << "[coop] " << targetId << " was offered gold in the other player's name";

}

void applyChatRequest(std::string_view npcId) {

	Entity * npc = entities.getById(npcId);
	if(!npc || !npc->script.valid) {
		return;
	}

	// In their name, so a line meant for the person who spoke to it reaches
	// them, and so anything it decides to do lands on the right player.
	Entity * cause = avatarEntity();
	ScopedPlayerContext context(cause);
	SendIOScriptEvent(cause ? cause : entities.player(), npc, SM_CHAT);

}

void applyCombineTaken(std::string_view sourceId) {

	Entity * item = entities.getById(sourceId);
	if(!item) {
		// Already gone - the destruction beat the answer here, which is fine.
		return;
	}

	LogInfo << "[coop] " << sourceId << " was accepted; it leaves our pack";

	// One out of a stack, exactly as many as were handed over.
	if((item->ioflags & IO_ITEM) && item->_itemdata && item->_itemdata->count > 1) {
		item->_itemdata->count--;
	} else {
		removeFromInventories(item);
		item->destroy();
	}

}

void applyGiveItem(std::string_view classPath, s16 count) {

	Entity * item = AddItem(res::path::load(classPath));
	if(!item) {
		LogWarning << "[coop] could not make " << classPath << ", which we were given";
		return;
	}

	item->scriptload = 1;
	SendInitScriptEvent(item);

	if((item->ioflags & IO_ITEM) && item->_itemdata) {
		item->_itemdata->count = std::max(s16(1), count);
	}

	// Falls at their feet if there is no room for it, rather than evaporating.
	giveToPlayer(item);

	LogInfo << "[coop] earned " << item->idString() << " on the other machine";

}

void applyItemDropped(std::string_view entityId, std::string_view classPath, s16 count,
                      float durability, const Vec3f & at, float angleYaw,
                      const Vec3f & velocity) {

	Entity * item = entities.getById(entityId);

	if(!item) {
		/*
		 * We have never had this one - they carried it in from elsewhere - so
		 * make one to match. This is the only case that needs the class path.
		 * It keeps THEIR id: the id is the name both machines use for it in
		 * every later message, and an item known by two different names is an
		 * item that cannot be picked up, dropped or destroyed in agreement.
		 */
		EntityInstance instance = EntityId(entityId).instance();
		item = AddItem(res::path::load(classPath), instance > 0 ? instance : -1);
		if(!item) {
			LogWarning << "[coop] could not place dropped item " << classPath;
			return;
		}
		SendInitScriptEvent(item);
		if(durability > 0.f) {
			item->durability = durability;
		}
		LogInfo << "[coop] the other player dropped " << entityId
		        << ", which we did not have; created " << item->idString();
	} else {
		LogInfo << "[coop] the other player put " << entityId << " down here";
	}

	if((item->ioflags & IO_ITEM) && item->_itemdata) {
		item->_itemdata->count = std::max(s16(1), count);
	}

	// It may have been sitting in an inventory on this side until now.
	removeFromInventories(item);
	item->setOwner(nullptr);

	item->angle = Anglef(0.f, angleYaw, 0.f);
	item->show = SHOW_FLAG_IN_SCENE;
	item->gameFlags |= GFLAG_ISINTREATZONE;

	ARX_INTERACTIVE_Teleport(item, at, true);

	/*
	 * Released, not teleported.
	 *
	 * Physics runs on the authority alone - the other machine never steps a
	 * physics box - and an object only moves while its box is active.
	 * ARX_INTERACTIVE_Teleport above switches that box OFF, which is right for
	 * a teleport and wrong for a throw, so an object the other player let go
	 * of simply hung in the air: the one machine that could have made it fall
	 * had just been told it was placed there deliberately.
	 *
	 * Launching it here with the impulse it was really released with puts the
	 * whole flight - arc, bounce, roll - back in the hands of the machine that
	 * owns the world, and the result reaches the thrower as ordinary position
	 * updates. A zero impulse means it was set down rather than thrown, and
	 * then the teleport alone is exactly right.
	 */
	if(velocity != Vec3f(0.f) && item->obj && item->obj->pbox) {
		item->soundtime = 0;
		item->soundcount = 0;
		EERIE_PHYSICS_BOX_Launch(item->obj, item->pos, item->angle, velocity);
	}

	// The authority's next snapshot was composed before this arrived, so leave
	// the item where it has just been put rather than letting that undo it.
	noteLocalEdit(item->idString());

}

void noteLocalEdit(std::string_view entityId) {
	g_localEdits[std::string(entityId)] = platform::getTime();
}

void applyEntitySpawn(std::string_view classPath, s32 instance, const Vec3f & at,
                      float angleYaw, s16 count, float durability) {

	if(entities.getById(EntityId(res::path::load(classPath).filename(), instance).string())) {
		return; // already have it, this is a repeat
	}

	Entity * item = AddItem(res::path::load(classPath), EntityInstance(instance));
	if(!item) {
		return;
	}

	SendInitScriptEvent(item);

	if((item->ioflags & IO_ITEM) && item->_itemdata) {
		item->_itemdata->count = std::max(s16(1), count);
	}

	if(durability > 0.f) {
		item->durability = durability;
	}

	item->pos = at;
	item->initpos = at;
	item->angle = Anglef(0.f, angleYaw, 0.f);
	item->show = SHOW_FLAG_IN_SCENE;
	item->gameFlags |= GFLAG_ISINTREATZONE;

	ARX_INTERACTIVE_Teleport(item, at);

}

void applyHitRequest(std::string_view entityId, float damage, u32 damageType) {

	Entity * target = entities.getById(entityId);
	if(!target || damage <= 0.f) {
		return;
	}

	// The blow came from the other player, so credit it to their body if it is
	// here; otherwise fall back to ours so the hit still has an author and
	// scripts that ask "who hit me" get an answer.
	Entity * source = avatarEntity();
	if(!source) {
		source = entities.player();
	}

	/*
	 * Sanity bound, not a reach check. The claim was already tested on the
	 * claimant's machine against the world as they saw it - which is slightly
	 * in the past, and judging the swing against that past IS the lag
	 * compensation every online game applies. What cannot be honoured is a
	 * claim about something nowhere near them: even after a generous latency
	 * and bow-range allowance, a target this far from the claiming player is a
	 * desync artefact, not an attack.
	 */
	if(avatarEntity() && glm::distance(target->pos, source->pos) > 2500.f) {
		return;
	}

	// The victim's ON HIT and ON OUCH run inside this call; if they retaliate
	// against "player", it should be the player who struck them.
	ScopedPlayerContext context(source);

	DamageType type = DamageType::load(damageType);

	if(target->ioflags & IO_NPC) {
		if(target->index() == EntityHandle_Player) {
			damagePlayer(damage, type, source);
		} else {
			damageNpc(*target, damage, source, nullptr, type, &target->pos);
		}
	} else if(target->ioflags & IO_FIX) {
		damageProp(*target, damage, source, nullptr, type);
	}

}

void applyEntityGone(std::string_view entityId) {

	if(Entity * entity = entities.getById(entityId)) {
		LogWarning << "[coop-item] the other machine ordered " << entityId
		           << " destroyed; ours is " << (isOwnBelonging(entity) ? "OWNED BY US" : "world scenery");
		entity->destroy();
	}

}

// -- magic --------------------------------------------------------------------------

void applyRemoteSpell(int spellType, float level, u32 flags, s64 durationMs,
                      std::string_view targetId, std::string_view casterId) {

	if(spellType <= SPELL_NONE || spellType >= SPELL_TYPES_COUNT) {
		return;
	}

	Entity * caster = casterId.empty() ? avatarEntity() : entities.getById(casterId);
	if(!caster) {
		// They cast it from another area. Anything that would show up here has
		// no business existing, and anything that would not is already theirs.
		return;
	}

	Entity * target = nullptr;
	if(targetId == "player") {
		// A relayed creature cast aimed at THIS machine's player: the full
		// vanilla effect lands on the real person, not on a puppet.
		target = entities.player();
	} else if(!targetId.empty()) {
		target = entities.getById(targetId);
	}

	// The cost was already paid on their machine, and their runes were already
	// checked there. Repeating either here would double-charge them or refuse a
	// spell they legitimately know.
	SpellcastFlags spellFlags = SpellcastFlags::load(flags);
	spellFlags |= SPELLCAST_FLAG_NOMANA | SPELLCAST_FLAG_NOCHECKCANCAST;

	ARX_SPELLS_Launch(SpellType(spellType), *caster, spellFlags, long(level), target,
	                  GameDuration(std::chrono::milliseconds(durationMs)));

}

void applyRemoteSpellEnd(int spellType) {

	if(spellType <= SPELL_NONE || spellType >= SPELL_TYPES_COUNT) {
		return;
	}

	Entity * caster = avatarEntity();
	if(!caster) {
		return;
	}

	spells.endByCaster(caster->index(), SpellType(spellType));

}

} // namespace coop

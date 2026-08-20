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

#include "net/CoopPlayer.h"

#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>

#include "animation/Animation.h"
#include "core/Core.h"
#include "core/GameTime.h"
#include "game/Damage.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "graphics/Draw.h"
#include "graphics/Math.h"
#include "graphics/Renderer.h"
#include "graphics/data/Mesh.h"
#include "core/Config.h"
#include "graphics/texture/TextureStage.h"
#include "gui/Dragging.h"
#include "gui/Hud.h"
#include "gui/Notification.h"
#include "gui/Text.h"
#include "io/log/Logger.h"
#include "math/Angle.h"
#include "net/CoopInterp.h"
#include "net/CoopNet.h"
#include "net/CoopWorld.h"
#include "physics/Collisions.h"
#include "scene/Interactive.h"
#include "scene/Scene.h"
#include "scene/LinkedObject.h"
#include "scene/Object.h"
#include "game/Inventory.h"
#include "io/fs/SystemPaths.h"
#include "script/Script.h"
#include "platform/Time.h"

//! Defined in ArxGame.cpp; the accumulator that shoves the player around.
extern Vec3f PUSH_PLAYER_FORCE;

//! Defined in Player.cpp; whether the camera hangs outside the body.
extern bool EXTERNALVIEW;

namespace coop {

namespace {

Avatar g_avatar;

//! Handle rather than a pointer: level teardown frees entities behind our back.
EntityHandle g_avatarHandle;

//! The weapon class currently hung off the body, so it is only rebuilt on change.
std::string g_avatarWeapon;

/*!
 * The class path of the second body.
 *
 * Deliberately not "player": that name is reserved by the engine and by every
 * script that says "sendevent to player". This body is a separate entity with a
 * name no level uses, so nothing addresses it by accident.
 */
const char * const AvatarClassPath = "graph/obj3d/interactive/npc/human_base/coop_player";

/*!
 * The last few places the other player reported being, on their own clock.
 * The history, the blending and the brief guessing across a lost packet all
 * live in MotionTrack; see CoopInterp.h and pushAvatarSample().
 */
MotionTrack g_bodyTrack;

/*!
 * Find which animation slot a handle sits in.
 *
 * Animations travel as an index into Entity::anims rather than as a path,
 * because both players use the same animation set and an index is one byte
 * where a path is fifty.
 */
u8 findAnimIndex(const Entity * entity, const ANIM_HANDLE * anim) {

	if(!entity || !anim) {
		return 0xff;
	}

	for(size_t i = 0; i < MAX_ANIMS && i < 0xff; i++) {
		if(entity->anims[i] == anim) {
			return u8(i);
		}
	}

	return 0xff;
}

void applyAnimLayer(Entity * body, size_t layer, u8 index, u16 flags, s32 timeMs) {

	arx_assert(body);
	arx_assert(layer < MAX_ANIM_LAYERS);

	AnimLayer & target = body->animlayer[layer];

	if(index == 0xff || index >= MAX_ANIMS) {
		target.cur_anim = nullptr;
		return;
	}

	ANIM_HANDLE * anim = body->anims[index];
	if(!anim) {
		return;
	}

	if(target.cur_anim != anim) {
		// Adopt the new animation, starting at the frame they are on.
		AcquireLastAnim(body);
		ResetAnim(target);
		target.cur_anim = anim;
		target.altidx_cur = 0;
		target.flags = AnimUseType::load(flags);
		target.ctime = std::chrono::milliseconds(timeMs);
		return;
	}

	// Still the same animation, so let it play out here rather than resetting
	// the playhead on every packet. See applyAnim() in CoopWorld.cpp - the two
	// have to agree, or the other player's walk cycle stutters exactly the way
	// the creatures around them would.
	constexpr s64 ResyncThresholdMs = 1000;
	if(std::abs(toMsi(target.ctime) - s64(timeMs)) > ResyncThresholdMs) {
		target.ctime = std::chrono::milliseconds(timeMs);
	}

}

//! Give the body the same animation set the local player has.
void shareAnimations(Entity * body) {

	Entity * source = entities.player();
	if(!source) {
		return;
	}

	for(size_t i = 0; i < MAX_ANIMS; i++) {
		if(!source->anims[i]) {
			continue;
		}
		// Load by path rather than copying the pointer: the animation manager
		// reference counts by path, and a borrowed pointer would be released
		// twice when both entities are destroyed.
		body->anims[i] = EERIE_ANIMMANAGER_Load(source->anims[i]->path);
	}

}

//! Defined below; the body has to be built before it can be given a face.
void paintAvatarFace(Entity * body, u8 skin);

Entity * createAvatarEntity() {

	Entity * source = entities.player();
	if(!source || !source->obj) {
		return nullptr;
	}

	Entity * body = new Entity(AvatarClassPath, EntityInstance(1));

	// Its own mesh copy. Sharing the player's would mean the first-person
	// trimming that hides our own head also hides the other player's.
	body->obj = loadObject("graph/obj3d/interactive/npc/human_base/human_base.teo", false).release();
	if(!body->obj) {
		LogWarning << "[coop] could not load the body mesh for the other player";
		delete body;
		return nullptr;
	}

	// Their face, not a repaint of ours. See paintAvatarFace.
	paintAvatarFace(body, g_avatar.skin);

	body->ioflags = IO_NPC;
	body->_npcdata = new IO_NPCDATA;
	body->_npcdata->lifePool.max = std::max(1.f, g_avatar.maxLife);
	body->_npcdata->lifePool.current = g_avatar.life;
	body->_npcdata->manaPool.max = std::max(1.f, g_avatar.maxMana);
	body->_npcdata->manaPool.current = g_avatar.mana;
	body->_npcdata->vvpos = -99999.f;
	body->_npcdata->xpvalue = 0;
	body->_npcdata->fDetect = -1; // never shown as a red dot: they get their own marker
	body->_npcdata->behavior = BEHAVIOUR_NONE;
	body->_npcdata->reach = 20.f;
	body->_npcdata->summoner = EntityHandle();

	body->armormaterial = "leather";
	body->material = MATERIAL_FLESH;
	body->scriptload = 2; // never written to a savegame
	// IO_FORCEDRAW keeps the body out of the portal culling pass. The other
	// player is the one thing on screen that must never blink out because a
	// room boundary decided it was not worth drawing.
	/*
	 * IO_FORCEDRAW keeps the body out of the portal culling pass. The other
	 * player is the one thing on screen that must never blink out because a
	 * room boundary decided it was not worth drawing.
	 *
	 * IO_NO_COLLISIONS is what lets the two of them share a doorway. This body
	 * is deliberately skipped by ARX_PHYSICS_Apply - it is driven by the person
	 * playing it, not by the AI - which also means the engine's usual
	 * push-creatures-apart step never runs for it. Left solid, two players who
	 * ended up overlapping would wedge each other in place with nothing to
	 * separate them again. The flag only excuses it from the movement cylinder
	 * test: CheckEverythingInSphere() and CheckIOInSphere() both keep finding
	 * it because it carries IO_NPC, so swords, arrows and spells still land.
	 */
	body->ioflags |= IO_NOSAVE | IO_FORCEDRAW | IO_NO_COLLISIONS;
	body->gameFlags |= GFLAG_ISINTREATZONE;
	body->gameFlags &= ~GFLAG_NEEDINIT;
	body->show = SHOW_FLAG_IN_SCENE;
	body->collision = 0;
	body->basespeed = 1.f;
	body->weight = 100.f;
	body->original_height = ARXCHARACTER::baseHeight();
	body->original_radius = ARXCHARACTER::baseRadius();
	body->physics.cyl.height = ARXCHARACTER::baseHeight();
	body->physics.cyl.radius = ARXCHARACTER::baseRadius();

	// The head/neck/chest/belt groups let the body look up and down the way the
	// player's does, so the other player's aim is readable.
	if(EERIE_OBJECT_GetGroup(body->obj, "head")
	   && EERIE_OBJECT_GetGroup(body->obj, "neck")
	   && EERIE_OBJECT_GetGroup(body->obj, "chest")
	   && EERIE_OBJECT_GetGroup(body->obj, "belt")) {
		body->_npcdata->ex_rotate = new EERIE_EXTRA_ROTATE();
		body->_npcdata->ex_rotate->group_number[0] = EERIE_OBJECT_GetGroup(body->obj, "head");
		body->_npcdata->ex_rotate->group_number[1] = EERIE_OBJECT_GetGroup(body->obj, "neck");
		body->_npcdata->ex_rotate->group_number[2] = EERIE_OBJECT_GetGroup(body->obj, "chest");
		body->_npcdata->ex_rotate->group_number[3] = EERIE_OBJECT_GetGroup(body->obj, "belt");
		body->_npcdata->ex_rotate->group_number[4] = EERIE_OBJECT_GetGroup(body->obj, "left_shoulder");
		body->_npcdata->ex_rotate->group_number[5] = EERIE_OBJECT_GetGroup(body->obj, "right_shoulder");
		for(Anglef & rotation : body->_npcdata->ex_rotate->group_rotate) {
			rotation = Anglef();
		}
	}

	shareAnimations(body);

	ARX_INTERACTIVE_RemoveGoreOnIO(body);

	g_avatarHandle = body->index();
	g_avatarWeapon.clear();
	g_bodyTrack.clear();

	LogInfo << "[coop] spawned a body for " << g_avatar.name;

	return body;
}

/*
 * What the body was last dressed in, so it is only rebuilt when it changes.
 *
 * And WHICH body it was, which matters more than it looks. Travelling to
 * another area tears the body down and builds a fresh one, undressed. Comparing
 * only the armour would see the same set as last time, decide there was nothing
 * to do, and leave the new body in its underwear - which is exactly what it did.
 */
std::string g_avatarHelmet, g_avatarArmour, g_avatarLeggings;
const Entity * g_dressedBody = nullptr;

/*
 * The shield is held rather than worn, so unlike armour it is a real entity
 * hanging off the body - and unlike the weapon, the body has no slot to keep it
 * in. So it is kept here, and this pointer is the only thing that knows it
 * exists: whatever deletes the body has to delete this first.
 */
std::string g_avatarShield;
Entity * g_avatarShieldEntity = nullptr;

/*!
 * Put the other player's armour on their body.
 *
 * Armour is not something a character holds - it is a change to the body
 * itself. ARX_EQUIPMENT_RecreatePlayerMesh does this for the player by throwing
 * the mesh away, loading a fresh one and applying each piece as a tweak: a
 * swapped mesh part and a repainted area of skin. The same has to happen here,
 * for the same reason, or the other player is seen in their underwear no matter
 * what they are wearing.
 *
 * Rebuilt only when the set actually changes, because it means reloading the
 * mesh - too expensive to do every frame and pointless when nothing has moved.
 */
/*!
 * Give the other player their own face.
 *
 * Choosing a face does not repaint your character - it overwrites the PIXELS
 * INSIDE a shared texture, and every model using that texture changes with it.
 * The engine says so itself, in a TODO above the four lines that do it. With
 * one hero on screen that was harmless; with two it means whoever picks last
 * decides what both of them look like.
 *
 * So the other player's head is bound to its own container instead, loaded
 * with NoInsert so it never enters the global list - which is where the
 * overwrite goes looking. Their face is then theirs no matter what this player
 * chooses, and picking a face here stops changing the person standing next to
 * you.
 *
 * Called again after every rebuild: putting armour on throws the whole mesh
 * away and loads a fresh one, and a fresh one comes with the shared textures.
 */
//! The face currently painted on, so it is only redone when it changes.
u8 g_paintedSkin = 0xff;

void paintAvatarFace(Entity * body, u8 skin) {

	if(!body || !body->obj) {
		return;
	}

	g_paintedSkin = skin;

	/*
	 * The same routine the local player uses on itself.
	 *
	 * Both bodies simply point their head material at the file for the face
	 * they were given, and nothing overwrites anything, so two heroes can stand
	 * in the same room wearing different faces - which the engine had never had
	 * to do before.
	 */
	ARX_PLAYER_ApplySkin(body->obj, skin);

}

void updateAvatarArmour(Entity * body) {

	if(!body) {
		return;
	}

	if(body == g_dressedBody
	   && g_avatarHelmet == g_avatar.helmet && g_avatarArmour == g_avatar.armour
	   && g_avatarLeggings == g_avatar.leggings) {
		return;
	}

	g_dressedBody = body;
	g_avatarHelmet = g_avatar.helmet;
	g_avatarArmour = g_avatar.armour;
	g_avatarLeggings = g_avatar.leggings;

	delete body->obj;
	body->obj = loadObject("graph/obj3d/interactive/npc/human_base/human_base.teo",
	                       false).release();
	if(!body->obj) {
		return;
	}

	/*
	 * Each piece is built, worn, and thrown away again. The tweak needs the
	 * item's own tweakerinfo - which mesh part, which skin - and that lives in
	 * the item, not in its name. Nothing is kept: the body wears the result,
	 * not the thing that caused it.
	 */
	struct Worn { const std::string & path; TweakType type; const char * selection; };
	const Worn worn[] = {
		{ g_avatar.helmet,   TWEAK_HEAD,  "head" },
		{ g_avatar.armour,   TWEAK_TORSO, "chest" },
		{ g_avatar.leggings, TWEAK_LEGS,  "leggings" }
	};

	for(const Worn & piece : worn) {
		if(piece.path.empty()) {
			continue;
		}
		if(Entity * item = AddItem(res::path::load(piece.path))) {

			/*
			 * Run its script before asking what it changes.
			 *
			 * Which mesh part a piece of armour swaps, and which skin it
			 * repaints, is not written in the item file - the item says it
			 * itself, by calling setplayertweak when it starts up. Skip that and
			 * tweakerinfo is still empty, the tweak below does nothing at all,
			 * and it does it without complaining.
			 */
			SendInitScriptEvent(item);

			if(!item->tweakerinfo) {
				LogWarning << "[coop] " << piece.path << " says nothing about how it is worn";
			}

			ARX_EQUIPMENT_ApplyTweak(body, item, piece.type, piece.selection);
			item->destroy();

		} else {
			LogWarning << "[coop] could not build the armour they are wearing: " << piece.path;
		}
	}

	// The mesh that was just loaded wears the shared hero textures again.
	paintAvatarFace(body, g_avatar.skin);

	EERIE_Object_Precompute_Fast_Access(body->obj);

	// The weapon and the shield hang off the mesh that was just replaced, so
	// they have to be hung again on the new one.
	g_avatarWeapon.clear();
	g_avatarShield.clear();

}

/*!
 * Hang the right shield off the body, or take it away when they put it down.
 *
 * The player's own shield is linked mesh to mesh - "shield_attach" on the arm
 * to "shield_attach" on the shield - rather than being drawn as part of the
 * body the way armour is. So this cannot go through the armour path: a shield
 * has no tweak to apply, and the body has no slot to keep it in the way it
 * keeps a weapon. It is a real entity, linked to the arm, and remembered here.
 */
void updateAvatarShield(Entity * body) {

	if(g_avatarShield == g_avatar.shield) {
		return;
	}

	g_avatarShield = g_avatar.shield;

	/*
	 * Unlink BEFORE deleting, or the body keeps drawing a shield that is gone.
	 *
	 * ~Entity detaches everything that calls IT owner, but it never removes
	 * itself from its OWNER's linked list - that only happens in setOwner(),
	 * which the destructor does not call. So a shield deleted while the body
	 * lives on leaves its EERIE_LINKED record behind, holding a raw pointer to
	 * a mesh that has just been freed, and the draw loop walks that list every
	 * frame. Put down one shield and pick up another and the body carries a
	 * dead record and a live one.
	 */
	if(g_avatarShieldEntity) {
		unlinkEntity(*g_avatarShieldEntity);
		delete g_avatarShieldEntity;
		g_avatarShieldEntity = nullptr;
	}

	if(g_avatarShield.empty()) {
		return;
	}

	if(Entity * worn = AddItem(res::path::load(g_avatarShield))) {
		SendInitScriptEvent(worn);
		worn->scriptload = 2;
		// The body never enters a savegame, so nothing hanging off it may either.
		worn->ioflags |= IO_NOSAVE;
		worn->show = SHOW_FLAG_LINKED;
		linkEntities(*body, "shield_attach", *worn, "shield_attach");
		g_avatarShieldEntity = worn;
	} else {
		LogWarning << "[coop] could not build the shield they are carrying: "
		           << g_avatarShield;
	}

}

//! Hang the right weapon off the body, or take it away when they sheathe it.
void updateAvatarWeapon(Entity * body) {

	if(g_avatarWeapon == g_avatar.weapon) {
		if(body->_npcdata->weapon) {
			// Same weapon, but they may have drawn or put it away since.
			if(g_avatar.combat) {
				SetWeapon_On(body);
			} else {
				SetWeapon_Back(body);
			}
		}
		return;
	}

	g_avatarWeapon = g_avatar.weapon;

	delete body->_npcdata->weapon;
	body->_npcdata->weapon = nullptr;
	body->_npcdata->weapontype = 0;

	if(g_avatarWeapon.empty()) {
		return;
	}

	/*
	 * Built here rather than through Prepare_SetWeapon, which takes a bare name
	 * and looks for it under the weapons folder and nowhere else. What the other
	 * player is holding may not be a weapon at all - it is whatever they put in
	 * the weapon slot - so the full path they sent is used as it stands. The
	 * rest of this mirrors what Prepare_SetWeapon does once it has the file.
	 */
	if(Entity * held = AddItem(res::path::load(g_avatarWeapon))) {
		body->_npcdata->weapon = held;
		SendInitScriptEvent(held);
		body->_npcdata->weapontype = held->type_flags;
		held->scriptload = 2;
		held->setOwner(body);
		SetWeapon_Back(body);
	} else {
		LogWarning << "[coop] could not build what the other player is holding: "
		           << g_avatarWeapon;
	}

	if(body->_npcdata->weapon) {
		// The body never enters a savegame, so nothing hanging off it may
		// either - a saved weapon would reference an owner that cannot be
		// loaded back.
		body->_npcdata->weapon->ioflags |= IO_NOSAVE;
	}

	if(body->_npcdata->weapon && g_avatar.combat) {
		SetWeapon_On(body);
	}

}

} // anonymous namespace

// ---------------------------------------------------------------------------

const Avatar & avatar() {
	return g_avatar;
}

void pushAvatarSample(u32 timeMs, const Vec3f & pos, const Anglef & angle) {
	
	if(g_bodyTrack.count
	   && glm::distance(g_bodyTrack.newest().pos, pos) > 400.f) {
		// A teleport or a level change, not walking: not motion to blend through.
		g_bodyTrack.clear();
	}
	
	g_bodyTrack.push(s64(timeMs), pos, angle);
	
}

Avatar & mutableAvatar() {
	return g_avatar;
}

Entity * avatarEntity() {
	return entities.get(g_avatarHandle);
}

bool isAvatarEntity(const Entity * entity) {
	return entity != nullptr && g_avatarHandle != EntityHandle()
	       && entity->index() == g_avatarHandle;
}

void resetAvatar() {
	g_avatarHandle = EntityHandle();
	g_avatarWeapon.clear();
	// Forgotten, never deleted: one caller is a level change, which has already
	// freed every entity in the area. Deleting here would be the second time.
	g_avatarShield.clear();
	g_avatarShieldEntity = nullptr;
	g_bodyTrack.clear();
}

/*!
 * Depth rather than a flag: applying one of the other player's actions can
 * trigger a script that triggers another, and the context has to survive the
 * whole chain, not just its first link.
 */
static int g_playerContextDepth = 0;

ScopedPlayerContext::ScopedPlayerContext(const Entity * cause)
	: m_active(cause != nullptr && isAvatarEntity(cause))
{
	if(m_active) {
		g_playerContextDepth++;
	}
}

ScopedPlayerContext::~ScopedPlayerContext() {
	if(m_active) {
		g_playerContextDepth--;
	}
}

float pathDistanceToPlayer(const Entity * io, const Vec3f & playerPos) {

	RoomHandle playerRoom = ARX_PORTALS_GetRoomNumForPosition(playerPos, RoomPositionForCamera);

	if(io->room && playerRoom) {
		return SP_GetRoomDist(io->pos, playerPos, io->room, playerRoom);
	}

	return fdist(io->pos, playerPos);
}

/*!
 * Who last hurt each creature, and when.
 *
 * Keyed by id string rather than handle: handles are reused the moment an
 * entity is freed, and a stale one would hand a creature's grudge to whatever
 * took its slot. Entries are never swept - they are only ever read through a
 * time window, so an old one is simply ignored.
 */
struct RecentAttacker {
	bool partner = false;
	PlatformInstant when = PlatformInstant(0);
};
std::map<std::string, RecentAttacker, std::less<>> g_recentAttackers;

/*
 * Long enough to survive the walk across a room and a few swings, short enough
 * that a creature is not still nursing a grudge from another fight entirely.
 */
constexpr PlatformDuration GrudgeWindow = 6s;

void noteAttacker(const Entity & victim, const Entity & attacker) {

	if(!isPlaying() || !(victim.ioflags & IO_NPC)) {
		return;
	}

	bool byPartner = isAvatarEntity(&attacker);
	if(!byPartner && attacker.index() != EntityHandle_Player) {
		return; // creature fighting creature; not ours to arbitrate
	}

	g_recentAttackers[victim.idString()] = { byPartner, platform::getTime() };

}

Entity * chooseTargetPlayer(const Entity * io, const Entity * current) {

	Entity * first = entities.player();
	Entity * other = avatarEntity();

	if(!other || !io) {
		return first;
	}

	// Never chase a corpse while the other player is alive.
	if(avatar().dead) {
		return first;
	}
	if(player.lifePool.current <= 0.f) {
		return other;
	}

	/*
	 * A script chain caused by the other player resolves "player" to THEM.
	 * An ON OUCH's "settarget player" means "attack whoever hurt me": in the
	 * original game the word and the attacker were the same being, so sight
	 * and distance never got a vote. Without this, a creature struck by the
	 * second player would turn on whoever happened to stand closer.
	 */
	if(scriptContextPlayer()) {
		return other;
	}

	/*
	 * Whoever hit it last, before any question of eyes or distance.
	 *
	 * A creature turns on what is hurting it. That is true of the game with one
	 * player and was the one thing missing with two: the second player could
	 * stand behind a goblin and take it apart while it walked at the first
	 * player, who had not moved, because the only vote being counted was who
	 * was nearer.
	 */
	if(auto it = g_recentAttackers.find(io->idString()); it != g_recentAttackers.end()) {
		if(platform::getTime() - it->second.when < GrudgeWindow) {
			return it->second.partner ? other : first;
		}
	}

	bool seesFirst = false;
	bool seesOther = false;
	if((io->ioflags & IO_NPC) && io->_npcdata) {
		seesFirst = io->_npcdata->m_seenPlayer;
		seesOther = io->_npcdata->m_seenPartner;
	}

	float toFirst = pathDistanceToPlayer(io, player.pos);
	float toOther = pathDistanceToPlayer(io, other->pos + ARXCHARACTER::baseOffset());

	/*
	 * Awareness beats distance - but awareness is more than eyes. The sight
	 * flag honours the creature's field of view, so the moment it turns to
	 * walk, a player directly behind it "vanishes". Preferring the player it
	 * can see then walks it away from one standing at arm's length, and every
	 * step keeps the near player behind it, so the mistake locks itself in:
	 * the log showed a goblin leaving a player eighty units away to march at
	 * one three hundred away down a corridor. Anyone close enough to touch is
	 * perceived, eyes or no eyes.
	 */
	const float CloseRange = 300.f;
	bool awareOfFirst = seesFirst || toFirst < CloseRange;
	bool awareOfOther = seesOther || toOther < CloseRange;
	if(awareOfFirst != awareOfOther) {
		return awareOfFirst ? first : other;
	}

	// Hysteresis: the incumbent keeps the aggro unless the challenger is
	// clearly closer, so the creature is not torn between two players standing
	// next to each other.
	const float margin = 150.f;

	if(current == other) {
		return (toFirst + margin < toOther) ? first : other;
	}
	if(current == first) {
		return (toOther + margin < toFirst) ? other : first;
	}

	return (toOther < toFirst) ? other : first;
}

// cause-less scene adoption state (see CoopPlayer.h for the story)
static bool g_adoptedSceneOwner = false;

Entity * scriptContextPlayer() {
	return (g_playerContextDepth > 0 || g_adoptedSceneOwner) ? avatarEntity() : nullptr;
}

bool isPartnerScriptContext() {
	return (g_playerContextDepth > 0 || g_adoptedSceneOwner) && avatarEntity() != nullptr;
}


void adoptProximitySceneOwner(const Entity * grabber) {

	if(!isPlaying() || isApplyingRemote() || g_adoptedSceneOwner) {
		return;
	}
	if(g_playerContextDepth > 0) {
		return; // the run has a real cause; ownership is already decided
	}
	if(config.misc.cutscenes != CutscenesForTrigger) {
		return; // the slider says scenes belong to a fixed player
	}
	Entity * partner = avatarEntity();
	if(!grabber || !partner || !sharingArea()) {
		return;
	}

	/*
	 * An arrival is nobody else's scene.
	 *
	 * Walking into a new area runs its arrival script, which places "the
	 * player" on its marker - and for those few seconds the other player is
	 * usually standing closer to that marker than we are, having arrived
	 * first. Read as "the nearer player owns it", our own arrival placement
	 * was posted to their machine and we were left standing wherever the fall
	 * had dropped us, outside the world.
	 */
	if(localPlayerArrivalProtected()) {
		return;
	}

	/*
	 * Whose scene is a scene nobody caused?
	 *
	 * Some scenes are not walked into, talked into or handed anything: an NPC
	 * simply finishes walking to a marker and starts speaking. There is no
	 * event to read an owner from - so the honest answer is the one the
	 * feature promised all along, the player who is actually standing there.
	 * A scene grabbing "the player" means the one it can see.
	 */
	Vec3f partnerPos = partner->pos + ARXCHARACTER::baseOffset();
	if(arx::distance2(grabber->pos, partnerPos) >= arx::distance2(grabber->pos, player.pos)) {
		return; // our own player is the nearer one; the scene is ours as ever
	}

	g_adoptedSceneOwner = true;
	LogWarning << "[coop-scene] scene adopted: '" << grabber->idString()
	           << "' grabbed the player, and the nearer player is the partner"
	           << " - the scene is theirs";
}

void clearAdoptedSceneOwner() {
	g_adoptedSceneOwner = false;
}

bool isAdoptedSceneOwner() {
	return g_adoptedSceneOwner;
}

void destroyAvatarEntity() {

	if(Entity * body = avatarEntity()) {
		// Both come off before the body does: deleting the body deletes the mesh
		// they are linked to, and unlinking afterwards would read it back.
		if(g_avatarShieldEntity) {
			unlinkEntity(*g_avatarShieldEntity);
			delete g_avatarShieldEntity;
			g_avatarShieldEntity = nullptr;
		}
		delete body->_npcdata->weapon;
		body->_npcdata->weapon = nullptr;
		delete body;
	}

	// Nothing is dressed any more, so nothing is remembered as dressed.
	g_dressedBody = nullptr;
	g_avatarHelmet.clear();
	g_avatarArmour.clear();
	g_avatarLeggings.clear();

	resetAvatar();

}

void captureLocalAvatar(Avatar & out) {

	Entity * self = entities.player();

	out.valid = true;
	out.area = g_currentArea;
	out.pos = player.pos;
	out.angle = player.angle;

	out.life = player.lifePool.current;
	out.maxLife = std::max(1.f, player.lifePool.max);
	out.mana = player.manaPool.current;
	out.maxMana = std::max(1.f, player.manaPool.max);
	out.level = player.level;

	out.dead = (player.lifePool.current <= 0.f);
	out.combat = (player.Interface & INTER_COMBATMODE) != 0;
	out.skin = player.skin;

	if(self) {
		out.anim0 = findAnimIndex(self, self->animlayer[0].cur_anim);

		/*
		 * Standing still, the engine picks the idle by which camera WE are
		 * using: third person gets ANIM_WAIT, first person ANIM_WAIT_SHORT.
		 * The names are the opposite of what they read like - player.asl binds
		 * ANIM_WAIT to player_wait_short and ANIM_WAIT_SHORT to player_wait_1st
		 * - so the second one is the first person idle, animated for the arms
		 * you see from inside your own head and for nothing else.
		 *
		 * The other player is looking at the whole body from outside, so they
		 * are told the third person idle instead. What we look like to them
		 * cannot depend on which camera we happen to be looking through.
		 */
		if(out.anim0 == u8(ANIM_WAIT_SHORT) && self->anims[ANIM_WAIT]) {
			out.anim0 = u8(ANIM_WAIT);
		}
		out.anim1 = findAnimIndex(self, self->animlayer[1].cur_anim);
		out.anim3 = findAnimIndex(self, self->animlayer[3].cur_anim);
		out.anim3Flags = u16(self->animlayer[3].flags);
		out.anim3Time = s32(toMsi(self->animlayer[3].ctime));
		out.anim0Flags = u16(self->animlayer[0].flags);
		out.anim1Flags = u16(self->animlayer[1].flags);
		out.anim0Time = s32(toMsi(self->animlayer[0].ctime));
		out.anim1Time = s32(toMsi(self->animlayer[1].ctime));
		out.invisibility = self->invisibility;
	}

	out.weapon.clear();
	if(Entity * weapon = entities.get(player.equiped[EQUIP_SLOT_WEAPON])) {
		/*
		 * Only the folder name travels, and that is deliberate: the other side
		 * hands it to Prepare_SetWeapon, which builds
		 * "graph/obj3d/interactive/items/weapons" / name / name itself. Sending
		 * the whole class path makes it prepend that directory to a path that
		 * already has it, and nothing is found.
		 */
		/*
		 * The whole class path, because a player will wield anything.
		 *
		 * Only sending the folder name means the other side has to guess where
		 * to look for it, and the only place it can guess is the weapons
		 * folder. That is fine for a sword and wrong for everything else: a
		 * bone is filed under provisions - it is food - and Arx is perfectly
		 * happy to let someone hit a goblin with it. Sending where the thing
		 * actually lives works whatever it is.
		 */
		out.weapon = weapon->classPath().string();
	}

	/*
	 * And what they are wearing. Same reasoning as the weapon: the class is
	 * enough, because the other machine builds its own copy from it.
	 */
	out.helmet.clear();
	out.armour.clear();
	out.leggings.clear();
	out.shield.clear();
	if(Entity * worn = entities.get(player.equiped[EQUIP_SLOT_HELMET])) {
		out.helmet = worn->classPath().string();
	}
	if(Entity * worn = entities.get(player.equiped[EQUIP_SLOT_ARMOR])) {
		out.armour = worn->classPath().string();
	}
	if(Entity * worn = entities.get(player.equiped[EQUIP_SLOT_LEGGINGS])) {
		out.leggings = worn->classPath().string();
	}
	if(Entity * worn = entities.get(player.equiped[EQUIP_SLOT_SHIELD])) {
		out.shield = worn->classPath().string();
	}


}

void updateAvatar() {

	if(!isPlaying() || !g_avatar.valid) {
		destroyAvatarEntity();
		return;
	}

	if(!g_avatar.present || !entities.player()) {
		// They are somewhere else in the fortress. Nothing to draw here.
		destroyAvatarEntity();
		return;
	}

	Entity * body = avatarEntity();
	if(!body) {
		body = createAvatarEntity();
		if(!body) {
			return;
		}
	}

	// Draw the body a step in the past of the other machine's clock, standing
	// between the two reported positions around that moment.
	Vec3f reportedPos = g_avatar.pos;
	Anglef reportedAngle = g_avatar.angle;
	{
		s64 renderTime = estimatedRemoteNowMs() - bodyInterpDelayMs();
		Vec3f smoothedPos;
		Anglef smoothedAngle;
		if(sampleSmoothed(g_bodyTrack, renderTime, g_framedelay, smoothedPos, smoothedAngle)) {
			reportedPos = smoothedPos;
			reportedAngle = smoothedAngle;
		}
	}

	// The body stands on the ground, whereas the position that travels is the
	// one the engine keeps for the player, which sits a head-height above it.
	Vec3f drawPos = Vec3f(reportedPos.x,
	                      reportedPos.y - ARXCHARACTER::baseHeight(),
	                      reportedPos.z);

	body->pos = drawPos;
	body->lastpos = drawPos;
	body->physics.cyl.origin = drawPos;
	body->_npcdata->vvpos = drawPos.y;

	/*
	 * Store the facing the player is actually looking, not the mirrored one.
	 *
	 * The two bodies reach the renderer by different routes. The player's own
	 * is handed to EERIEDrawAnimQuatUpdate() directly, so ARX_PLAYER_Manage_Visual
	 * pre-mirrors it into the entity angle. This one goes through UpdateInter(),
	 * which mirrors every NPC angle itself on the way past. Pre-mirroring here
	 * as well would cancel out and leave the other player permanently facing
	 * the opposite way from where they are looking.
	 */
	body->angle = Anglef(0.f, MAKEANGLE(reportedAngle.getYaw()), 0.f);

	if(body->_npcdata->ex_rotate) {
		float pitch = reportedAngle.getPitch();
		if(pitch > 160.f) {
			pitch = -(360.f - pitch);
		}
		EERIE_EXTRA_ROTATE * rotation = body->_npcdata->ex_rotate;
		rotation->group_rotate[0] = Anglef(pitch * 0.25f, 0.f, 0.f);
		rotation->group_rotate[1] = Anglef(pitch * 0.25f, 0.f, 0.f);
		rotation->group_rotate[2] = Anglef(pitch * 0.25f, 0.f, 0.f);
		rotation->group_rotate[3] = Anglef(pitch * 0.25f, 0.f, 0.f);
		rotation->group_rotate[4] = Anglef();
		rotation->group_rotate[5] = Anglef();
	}

	body->_npcdata->lifePool.max = std::max(1.f, g_avatar.maxLife);
	body->_npcdata->lifePool.current = g_avatar.life;
	body->_npcdata->manaPool.max = std::max(1.f, g_avatar.maxMana);
	body->_npcdata->manaPool.current = g_avatar.mana;
	body->invisibility = g_avatar.invisibility;

	// Always simulated and always drawn: the other player is the one thing on
	// screen that must never be culled away as background clutter.
	body->gameFlags |= GFLAG_ISINTREATZONE;

	/*
	 * Make the body audible. The engine spawns hearing events from the local
	 * player's own step logic, which never runs for this body - so without
	 * this, the other player walks in perfect silence past every creature in
	 * the game. Same cadence as the player's own steps: one audible event per
	 * step-length walked.
	 */
	{
		static Vec3f lastNoisePos = Vec3f(0.f);
		static float walked = 0.f;
		constexpr float StepDistance = 120.f;
		if(lastNoisePos == Vec3f(0.f)) {
			lastNoisePos = body->pos;
		}
		walked += glm::distance(getXZ(lastNoisePos), getXZ(body->pos));
		lastNoisePos = body->pos;
		if(walked >= StepDistance) {
			walked = 0.f;
			spawnAudibleSound(body->pos, *body);
		}
	}

	applyAnimLayer(body, 0, g_avatar.anim0, g_avatar.anim0Flags, g_avatar.anim0Time);
	applyAnimLayer(body, 1, g_avatar.anim1, g_avatar.anim1Flags, g_avatar.anim1Time);
	applyAnimLayer(body, 3, g_avatar.anim3, g_avatar.anim3Flags, g_avatar.anim3Time);

	/*
	 * Authoritative separation. Creatures normally stop at the body's edge, but
	 * a mid-swing position snap or a spawn can leave one overlapping it - and
	 * the engine's push-apart never runs for this body, because the AI loop
	 * skips it. The authority resolves the overlap itself: any living creature
	 * inside the body's cylinder is eased out, a few units per frame, until the
	 * two stand apart the way the movement rules would normally guarantee.
	 */
	if(hasWorldAuthority()) {
		const float bodyRadius = ARXCHARACTER::baseRadius();
		for(Entity & npc : entities.inScene(IO_NPC)) {
			if(&npc == body || npc.index() == EntityHandle_Player) {
				continue;
			}
			if(!npc._npcdata || npc._npcdata->lifePool.current <= 0.f) {
				continue;
			}
			if(glm::abs(npc.pos.y - body->pos.y) > 250.f) {
				continue;
			}
			Vec3f flat = npc.pos - body->pos;
			flat.y = 0.f;
			float dist = glm::length(flat);
			// The engine's own radius, so a creature standing in legitimate
			// cylinder contact - its fighting distance - gets zero push. Any
			// wider ring holds attackers just outside their own arrival
			// tolerance and they shove at the gap forever.
			float npcRadius = getEntityRadius(npc);
			float overlap = bodyRadius + npcRadius - dist;
			if(overlap <= 2.f) {
				continue;
			}
			// The creature takes ALL of the separation, as it would against the
			// local player. The real other player is never pushed: this body
			// stands where they were a network-delay ago, and a chasing creature
			// pressing on that stale spot would turn a shared push into a
			// thruster that shoves them across the room.
			Vec3f away = (dist > 0.001f) ? flat / dist : Vec3f(1.f, 0.f, 0.f);
			npc.forcedmove += away * std::min(overlap, 20.f);
		}
	}

	// Armour first: it rebuilds the mesh the weapon and shield hang off.
	updateAvatarArmour(body);

	/*
	 * Their face can change after the body exists - most obviously when a
	 * joining player is still choosing one while standing here. Painted only
	 * at build time, the body kept whatever they had when they arrived,
	 * which is the default nobody picked.
	 */
	if(g_avatar.skin != g_paintedSkin) {
		paintAvatarFace(body, g_avatar.skin);
	}
	updateAvatarWeapon(body);
	updateAvatarShield(body);

	/*
	 * A guest that has just arrived at the host's area lands wherever its own
	 * savegame left it, which is usually nowhere near them. Put it next to
	 * them, once, so joining ends with the two players looking at each other.
	 */
	if(takeJoinNudge()) {

		/*
		 * Put the joining player next to the one already here.
		 *
		 * The hard part is not choosing a nice spot, it is refusing a bad one.
		 * Simply measuring out a step behind them lands inside whatever happens
		 * to be standing there - a chair, a table, a wall - and a player who
		 * spawns inside geometry cannot walk out of it, because the collision
		 * test refuses every direction at once.
		 *
		 * ARX_INTERACTIVE_ConvertToValidPosForIO is the engine's own answer to
		 * this. It spirals outward from a point until the player's cylinder
		 * fits with floor beneath it and ceiling above it, which rules out both
		 * being buried in furniture and being dropped outside the level.
		 *
		 * Positions here are in the cylinder's own terms - where the feet go -
		 * which is what that helper expects and what basePosition() means.
		 */
		const Vec3f hostFeet(g_avatar.pos.x,
		                     g_avatar.pos.y - ARXCHARACTER::baseHeight(),
		                     g_avatar.pos.z);

		/*
		 * No computed positions, ever. Every underground, void and rooftop
		 * spawn this project has seen came from asking the geometry to
		 * suggest a spot; the geometry lies. The one position proven valid
		 * is the one the other player is standing on RIGHT NOW - so that is
		 * exactly where we appear. The bodies pass through each other by
		 * design; one shared heartbeat, then a step apart.
		 */
		Vec3f feet = hostFeet;

		// A summon does not come through here at all any more: it lands at
		// its own spot during the level change, so there is nothing to nudge.

		LogWarning << "[coop-place] nudge: avatar=" << g_avatar.pos.x << ',' << g_avatar.pos.y
		           << ',' << g_avatar.pos.z << " feet=" << feet.x << ',' << feet.y << ',' << feet.z
		           << " was=" << player.pos.x << ',' << player.pos.y << ',' << player.pos.z;

		player.pos = g_moveto = feet + ARXCHARACTER::baseOffset();
		player.desiredangle = player.angle = Anglef(0.f, MAKEANGLE(g_avatar.angle.getYaw() + 180.f), 0.f);
		entities.player()->pos = player.basePosition();
		ARX_PLAYER_Reset_Fall();

		LogInfo << "[coop] placed the joining player beside " << g_avatar.name;

	}

}

fs::path guestProfileFile() {
	return fs::getUserDir() / ("coop-profile-" + playthroughId() + ".bin");
}

//! Everything of the local player's that is carried or worn.
static void collectBelongings(std::vector<Entity *> & out, std::vector<s8> * slots) {

	for(Entity & e : entities) {
		if(&e == entities.player()) {
			continue;
		}
		s8 slot = -1;
		for(size_t i = 0; i < player.equiped.size(); i++) {
			if(player.equiped[i] != EntityHandle() && player.equiped[i] == e.index()) {
				slot = s8(i);
				break;
			}
		}
		bool carried = false;
		if(slot < 0) {
			InventoryPos ip = locateInInventories(&e);
			carried = ip && ip.container == entities.player();
		}
		if(slot >= 0 || carried) {
			out.push_back(&e);
			if(slots) {
				slots->push_back(slot);
			}
		}
	}

}

void saveGuestProfileIfDue(bool force) {

	static PlatformInstant lastSave = 0;

	if(!isGuest() || !isPlaying() || playthroughId().empty()
	   || !entities.player() || !g_currentArea || player.lifePool.max <= 0.f) {
		return;
	}

	PlatformInstant now = platform::getTime();
	if(!force && lastSave != PlatformInstant(0) && now - lastSave < 60000ms) {
		return;
	}
	lastSave = now;

	std::FILE * f = std::fopen(guestProfileFile().string().c_str(), "wb");
	if(!f) {
		return;
	}

	// "ACP2" - the same profile as ACP1 with the chosen face on the end
	u32 magic = 0x32504341u;
	std::fwrite(&magic, sizeof(magic), 1, f);
	/*
	 * What they look like.
	 *
	 * Everything else about this character was kept and this was not, so a
	 * player who made a face at their first join met a default one at the
	 * next - which reads as the game forgetting them.
	 */
	u8 skin = player.skin;
	std::fwrite(&skin, sizeof(skin), 1, f);
	s32 level = player.level;
	std::fwrite(&level, sizeof(level), 1, f);
	s64 xp = player.xp;
	std::fwrite(&xp, sizeof(xp), 1, f);
	s64 gold = player.gold;
	std::fwrite(&gold, sizeof(gold), 1, f);
	std::fwrite(&player.lifePool, sizeof(player.lifePool), 1, f);
	std::fwrite(&player.manaPool, sizeof(player.manaPool), 1, f);
	std::fwrite(&player.m_attribute, sizeof(player.m_attribute), 1, f);
	std::fwrite(&player.m_skill, sizeof(player.m_skill), 1, f);
	std::fwrite(&player.Attribute_Redistribute, sizeof(player.Attribute_Redistribute), 1, f);
	std::fwrite(&player.Skill_Redistribute, sizeof(player.Skill_Redistribute), 1, f);
	std::fwrite(&player.rune_flags, sizeof(player.rune_flags), 1, f);
	std::fwrite(&player.hunger, sizeof(player.hunger), 1, f);

	std::vector<Entity *> belongings;
	std::vector<s8> slots;
	collectBelongings(belongings, &slots);

	u16 count = u16(std::min<size_t>(belongings.size(), 0xffff));
	std::fwrite(&count, sizeof(count), 1, f);
	for(size_t i = 0; i < count; i++) {
		std::string cls = belongings[i]->classPath().string();
		u16 len = u16(std::min<size_t>(cls.size(), 0xffff));
		std::fwrite(&len, sizeof(len), 1, f);
		std::fwrite(cls.data(), 1, len, f);
		std::fwrite(&slots[i], sizeof(s8), 1, f);
	}

	std::fclose(f);
	LogInfo << "[coop] saved this playthrough's character (" << count << " items)";

}

//! Where the guest's private id range begins; levels and hosts stay far below.
static const s32 GuestItemInstanceBase = 20000;

static int g_guestItemScope = 0;

ScopedGuestItems::ScopedGuestItems() {
	g_guestItemScope++;
}

ScopedGuestItems::~ScopedGuestItems() {
	g_guestItemScope--;
}

s32 firstFreeInstanceHint() {

	if(g_guestItemScope > 0) {
		return GuestItemInstanceBase;
	}

	/*
	 * Everything a guest mints on its own goes in the private range - not
	 * only its saved belongings. Scripts run on both machines, and plenty of
	 * them create entities: a skeleton's script rolls a die and puts a bone
	 * in its pack, a chest fills itself, a spell leaves a residue. Each
	 * machine names its own copy from its own list of free numbers, and
	 * those lists drifted apart the moment the guest stripped the character
	 * it was cloned from. That is how the second player ended up holding a
	 * bone named exactly like the first player's bone - two different
	 * objects, one name - so when the first player's weapon broke, the
	 * world's "that one is gone" killed the weapon in the other's hands.
	 *
	 * Anything the host tells us to create carries the host's own id and
	 * arrives through applyRemote, which is why it is excluded here.
	 */
	if(isGuest() && !isApplyingRemote()) {
		return GuestItemInstanceBase;
	}

	return 1;

}

//! Set on the first join of a playthrough; consumed by the main loop.
static bool g_askWhoTheyAre = false;

//! Set while that player is actually on the character sheet.
static bool g_onCharacterSheet = false;

bool isJoiningCharacterCreation() {
	return g_onCharacterSheet;
}

void setJoiningCharacterCreation(bool active) {
	g_onCharacterSheet = active;
}

bool takeCharacterCreationRequest() {
	bool ask = g_askWhoTheyAre;
	g_askWhoTheyAre = false;
	return ask;
}

void applyGuestIdentity() {

	if(!isGuest() || !entities.player()) {
		return;
	}

	/*
	 * Strip the host-clone: unequip, then remove everything carried.
	 *
	 * Silently. Joining means loading the host's savegame, so for a moment this
	 * player is an exact copy of them - wearing their armour, holding their
	 * weapon, carrying their pack, and every one of those items has the host's
	 * own id on it. Throwing the copies away is housekeeping that belongs to
	 * this machine alone.
	 *
	 * Announced, it is a disaster: the host hears that its armour has been
	 * destroyed, believes it, and destroys the real one. The player who was
	 * wearing it watches it vanish off their body the moment somebody joins.
	 */
	ApplyScope silent;

	ARX_EQUIPMENT_UnEquipAllPlayer();
	if(player.torch) {
		ARX_PLAYER_KillTorch();
	}
	/*
	 * And take whatever ended up in their hand.
	 *
	 * Unequipping does not always put a thing away. With nowhere to file it -
	 * and this player's pack is about to be emptied anyway - the engine leaves
	 * it on the cursor instead. An item being dragged is neither worn nor in an
	 * inventory, so collectBelongings below never sees it, and it is the one
	 * piece of the host's kit that survives the purge: the guest spawns holding
	 * a copy of the host's weapon, unable to equip it and unable to put it down
	 * properly, because it was never really theirs.
	 */
	if(g_draggedEntity) {
		Entity * held = g_draggedEntity;
		setDraggedEntity(nullptr);
		held->destroy();
	}

	std::vector<Entity *> doomed;
	collectBelongings(doomed, nullptr);
	for(Entity * e : doomed) {
		e->destroy();
	}

	/*
	 * And build the body again, now that it is carrying nothing.
	 *
	 * Emptying the slots is not enough. What a player looks like is baked into
	 * their mesh - the weapon is an object linked to a hand, the armour is a
	 * swapped mesh part and a repainted skin - and none of that comes off just
	 * because the slot behind it was cleared. Without this the guest spawns
	 * holding the host's weapon and wearing the host's armour while owning
	 * neither: the clone is gone but its reflection is still standing there.
	 */
	ARX_EQUIPMENT_RecreatePlayerMesh();


	// Everything this player owns from here on is theirs alone, named in a
	// range the host's world never uses.
	ScopedGuestItems ownBelongings;

	std::FILE * f = playthroughId().empty() ? nullptr
	                : std::fopen(guestProfileFile().string().c_str(), "rb");
	if(!f) {
		ARX_PLAYER_MakeFreshHero();
		/*
		 * First join, so there is nobody to restore - which makes this the
		 * one moment worth asking who they want to be. Every later join finds
		 * a profile and skips this, so the character they make here is the
		 * one they keep.
		 */
		g_askWhoTheyAre = true;
		LogInfo << "[coop] first join of this playthrough: asking who they are";
		return;
	}

	u32 magic = 0;
	std::fread(&magic, sizeof(magic), 1, f);
	if(magic != 0x31504341u && magic != 0x32504341u) {
		std::fclose(f);
		ARX_PLAYER_MakeFreshHero();
		LogWarning << "[coop] unreadable character profile; starting fresh";
		return;
	}

	// ACP2 carries the face they chose; ACP1 predates it and keeps this one
	bool hasFace = (magic == 0x32504341u);
	u8 skin = player.skin;
	if(hasFace) {
		std::fread(&skin, sizeof(skin), 1, f);
	}

	s32 level = 0;
	s64 xp = 0, gold = 0;
	std::fread(&level, sizeof(level), 1, f);
	std::fread(&xp, sizeof(xp), 1, f);
	std::fread(&gold, sizeof(gold), 1, f);
	std::fread(&player.lifePool, sizeof(player.lifePool), 1, f);
	std::fread(&player.manaPool, sizeof(player.manaPool), 1, f);
	std::fread(&player.m_attribute, sizeof(player.m_attribute), 1, f);
	std::fread(&player.m_skill, sizeof(player.m_skill), 1, f);
	std::fread(&player.Attribute_Redistribute, sizeof(player.Attribute_Redistribute), 1, f);
	std::fread(&player.Skill_Redistribute, sizeof(player.Skill_Redistribute), 1, f);
	std::fread(&player.rune_flags, sizeof(player.rune_flags), 1, f);
	std::fread(&player.hunger, sizeof(player.hunger), 1, f);
	player.level = short(level);
	player.xp = long(xp);
	player.gold = long(gold);

	/*
	 * And the face. Setting the number is not enough - the head textures are
	 * chosen from it when a body is built, so the body has to be told to look
	 * again or the player keeps whichever face they were handed.
	 */
	if(hasFace) {
		player.skin = skin;
		ARX_PLAYER_Restore_Skin();
	}

	u16 count = 0;
	std::fread(&count, sizeof(count), 1, f);
	u16 restored = 0;
	for(u16 i = 0; i < count; i++) {
		u16 len = 0;
		if(std::fread(&len, sizeof(len), 1, f) != 1 || len == 0 || len > 512) {
			break;
		}
		std::string cls(len, '\0');
		if(std::fread(cls.data(), 1, len, f) != len) {
			break;
		}
		s8 slot = -1;
		std::fread(&slot, sizeof(slot), 1, f);

		Entity * item = AddItem(res::path::load(cls));
		if(!item) {
			continue;
		}
		item->scriptload = 1;
		SendInitScriptEvent(item);
		giveToPlayer(item);
		if(slot >= 0) {
			ARX_EQUIPMENT_Equip(entities.player(), item);
		}
		restored++;
	}
	std::fclose(f);

	ARX_PLAYER_ComputePlayerFullStats();
	LogInfo << "[coop] this playthrough's character restored (" << restored << " items)";

}

void logOwnBelongings(const char * when) {

	std::string worn;
	for(size_t i = 0; i < player.equiped.size(); i++) {
		if(Entity * item = entities.get(player.equiped[i])) {
			worn += ' ' + item->idString() + "(slot" + std::to_string(i) + ')';
		}
	}

	std::string pack;
	std::vector<Entity *> belongings;
	collectBelongings(belongings, nullptr);
	for(Entity * item : belongings) {
		pack += ' ' + item->idString();
	}

	LogWarning << "[coop-item] " << when << " - worn:" << (worn.empty() ? " none" : worn)
	           << " | carried:" << (pack.empty() ? " none" : pack);

}

//! How long the other player has stood over the body, building up a rescue.

/*!
 * Back on our feet with half our health - alive, hurt, still in the fight.
 *
 * This undoes exactly what ARX_PLAYER_BecomesDead() and the death camera
 * set. Two things ask for it now, standing over the body and a spell cast
 * from across the room, and they must agree about what being revived
 * means - so they share this rather than each doing their own version.
 */
void reviveLocalPlayer(const std::string & why) {
	
	if(player.lifePool.current > 0.f) {
		return;                       // not dead; nothing to undo
	}
	
	player.lifePool.current = 0.5f * player.lifePool.max;
	player.manaPool.current = std::max(player.manaPool.current,
	                                   0.25f * player.manaPool.max);
	player.DeadTime = 0;
	BLOCK_PLAYER_CONTROLS = false;
	EXTERNALVIEW = false;
	player.Interface = INTER_LIFE_MANA | INTER_MINIBACK | INTER_MINIBOOK;
	reportRevive();
	notification_add(std::string(why));
	
}

/*!
 * Whether being dead might still be temporary.
 *
 * It used to raise you as well: two seconds of the other player standing over
 * the body and you were up. That was the only way back co-op had, and it is
 * not any more - a spell does it now, from wherever they are standing - so the
 * standing-over-you part is gone and only the waiting is left.
 *
 * The waiting matters. While the other player is alive the death fade is held
 * short of the menu; without it a dead player is dropped back to the main menu
 * long before anyone can finish drawing three runes.
 */
bool updateReviveOpportunity() {

	if(!isPlaying() || !g_avatar.valid || g_avatar.dead) {
		return false; // no one left standing, so death is allowed to be the end
	}

	return true;
}

void drawPartnerHud() {

	if(!isPlaying() || !g_avatar.valid) {
		return;
	}

	(void) 0; // the orb is drawn by drawPartnerHealthOrb, inside the HUD pass

}

void drawPartnerHealthOrb(const Rectf & mine) {

	if(!isPlaying() || !g_avatar.valid) {
		return;
	}

	if(mine.width() <= 0.f || mine.height() <= 0.f) {
		return;
	}

	const float scale = minSizeRatio();
	const float gap = 6.f * scale;
	Rectf theirs = mine;
	theirs.move(0.f, -(mine.height() + gap));

	static TextureContainer * emptyTex = nullptr;
	static TextureContainer * filledTex = nullptr;
	if(!emptyTex) {
		emptyTex = TextureContainer::LoadUI("graph/interface/bars/empty_gauge_red");
		filledTex = TextureContainer::LoadUI("graph/interface/bars/filled_gauge_red");
	}
	if(!emptyTex || !filledTex) {
		return;
	}

	float fraction = glm::clamp(g_avatar.life / std::max(1.f, g_avatar.maxLife), 0.f, 1.f);

	/*
	 * Dimmed while they are somewhere else in the fortress. Their health is
	 * still true - it keeps arriving - but a full-strength orb next to yours
	 * reads as "standing beside you", and they are not.
	 */
	Color tint = g_avatar.present ? Color::red : Color::rgb(0.45f, 0.12f, 0.12f);
	Color frame = g_avatar.present ? Color::white : Color::gray(0.6f);

	EERIEDrawBitmap2DecalY(theirs, 0.f, filledTex, tint, 1.f - fraction);
	EERIEDrawBitmap(theirs, 0.001f, emptyTex, frame);

	// A small mark for the state a drained orb cannot express by itself.
	if(hFontInGame && (g_avatar.dead || !g_avatar.present)) {
		UNICODE_ARXDrawTextCenter(hFontInGame,
		                          Vec2f(theirs.center().x, theirs.top - 14.f * scale),
		                          g_avatar.dead ? "down" : "away", Color::gray(0.7f));
	}

}

void applyRemoteDamage(float damage, u32 damageType) {

	if(damage <= 0.f) {
		return;
	}

	DamageType type = DamageType::load(damageType);

	/*
	 * The attacker sent the raw blow, because only this machine knows what this
	 * character is wearing and how well they were taught to take a hit. Their
	 * defence is applied here, once, for the same reason: doing it on their
	 * side would have used the wrong character sheet.
	 */
	float absorbed = damage - damage * player.m_skillFull.defense * 0.5f * 0.01f;
	absorbed = std::max(0.f, absorbed);

	if(Entity * source = avatarEntity()) {
		// Knock the victim away from whoever hit them, as a normal blow does.
		Vec3f away = player.pos + player.baseOffset() - source->pos;
		if(away != Vec3f(0.f)) {
			away = glm::normalize(away);
			PUSH_PLAYER_FORCE += away * absorbed * Vec3f(1.f / 11, 1.f / 30, 1.f / 11);
		}
	}

	damagePlayer(absorbed, type, avatarEntity());
	ARX_DAMAGES_DamagePlayerEquipment(absorbed);

}

} // namespace coop

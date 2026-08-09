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

#ifndef ARX_NET_COOPPLAYER_H
#define ARX_NET_COOPPLAYER_H

#include <string>

#include "io/fs/FilePath.h"

#include "game/GameTypes.h"
#include "graphics/BaseGraphicsTypes.h"
#include "math/Angle.h"
#include "math/Vector.h"
#include "platform/Platform.h"

class Entity;

namespace coop {

/*!
 * The other player, as this machine knows them.
 *
 * Note what is not here: no attributes, no skills, no inventory, no experience
 * table. The other player's character lives on the other player's machine and
 * is never second-guessed from this side. All that travels is what this machine
 * genuinely needs - where the body is, what it is doing, and how hurt it is,
 * so it can be drawn, shown on the map, and hit.
 */
struct Avatar {

	bool valid = false;      //!< a body state has been received at least once
	bool present = false;    //!< ...and they are in the same area as us

	std::string name = "Player";

	AreaId area;
	Vec3f pos = Vec3f(0.f);
	Anglef angle = Anglef();

	float life = 0.f;
	float maxLife = 1.f;
	float mana = 0.f;
	float maxMana = 1.f;
	s16 level = 0;

	//! Index into Entity::anims of the animation each layer is playing, 0xff for none.
	u8 anim0 = 0xff;
	u8 anim1 = 0xff;
	u8 anim3 = 0xff;
	u16 anim0Flags = 0;
	u16 anim1Flags = 0;
	s32 anim0Time = 0;
	s32 anim1Time = 0;

	bool dead = false;
	bool combat = false;
	float invisibility = 0.f;
	u8 skin = 0;

	//! Class path of the weapon in hand, empty when unarmed. Used to hang the
	//! right mesh off the body so the other player can see what you are holding.
	std::string weapon;

	/*
	 * What they are wearing. Armour is not carried like a weapon - it changes
	 * the body itself, swapping mesh parts and repainting skin - so these are
	 * the classes to dress the copy of them standing here with.
	 */
	std::string helmet;
	std::string armour;
	std::string leggings;
	std::string shield;

	//! Local time of the last update, used to fade the body out if they drop.
	u32 lastUpdate = 0;

};

//! The other player. Only meaningful while a session is running.
[[nodiscard]] const Avatar & avatar();
[[nodiscard]] Avatar & mutableAvatar();

/*!
 * Record one position report from the other player, stamped with THEIR clock.
 *
 * The body is not drawn where the newest packet put it: it is drawn a step in
 * the past of the other machine's timeline, blending between the two reports
 * around the drawn moment, so it walks the way they walked instead of
 * stepping between 20 Hz updates.
 */
void pushAvatarSample(u32 timeMs, const Vec3f & pos, const Anglef & angle);

/*!
 * The guest's own belongings are created in a private range of entity ids.
 *
 * Entity ids are per-class instance numbers handed out from the lowest free
 * slot on the machine that creates the entity, and they are also the name
 * every co-op message uses. A guest that mints its own items from the same
 * low numbers is certain to collide with the host's world: stripping the
 * host-clone frees exactly the numbers the clone used, so the restored
 * weapon takes the id of the very weapon it was copied from. Then the day
 * the first player's sword breaks, the world says "this id is gone" and the
 * second player's weapon evaporates out of their hands.
 *
 * Inside this scope, ids start at GuestItemInstanceBase instead, a range no
 * level and no host allocation ever reaches.
 */
struct ScopedGuestItems {
	ScopedGuestItems();
	~ScopedGuestItems();
	ScopedGuestItems(const ScopedGuestItems &) = delete;
	ScopedGuestItems & operator=(const ScopedGuestItems &) = delete;
};

//! Lowest entity instance number to try when creating an entity right now.
[[nodiscard]] s32 firstFreeInstanceHint();

/*!
 * The guest's character, kept per playthrough.
 *
 * A world-transfer join makes the guest a clone of the host's character.
 * applyGuestIdentity() strips the clone and installs the character saved
 * for this playthrough - or a fresh hero on the first ever join. The
 * profile (stats, gold, runes, hunger, inventory classes, equipped slots)
 * is written every minute and at shutdown, keyed by the playthrough id.
 */
void applyGuestIdentity();
void saveGuestProfileIfDue(bool force);

//! Debug: name everything this player wears or carries, with entity ids.
void logOwnBelongings(const char * when);

/*!
 * While the local player lies dead, keep the door open for a rescue.
 *
 * Runs on the dead player's machine every death frame. When the other player
 * stands over the body for two seconds, the local player gets back up with
 * half their health and the partner is told. \return true while a rescue is
 * still possible, which is what holds the death fade short of the main menu.
 */
bool updateReviveOpportunity();

/*!
 * The entity that carries the other player's body in this world, or nullptr.
 *
 * It is a real entity, created from the same mesh and animation set as the
 * player, so everything that already knows how to light, animate, cull and hit
 * an entity works on it without being taught about co-op.
 */
[[nodiscard]] Entity * avatarEntity();

//! Where the guest's own belongings are kept between sessions.
[[nodiscard]] fs::path guestProfileFile();

/*!
 * True for the other player's body.
 *
 * The engine special-cases the player in a number of passes: NPC AI skips it,
 * the treat zone always contains it, and the first-person mesh trimming applies
 * to it. A second player's body has to be recognised in those same places, or
 * it inherits NPC behaviour and gets steered around by the AI.
 */
[[nodiscard]] bool isAvatarEntity(const Entity * entity);

//! Create, move, animate or retire the other player's body. Once per frame.
void updateAvatar();

//! Drop the body without touching the entity, for level teardown.
void resetAvatar();

//! Destroy the body, e.g. when the other player leaves the area or the session.
void destroyAvatarEntity();

/*!
 * Fill in this machine's own body state for sending.
 *
 * Reads the player globals rather than taking arguments so that there is one
 * place that decides what a body state consists of, shared by both sides.
 */
void captureLocalAvatar(Avatar & out);

//! Apply damage the other player dealt to this one, on the machine that owns the health.
void applyRemoteDamage(float damage, u32 damageType);

/*!
 * Distance from a creature to a player position, counted the way feet count it.
 *
 * Straight-line distance walks through walls; the engine's room-to-room
 * distance follows the portals. A player one wall away is close as the crow
 * flies and far as the goblin walks, and target choices made with the wrong
 * one of those send creatures marching at closed doors while the other player
 * stands next to them.
 */
[[nodiscard]] float pathDistanceToPlayer(const Entity * io, const Vec3f & playerPos);

/*!
 * Which of the two players a creature going after "the player" should pick.
 *
 * Sight first: a player it can currently see beats one it cannot, whatever the
 * distances say. Then walking distance. A dead player is never picked while
 * the other is alive. Pass the current target to add hysteresis, so two
 * players standing shoulder to shoulder do not make it swap every frame.
 *
 * Outside a co-op session this is always the first player.
 */
[[nodiscard]] Entity * chooseTargetPlayer(const Entity * io, const Entity * current = nullptr);

/*!
 * Marks a stretch of script execution as being *about* the other player.
 *
 * Scripts have one word, "player", and outside co-op it can only mean one
 * person. When the other player trips a trigger zone or strikes something, the
 * script that runs in response says things like DODAMAGE PLAYER 500 - and
 * without context, that damage would land on whoever this machine calls
 * player, i.e. the wrong one.
 *
 * While an instance of this guard is alive, the name "player" resolves to the
 * other player's body instead, so the consequences of their actions fall on
 * them. It activates only when constructed with their body; constructed with
 * anything else it does nothing, so call sites do not need their own check.
 */
class ScopedPlayerContext {

	bool m_active;

public:

	explicit ScopedPlayerContext(const Entity * cause);
	~ScopedPlayerContext();

	ScopedPlayerContext(const ScopedPlayerContext &) = delete;
	ScopedPlayerContext & operator=(const ScopedPlayerContext &) = delete;

};

//! The other player's body while inside a ScopedPlayerContext, else nullptr.
[[nodiscard]] Entity * scriptContextPlayer();

//! True while script execution is about the other player and their body exists.
[[nodiscard]] bool isPartnerScriptContext();

/*!
 * Draw the partner's name and health under the minimap.
 *
 * Knowing whether the person you are fighting alongside is about to die is the
 * difference between co-op and two people in the same corridor, and it is the
 * one piece of their character sheet worth taking screen space for.
 */
void drawPartnerHud();

/*!
 * The other player's life orb, drawn directly above the local one.
 *
 * Called from inside the interface's own render pass and handed the local
 * orb's rectangle, so it shares the HUD's scale, slide and render state -
 * these gauges are alpha-cut textures and draw as flat pictures anywhere
 * else. \param mine where the local player's life orb currently sits.
 */
void drawPartnerHealthOrb(const Rectf & mine);

} // namespace coop

#endif // ARX_NET_COOPPLAYER_H

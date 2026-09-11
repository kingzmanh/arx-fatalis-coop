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

#include "net/CoopMmo.h"

#include <array>
#include <cmath>
#include <map>
#include <string>

#include "cinematic/CinematicController.h"
#include "core/Config.h"
#include "core/Core.h"
#include "core/GameTime.h"
#include "animation/Animation.h"
#include "core/Localisation.h"
#include "game/Camera.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "game/Spells.h"
#include "game/magic/SpellData.h"
#include "game/magic/SpellRecognition.h"
#include "graphics/Draw.h"
#include "graphics/Math.h"
#include "graphics/Raycast.h"
#include "graphics/Renderer.h"
#include "graphics/data/Mesh.h"
#include "graphics/data/TextureContainer.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourcePath.h"
#include "gui/Interface.h"
#include "gui/Menu.h"
#include "gui/hud/PlayerInventory.h"
#include "gui/hud/SecondaryInventory.h"
#include "gui/Notification.h"
#include "gui/Text.h"
#include "input/Input.h"
#include "io/log/Logger.h"
#include "math/Angle.h"
#include "net/CoopPlayer.h"
#include "physics/Collisions.h"
#include "script/Script.h"
#include "scene/GameSound.h"
#include "platform/Time.h"
#include "util/String.h"

//! Defined in ArxGame.cpp; whether the camera hangs outside the body.
extern bool EXTERNALVIEW;

namespace coop {

bool mmoMode() {
	return config.input.mmoMode;
}

// -- the bar's contents ----------------------------------------------------

static const size_t BAR_SLOTS = 5;

/*!
 * One key's worth of bar.
 *
 * Key one is the swing and stays the swing: it is the one thing a character
 * can always do, it costs nothing, and a player who had filled every key
 * with spells and then met something in a corridor would have nothing to
 * hit it with. The other four are theirs to fill.
 */
struct Slot {

	enum Kind {
		Empty,
		AutoAttack,
		Cast
	};

	Kind kind;
	SpellType spell;

	Slot() : kind(Empty), spell(SPELL_NONE) { }

};

static std::array<Slot, BAR_SLOTS> g_bar;
static bool g_barLoaded = false;

/*
 * The bar lives in the config rather than in the save, deliberately. Which
 * keys a player likes their spells on is a habit, not a fact about a
 * character: it should survive starting a new game, and it should be the same
 * on both halves of a two-player session without either machine having to
 * tell the other. A spell the character has not learned yet simply draws dark
 * until they learn it.
 */
static void loadBar() {

	g_barLoaded = true;
	g_bar.fill(Slot());

	const std::string & spec = config.input.actionBar;
	size_t slot = 0;
	size_t start = 0;

	while(slot < BAR_SLOTS && start <= spec.size()) {

		size_t end = spec.find(',', start);
		if(end == std::string::npos) {
			end = spec.size();
		}

		std::string name = std::string(util::trim(std::string_view(spec).substr(start, end - start)));
		if(name == "attack") {
			g_bar[slot].kind = Slot::AutoAttack;
		} else if(!name.empty()) {
			SpellType spell = GetSpellId(name);
			if(spell != SPELL_NONE) {
				g_bar[slot].kind = Slot::Cast;
				g_bar[slot].spell = spell;
			} else {
				LogWarning << "[mmo] action bar slot " << (slot + 1) << " names no spell: " << name;
			}
		}

		slot++;
		start = end + 1;
	}

	// Whatever an older file or a hand-edited one says, key one is the swing
	g_bar[0].kind = Slot::AutoAttack;
	g_bar[0].spell = SPELL_NONE;

}

static void saveBar() {

	std::string spec;
	for(size_t i = 0; i < BAR_SLOTS; i++) {
		if(i != 0) {
			spec += ',';
		}
		switch(g_bar[i].kind) {
			case Slot::AutoAttack: spec += "attack"; break;
			case Slot::Cast:       spec += spellName(g_bar[i].spell); break;
			case Slot::Empty:      break;
		}
	}

	config.input.actionBar = spec;
	config.save();

}

// -- which keys belong to whom ---------------------------------------------

static const ControlAction MMO_ACTIONS[] = {
	CONTROLS_CUST_MMO_SLOT1, CONTROLS_CUST_MMO_SLOT2, CONTROLS_CUST_MMO_SLOT3,
	CONTROLS_CUST_MMO_SLOT4, CONTROLS_CUST_MMO_SLOT5,
	CONTROLS_CUST_MMO_THIRDPERSON, CONTROLS_CUST_MMO_TARGET
};

bool mmoTakesOver(ControlAction action) {

	if(!thirdPerson()) {
		return false;
	}

	/*
	 * Only the key actually being pressed is taken, never the whole action.
	 *
	 * Getting this wrong cost a weapon: draw-weapon is bound to Tab AND to
	 * Numpad0, targeting took Tab, and suppressing the action rather than the
	 * key killed Numpad0 along with it - leaving no way to draw a weapon at
	 * all, and so no way to break a wooden grate and get off the first floor.
	 * A shared key changes hands; a binding that shares nothing keeps working.
	 */
	for(InputKeyId theirs : config.actions[action].key) {

		if(theirs == ActionKey::UNUSED || !GInput->isKeyPressed(theirs)) {
			continue;
		}

		for(ControlAction mine : MMO_ACTIONS) {
			for(InputKeyId ours : config.actions[mine].key) {
				if(ours != ActionKey::UNUSED && ours == theirs) {
					return true;
				}
			}
		}

	}

	return false;
}

// -- the camera ------------------------------------------------------------

/*
 * How far back the shoulder view sits, in world units, and zero for down the
 * character's own eyes. Kept here rather than in the config because a player
 * pulls the wheel a dozen times a minute and none of that is worth a file
 * write; the mode itself is what persists.
 */
static float g_camDistance = 0.f;
static bool g_holdingCamera = false;
static Vec3f g_eye(0.f);

/*
 * The camera's own heading, kept apart from the character's.
 *
 * While the camera is in at the eyes these simply follow player.angle, so
 * first person is untouched and zooming out starts from wherever the player
 * was already looking.
 */
static float g_camYaw = 0.f;
static float g_camPitch = 0.f;

//! Arx keeps angles in 0..360; this is the same angle as a turn either side of straight ahead.
static float signedAngle(float degrees) {
	float a = MAKEANGLE(degrees);
	return (a > 180.f) ? a - 360.f : a;
}

//! Where the camera goes when third person is switched on cold.
static const float DEFAULT_CAM_DISTANCE = 200.f;
static const float MAX_CAM_DISTANCE = 420.f;
static const float ZOOM_STEP = 35.f;

bool thirdPerson() {
	return mmoMode() && g_camDistance > 1.f;
}

/*!
 * True while the game is telling its own story and the camera is not ours to
 * take: a cinematic, or any moment the player's controls are blocked.
 *
 * Starting a new game showed why this is needed. Third person is on from the
 * very first frame, so before the opening cinematic had begun the camera was
 * already out behind the body - and what it showed was the player standing in
 * an unlit room, which is exactly what the intro is there to hide.
 */
static bool cameraBlocked() {
	return BLOCK_PLAYER_CONTROLS || isInCinematic();
}

bool beginCameraFrame() {

	bool wanted = thirdPerson() && !cameraBlocked();

	/*
	 * Hand the flag back on the frame the mode ends. Without this the engine
	 * would still believe the camera is outside the body next frame and take
	 * its cutscene branch, leaving the player looking at their own back with
	 * no way out.
	 */
	if(!wanted && g_holdingCamera) {
		EXTERNALVIEW = false;
		g_holdingCamera = false;
	}

	if(wanted) {
		g_holdingCamera = true;
	}

	return wanted;
}

void notePlayerEye(const Vec3f & eye) {
	g_eye = eye;
}

Anglef cameraAngle() {
	return Anglef(g_camPitch, g_camYaw, 0.f);
}

//! The pitch a third-person camera may reach. Past this it is either in the floor or on top of the head.
static const float CAM_PITCH_LIMIT = 62.f;

/*!
 * Hold this and the left button is Arx's again for as long as it is down.
 *
 * Picking something up off the floor is a press, a drag and a release - the
 * exact shape of a camera swing - so no amount of measuring can tell the two
 * apart. A key says which was meant. Alt is already the strafe modifier, so
 * while it is down the arrow keys strafe rather than turn; that is the whole
 * cost, and it only applies while a hand is on the keyboard anyway.
 */
static bool handModifier() {
	return GInput->isKeyPressed(Keyboard::Key_LeftAlt);
}

bool cameraDragging() {

	if(!thirdPerson() || BLOCK_PLAYER_CONTROLS || handModifier()) {
		return false;
	}

	// with the book open the mouse is reading it, not flying the camera
	if(player.Interface & INTER_PLAYERBOOK) {
		return false;
	}

	return GInput->getMouseButtonRepeat(Mouse::Button_0)
	       || GInput->getMouseButtonRepeat(Mouse::Button_1);
}

bool strafeIsTurn() {
	return thirdPerson() && !BLOCK_PLAYER_CONTROLS
	       && !GInput->getMouseButtonRepeat(Mouse::Button_1);
}

bool mouseRunForward() {

	if(!thirdPerson() || BLOCK_PLAYER_CONTROLS || (player.Interface & INTER_PLAYERBOOK)) {
		return false;
	}

	return GInput->getMouseButtonRepeat(Mouse::Button_0)
	       && GInput->getMouseButtonRepeat(Mouse::Button_1);
}

bool mmoTurn(const Vec2f & rotation, bool keyTurn) {

	if(!thirdPerson()) {
		return false;
	}

	bool turnBody = keyTurn || GInput->getMouseButtonRepeat(Mouse::Button_1);
	bool orbit = GInput->getMouseButtonRepeat(Mouse::Button_0) && !handModifier();

	if(orbit || turnBody) {
		static PlatformInstant lastTurnLog = 0;
		PlatformInstant turnNow = platform::getTime();
		if(turnNow - lastTurnLog >= 150ms) {
			lastTurnLog = turnNow;
			LogInfo << "[mmo-turn] rot=" << rotation.x << "," << rotation.y
			        << " orbit=" << int(orbit) << " turnBody=" << int(turnBody)
			        << " camYaw=" << int(g_camYaw);
		}
	}

	if(!turnBody && !orbit) {
		/*
		 * Nothing held: the mouse is a cursor and moving it moves nothing.
		 * Consumed rather than passed on, so that neither Arx's mouse-look nor
		 * its turn-at-the-screen-edge quietly swings the view while somebody
		 * is reaching for the action bar.
		 */
		return true;
	}

	g_camYaw = MAKEANGLE(g_camYaw - rotation.x);
	g_camPitch = glm::clamp(g_camPitch + rotation.y, -CAM_PITCH_LIMIT, CAM_PITCH_LIMIT);

	/*
	 * A right drag, or the turn keys, turn the character too - and the way to
	 * do that is to let the engine's own mouse-look code have the rotation,
	 * rather than to write player.angle here. It clamps the pitch, flags the
	 * turn for the walk animation and times the footsteps; all of that would
	 * have to be copied, and would then rot.
	 */
	return !turnBody;
}

// -- the mouse buttons -----------------------------------------------------

/*
 * The physical buttons, deliberately, not whatever ACTION and USE are bound
 * to. "Hold left click to swing the camera" is a statement about the mouse,
 * and on this machine those two actions are not even on the mouse - USE is on
 * F and the right button belongs to FREELOOK - so asking about the bindings
 * meant the right button did nothing at all.
 *
 * A press in third person cannot be acted on when it happens.
 *
 * The left button has two jobs - swing the camera, or click on something -
 * and which one it was is not known until it is let go. So nothing happens on
 * the way down; movement is measured while it is held; and if it comes back
 * up having barely travelled, the click is played back as a single frame of
 * the button being down, which is what the rest of the game is watching for.
 * Travel is measured from the mouse's own movement rather than the cursor,
 * because the cursor is grabbed and standing still for the whole drag.
 */
struct HeldButton {

	bool held = false;
	float travel = 0.f;
	int pulse = 0;

};

static HeldButton g_left;
static HeldButton g_right;

//! Far enough that it was meant as a drag, near enough that a shaky click still counts as one.
static const float DRAG_SLOP = 6.f;

static void updateHeldButton(HeldButton & button, int mouseButton, long bit, float moved) {

	if(button.pulse > 0) {
		button.pulse--;
		if(button.pulse == 0) {
			EERIEMouseButton &= ~bit;
		}
		return;
	}

	if(GInput->getMouseButtonNowPressed(mouseButton)) {
		button.held = true;
		button.travel = 0.f;
	}

	if(button.held) {
		button.travel += moved;
	}

	if(GInput->getMouseButtonNowUnPressed(mouseButton)) {
		if(button.held && button.travel < DRAG_SLOP) {
			EERIEMouseButton |= bit;
			button.pulse = 2;
		}
		button.held = false;
	}

}

bool mmoMouseButtons() {

	/*
	 * With the modifier down the buttons go straight back to the engine,
	 * pressing and releasing when they really do, which is what dragging an
	 * item needs. Any half-finished camera press is dropped rather than
	 * replayed later as a click nobody asked for.
	 */
	if(!thirdPerson() || handModifier()) {
		g_left = HeldButton();
		g_right = HeldButton();
		return false;
	}

	Vec2f rel = GInput->getRelativeMouseMovement();
	float moved = std::abs(rel.x) + std::abs(rel.y);

	/*
	 * Taking hold of the right button faces the character the way the camera
	 * is looking, which is what an MMO does: a left drag lets you look around
	 * without turning, and grabbing the right button says "never mind that, I
	 * am going this way". Without it the offset a left drag opened up would
	 * be kept for ever and the character would turn permanently askew from
	 * the view.
	 */
	if(GInput->getMouseButtonNowPressed(Mouse::Button_1) && !BLOCK_PLAYER_CONTROLS
	   && !(player.Interface & INTER_PLAYERBOOK)) {
		player.angle.setYaw(g_camYaw);
		player.desiredangle.setYaw(g_camYaw);
	}

	updateHeldButton(g_left, Mouse::Button_0, 1, moved);
	updateHeldButton(g_right, Mouse::Button_1, 2, moved);

	return true;
}

Vec3f eyePos() {
	return thirdPerson() ? g_eye : g_playerCamera.m_pos;
}

//! How far short of a wall the camera stops, so the near plane does not poke through it.
static const float CAM_WALL_MARGIN = 24.f;

/*
 * The boom, and the wall it stops at.
 *
 * The first version of this stepped backwards asking CheckInPoly whether there
 * was still floor underneath, which is what the cutscene camera does. That is
 * the wrong question: floor beneath a point says nothing about the wall
 * between it and the player's head, so the camera walked straight through
 * masonry and ended up outside the level looking back in at the dark.
 *
 * A ray from the eye to where the camera wants to be answers the right
 * question, and the engine already has one - the same raycastScene an arrow
 * uses to find what it hit. Stop a little short of whatever it finds.
 */
Vec3f thirdPersonCameraPos(const Vec3f & eye, const Anglef & angle) {

	Vec3f back = -angleToFrontUpVec(angle).first;
	Vec3f wanted = eye + back * g_camDistance;

	RaycastResult blocked = raycastScene(eye, wanted, POLY_TRANS, RaycastIgnorePlayer);
	if(!blocked) {
		return wanted;
	}

	float reach = glm::distance(blocked.pos, eye) - CAM_WALL_MARGIN;

	return eye + back * std::max(0.f, reach);
}

static void updateCameraZoom() {

	if(ARXmenu.mode() != Mode_InGame) {
		return;
	}

	if(GInput->actionNowPressed(CONTROLS_CUST_MMO_THIRDPERSON)) {
		g_camDistance = (g_camDistance > 1.f) ? 0.f : DEFAULT_CAM_DISTANCE;
	}

	/*
	 * The book and the bags want the wheel, but only while the cursor is
	 * actually on them. Testing the flags instead was why the wheel did
	 * nothing: cursor mode leaves the inventory permanently open (see
	 * InventoryOpenClose in ARX_INTERFACE_NoteManage's caller), so the flag is
	 * always set and the camera never got a single notch.
	 */
	if((player.Interface & INTER_PLAYERBOOK) || g_cursorOverBook
	   || g_playerInventoryHud.containsPos(DANAEMouse)
	   || g_secondaryInventoryHud.containsPos(DANAEMouse)) {
		return;
	}

	int wheel = GInput->getMouseWheelDir();
	if(wheel > 0) {
		g_camDistance = std::max(0.f, g_camDistance - ZOOM_STEP);
	} else if(wheel < 0) {
		g_camDistance = std::min(MAX_CAM_DISTANCE, g_camDistance + ZOOM_STEP);
	}

}

// -- auto-attack -----------------------------------------------------------

static bool g_autoAttack = false;
static bool g_swingHeld = false;
static PlatformInstant g_releasedAt = 0;

/*
 * The reach of a blow, taken from the game rather than chosen: NPC.cpp works
 * out whether a creature is close enough to swing at with exactly this number.
 * Anything else here and the player's auto-swing and a goblin's would disagree
 * about what "in range" means.
 */
static const float STRIKE_DISTANCE = 220.f;

//! Half the arc a blow may be swung in. It decides when to stop flailing, not when a hit counts.
static const float STRIKE_ARC = 70.f;

bool autoAttackHeld() {
	return g_swingHeld;
}

//! Defined with the casting code below; the swing stands aside while a spell is being thrown.
static bool castingInProgress();

/*!
 * Signed degrees the player would have to turn to look straight at this
 * creature: negative to the left, positive to the right.
 *
 * The yaw comes out of the engine's own vectorToAngle rather than being
 * worked out here. Arx measures yaw from +Z with x negated, so hand-rolled
 * trigonometry has an even chance of coming out backwards - which it did,
 * the first time this was written.
 */
static float turnToward(const Entity & target) {

	Vec3f to = target.pos - entities.player()->pos;
	to.y = 0.f;
	if(arx::length2(to) < 1.f) {
		return 0.f;
	}

	float wanted = vectorToAngle(to).getYaw();
	float diff = MAKEANGLE(wanted - player.angle.getYaw());
	if(diff > 180.f) {
		diff -= 360.f;
	}

	return diff;
}

static bool facingEnough(const Entity & target) {
	return std::abs(turnToward(target)) <= STRIKE_ARC;
}

/*
 * Turn to face what is being hit, but never out from under the player's hand.
 *
 * The camera in this first cut rides the character's own facing, so turning
 * the character turns the view. Doing that while the player is moving the
 * mouse would be a fight nobody wins, so this only corrects while the mouse is
 * still.
 */
static void faceTarget(const Entity & target) {

	if(std::abs(GInput->getRelativeMouseMovement().x) > 0.01f) {
		return;
	}

	float diff = turnToward(target);
	float step = 220.f * toMsf(g_platformTime.lastFrameDuration()) * 0.001f;
	diff = glm::clamp(diff, -step, step);

	player.angle.setYaw(MAKEANGLE(player.angle.getYaw() + diff));

}

static void stopAutoAttack() {
	g_autoAttack = false;
	g_swingHeld = false;
}

static void updateAutoAttack() {

	if(!g_autoAttack) {
		g_swingHeld = false;
		return;
	}

	/*
	 * A cast in progress has just put the weapon away on purpose; swinging
	 * would drag it straight back out and the two would fight over the arm
	 * every frame. WILLRETURNTOCOMBATMODE brings the weapon back when the
	 * gesture finishes, and the swing picks up again from there.
	 */
	if(castingInProgress()) {
		g_swingHeld = false;
		return;
	}

	Entity * target = targetEntity();
	if(!target || !target->_npcdata || target->_npcdata->lifePool.current <= 0.f) {
		stopAutoAttack();
		return;
	}

	if(player.lifePool.current <= 0.f || BLOCK_PLAYER_CONTROLS) {
		g_swingHeld = false;
		return;
	}

	// Out of reach: stand ready with the weapon out, but do not swing at air
	if(!closerThan(target->pos, entities.player()->pos, STRIKE_DISTANCE)) {
		g_swingHeld = false;
		return;
	}

	faceTarget(*target);

	if(!facingEnough(*target)) {
		g_swingHeld = false;
		return;
	}

	if(!(player.Interface & INTER_COMBATMODE)) {
		ARX_INTERFACE_setCombatMode(COMBAT_MODE_ON);
		g_swingHeld = false;
		return;
	}

	/*
	 * Wind up until the swing is at full weight and then let go, which is what
	 * a player does by holding the button down. Full_AimTime is the game's own
	 * measure of a finished wind-up and shrinks as the character gets better
	 * with the weapon, so auto-attack quickens exactly as hand-swinging does.
	 *
	 * The pause after letting go is what makes the blow actually happen: the
	 * combat animations only turn a wind-up into a strike on a frame where the
	 * button is seen to be up.
	 */
	PlatformInstant now = platform::getTime();
	if(now - g_releasedAt < 120ms) {
		g_swingHeld = false;
		return;
	}

	if(g_swingHeld && player.Full_AimTime > 0 && player.m_aimTime >= player.Full_AimTime) {
		g_swingHeld = false;
		g_releasedAt = now;
		return;
	}

	g_swingHeld = true;

}

// -- casting off the bar ---------------------------------------------------

static bool knowsSpell(SpellType spell) {
	return spell != SPELL_NONE && player.hasAllRunes(spellicons[spell].symbols);
}

static float manaCost(SpellType spell) {
	return ARX_SPELLS_GetManaCost(spell, player.spellLevel());
}

/*
 * A spell pressed on a key does not go off on the key press.
 *
 * The first cut launched it there and then, and in third person that looked
 * exactly as wrong as it was: the body stayed in its idle pose and the spell
 * simply appeared at the hand, pointing wherever the camera happened to be.
 *
 * The game already had this problem and already solved it. A precast spell is
 * also fired from a key with no runes drawn, and ARX_SPELLS_Precast_Check
 * drops out of combat mode, plays ANIM_CAST, and launches only once that
 * animation is within 550ms of its end - the moment the hand is thrown
 * forward. This does the same, and turns to face the target while the arm
 * comes up, because a missile leaves along player.angle (MagicMissileSpell)
 * and so aiming the body is aiming the spell.
 */
static SpellType g_casting = SPELL_NONE;
static EntityHandle g_castingAt;

static bool castingInProgress() {
	return g_casting != SPELL_NONE;
}

/*!
 * Look where a first-person player would be looking to cast this: from the
 * eye to the middle of the creature, in the engine's own angle convention.
 *
 * \param snap take the whole remaining turn at once. True on the frame the
 *             spell actually leaves, so that what was aimed at is what is hit
 *             even if the turn had not quite finished.
 */
static void aimAtTarget(const Entity & target, bool snap) {

	// the cylinder's height points up, so half of it is the middle of the body
	Vec3f middle = target.pos + Vec3f(0.f, target.physics.cyl.height * 0.5f, 0.f);
	Vec3f to = middle - eyePos();
	if(arx::length2(to) < 1.f) {
		return;
	}

	Anglef want = vectorToAngle(to);

	float yaw = MAKEANGLE(want.getYaw() - player.angle.getYaw());
	if(yaw > 180.f) {
		yaw -= 360.f;
	}
	float pitch = want.getPitch() - player.angle.getPitch();

	if(!snap) {
		float step = 600.f * toMsf(g_platformTime.lastFrameDuration()) * 0.001f;
		yaw = glm::clamp(yaw, -step, step);
		pitch = glm::clamp(pitch, -step, step);
	}

	player.angle.setYaw(MAKEANGLE(player.angle.getYaw() + yaw));
	player.angle.setPitch(glm::clamp(player.angle.getPitch() + pitch, -70.f, 70.f));

}

static void castSlot(SpellType spell) {

	if(!knowsSpell(spell)) {
		ARX_SOUND_PlaySpeech("player_cantcast");
		return;
	}

	g_casting = spell;
	Entity * target = targetEntity();
	g_castingAt = target ? target->index() : EntityHandle();

}

static void updateCasting() {

	if(g_casting == SPELL_NONE) {
		return;
	}

	Entity * self = entities.player();
	if(!self || player.lifePool.current <= 0.f || BLOCK_PLAYER_CONTROLS) {
		g_casting = SPELL_NONE;
		return;
	}

	Entity * target = entities.get(g_castingAt);

	/*
	 * NOCHECKCANCAST is not a cheat here. ARX_SPELLS_Launch tests the runes in
	 * SpellSymbol, the buffer of what was just traced on screen, which is right
	 * for a spell drawn with the mouse and meaningless for one pressed on a
	 * key, where that buffer holds whatever was drawn last. The check the
	 * player deserves is the one castSlot() already made: do they know this
	 * spell's own runes.
	 *
	 * Level -1 and duration -1 are what the rune-drawing path passes
	 * (ARX_SPELLS_AnalyseSPELL), meaning "work it out from the caster". A spell
	 * off the bar is the same spell cast by the same character, so it gets the
	 * same power and the same mana bill.
	 */
	AnimLayer & layer1 = self->animlayer[1];

	if(!self->anims[ANIM_CAST]) {
		// no gesture to play: better an instant spell than none at all
		if(target) {
			aimAtTarget(*target, true);
		}
		ARX_SPELLS_Launch(g_casting, *self, SPELLCAST_FLAG_NOCHECKCANCAST, -1, target,
		                  GameDuration::ofRaw(-1));
		g_casting = SPELL_NONE;
		return;
	}

	if(player.Interface & INTER_COMBATMODE) {
		WILLRETURNTOCOMBATMODE = true;
		ARX_INTERFACE_setCombatMode(COMBAT_MODE_OFF);
		ResetAnim(layer1);
		layer1.flags &= ~EA_LOOP;
	}

	if(layer1.cur_anim != self->anims[ANIM_CAST]) {
		changeAnimation(self, 1, self->anims[ANIM_CAST]);
		return;
	}

	bool throwing = layer1.currentAltAnim()
	                && layer1.ctime + 550ms > layer1.currentAltAnim()->anim_time;

	if(target) {
		aimAtTarget(*target, throwing);
	}

	if(throwing) {
		ARX_SPELLS_Launch(g_casting, *self, SPELLCAST_FLAG_NOCHECKCANCAST, -1, target,
		                  GameDuration::ofRaw(-1));
		g_casting = SPELL_NONE;
	}

}

static void pressSlot(size_t index) {

	arx_assert(index < BAR_SLOTS);

	switch(g_bar[index].kind) {

		case Slot::Empty:
			break;

		case Slot::AutoAttack:
			if(g_autoAttack) {
				stopAutoAttack();
			} else if(targetEntity()) {
				g_autoAttack = true;
			} else {
				notification_add("No target");
			}
			break;

		case Slot::Cast:
			castSlot(g_bar[index].spell);
			break;

	}

}

// -- picking a target ------------------------------------------------------

/*
 * Tab, and what it means here.
 *
 * The nearest living creature in front that is further off than the one
 * already picked, so repeated presses walk outwards through a room and then
 * come back round to the nearest.
 */
void cycleTarget() {

	Entity * current = targetEntity();
	Entity * best = nullptr;
	Entity * nearest = nullptr;
	float bestDist = 0.f;
	float nearestDist = 0.f;

	float currentDist = current ? glm::distance(current->pos, entities.player()->pos) : 0.f;

	for(Entity & npc : entities.inScene(IO_NPC)) {

		if(npc == *entities.player() || !npc._npcdata || isAvatarEntity(&npc)
		   || npc._npcdata->lifePool.current <= 0.f) {
			continue;
		}

		float dist = glm::distance(npc.pos, entities.player()->pos);
		if(dist > 1200.f || !facingEnough(npc)) {
			continue;
		}

		if(!nearest || dist < nearestDist) {
			nearest = &npc;
			nearestDist = dist;
		}

		if(current && dist <= currentDist) {
			continue;
		}

		if(!best || dist < bestDist) {
			best = &npc;
			bestDist = dist;
		}

	}

	if(!best) {
		best = nearest;
	}

	if(best) {
		noteTargetHit(*best);
	}

}

// -- the frame -------------------------------------------------------------

static void updateBarEditing();

//! Both defined with the drag code below; first person drops whatever the bar was holding.
static void cancelSpellDrag();

void updateMmo() {

	if(!g_barLoaded) {
		loadBar();
	}

	if(!mmoMode()) {
		g_camDistance = 0.f;
		stopAutoAttack();
		return;
	}

	/*
	 * The game starts where it always started, at the character's own eyes.
	 * Third person is something the player asks for - V, or the wheel - not
	 * something that happens to them the moment a save loads. While the
	 * camera is in, the sync below keeps its heading on the character's, so
	 * the first press of V picks up looking exactly where they already were.
	 */
	updateCameraZoom();

	/*
	 * In at the eyes there is only one heading, so the camera's copy follows
	 * the character's. That way zooming out picks up looking exactly where
	 * the player already was, and zooming back in hands the view over without
	 * a jump.
	 */
	/*
	 * First person is the whole of the old game back, not just the old camera.
	 *
	 * Zoomed in at the eyes there is no bar, no sticky target, no auto-attack,
	 * and the number keys belong to precast again - everything downstream asks
	 * thirdPerson() rather than mmoMode(), so this one line is the switch.
	 * Anything already running is put down on the way through, or the player
	 * would arrive in first person still swinging at something they can no
	 * longer see a frame for.
	 */
	if(!thirdPerson()) {

		g_camYaw = player.angle.getYaw();
		g_camPitch = glm::clamp(signedAngle(player.angle.getPitch()),
		                        -CAM_PITCH_LIMIT, CAM_PITCH_LIMIT);

		stopAutoAttack();
		g_casting = SPELL_NONE;
		cancelSpellDrag();

		return;
	}

	if(ARXmenu.mode() == Mode_InGame && !BLOCK_PLAYER_CONTROLS) {

		if(GInput->actionNowPressed(CONTROLS_CUST_MMO_TARGET)) {
			cycleTarget();
		}

		/*
		 * With the book open the same keys fill the bar instead of firing it:
		 * the action page puts whatever the cursor is on onto the key pressed.
		 * Casting with the book in your hands was never possible anyway.
		 */
		if(!(player.Interface & INTER_PLAYERBOOK)) {
			for(size_t i = 0; i < BAR_SLOTS; i++) {
				if(GInput->actionNowPressed(MMO_ACTIONS[i])) {
					pressSlot(i);
				}
			}
		}

	}

	updateCasting();
	updateAutoAttack();
	updateBarEditing();

	/*
	 * A second's worth of state, once a second. Here because "nothing
	 * happened" is not something that can be argued about from the source -
	 * either the camera has its own heading and the buttons are being seen,
	 * or it does not and they are not, and this says which.
	 */
	static PlatformInstant lastReport = 0;
	PlatformInstant now = platform::getTime();
	if(now - lastReport >= 1000ms) {
		lastReport = now;
		LogInfo << "[mmo] third=" << int(thirdPerson()) << " dist=" << int(g_camDistance)
		        << " L=" << int(GInput->getMouseButtonRepeat(Mouse::Button_0))
		        << " R=" << int(GInput->getMouseButtonRepeat(Mouse::Button_1))
		        << " trueLook=" << int(TRUE_PLAYER_MOUSELOOK_ON)
		        << " look=" << int(PLAYER_MOUSELOOK_ON)
		        << " camYaw=" << int(g_camYaw) << " bodyYaw=" << int(player.angle.getYaw())
		        << " strafeTurns=" << int(strafeIsTurn())
		        << " rel=" << GInput->getRelativeMouseMovement().x
		        << "," << GInput->getRelativeMouseMovement().y;
	}

}

// -- drawing ---------------------------------------------------------------

/*
 * Sizes here are in the HUD's own 640x480 design space, not in pixels:
 * minSizeRatio() is min(width/640, height/480), so on a 1920x1080 screen every
 * one of these numbers is multiplied by 2.25. The whole painted bar at 340
 * comes out about 765 pixels wide there, with sockets of about 64 - roughly
 * what an MMO puts under the middle of the screen.
 */
static const float BAR_WIDTH = 255.f;

//! Only used when there is no painted frame: a drawn socket and the space between two.
static const float SLOT_SIZE = 29.f;
static const float SLOT_GAP = 4.f;

/*
 * Where the sockets are, as fractions of the painted frame.
 *
 * These were measured off action_bar.png rather than chosen: the holes are
 * painted into the picture, so the icons have to land in them and not the
 * other way round. A repainted bar has to put its sockets in the same places -
 * ACTION-BAR-ART.md says where - or these five numbers have to move with it.
 */
static const float ART_SLOT_STEP = 0.18373f; //!< centre to centre, of frame width
static const float ART_SLOT_MID = 0.49785f;  //!< the middle socket's centre
static const float ART_SLOT_Y = 0.4937f;     //!< socket centre height, of frame height
static const float ART_SLOT_SIZE = 0.084f;   //!< socket opening, of frame width
static const float ART_LIT_SCALE = 1.45f;    //!< the lit socket sits proud of the icon, so its glow shows

/*
 * Painted art if it is there, drawn shapes if it is not.
 *
 * The same bargain the target frame strikes: this has to look right with
 * nothing but code, and better the moment somebody paints the pictures. Drop
 * a file into graph/interface/coop and it is used from the next start;
 * nothing here has to be told it arrived.
 *
 *   action_bar       384 x 96   the trough all five slots sit in
 *   action_slot       64 x 64   one empty socket
 *   action_slot_lit   64 x 64   the same socket, lit: ready to cast, or hovered
 *   action_attack     64 x 64   the swing, for whichever slot holds it
 *   spell_<name>      64 x 64   an icon of our own for one spell, optional.
 *                               <name> is the spell's script name, so
 *                               spell_magic_missile, spell_heal, spell_ignit
 */
static TextureContainer * g_barFrame = nullptr;
static TextureContainer * g_slotArt = nullptr;
static TextureContainer * g_slotArtLit = nullptr;
static TextureContainer * g_attackArt = nullptr;
static bool g_barArtLooked = false;

/*!
 * Load a picture if the player put one there, and say nothing if they did not.
 *
 * Asking the texture loader directly would work, but it logs a loud error for
 * every file it cannot find, and most of these files are meant to be absent -
 * the bar is supposed to run on drawn shapes until somebody paints it. So the
 * archive is asked first, quietly. PNG, because that is the only one of the
 * formats the engine tries that keeps an alpha channel without the old
 * pure-black colour key, and the bar has to sit over the world.
 */
static TextureContainer * loadIfPresent(const std::string & name) {

	if(!g_resources->getFile(res::path(name + ".png"))) {
		return nullptr;
	}

	return TextureContainer::LoadUI(res::path(name));
}

static void lookForBarArt() {

	if(g_barArtLooked) {
		return;
	}

	g_barArtLooked = true;
	g_barFrame = loadIfPresent("graph/interface/coop/action_bar");
	g_slotArt = loadIfPresent("graph/interface/coop/action_slot");
	g_slotArtLit = loadIfPresent("graph/interface/coop/action_slot_lit");
	g_attackArt = loadIfPresent("graph/interface/coop/action_attack");

	LogInfo << "[mmo] action bar art: frame " << (g_barFrame ? "painted" : "drawn")
	        << ", sockets " << (g_slotArt ? "painted" : "drawn")
	        << "; screen " << g_size.width() << "x" << g_size.height()
	        << ", scale " << minSizeRatio() << ", bar " << (BAR_WIDTH * minSizeRatio()) << "px";

}

/*!
 * The painted frame's place on screen.
 *
 * Its height comes from the picture's own proportions rather than a number
 * here, so a taller or shorter repaint is drawn as it was painted instead of
 * being squashed into a shape somebody once assumed.
 */
static Rectf barFrameRect() {

	arx_assert(g_barFrame);

	float scale = minSizeRatio();
	float width = BAR_WIDTH * scale;
	float height = width * float(g_barFrame->m_size.y) / float(g_barFrame->m_size.x);
	float left = (float(g_size.width()) - width) * 0.5f;
	float top = float(g_size.height()) - height - 4.f * scale;

	return Rectf(Vec2f(left, top), width, height);
}

static Rectf slotRect(size_t index) {

	lookForBarArt();

	float scale = minSizeRatio();

	if(g_barFrame) {
		Rectf frame = barFrameRect();
		float size = ART_SLOT_SIZE * frame.width();
		float cx = frame.left + (ART_SLOT_MID + (float(index) - 2.f) * ART_SLOT_STEP) * frame.width();
		float cy = frame.top + ART_SLOT_Y * frame.height();
		return Rectf(Vec2f(cx - size * 0.5f, cy - size * 0.5f), size, size);
	}

	float size = SLOT_SIZE * scale;
	float gap = SLOT_GAP * scale;
	float total = float(BAR_SLOTS) * size + float(BAR_SLOTS - 1) * gap;
	float left = (float(g_size.width()) - total) * 0.5f + float(index) * (size + gap);
	float top = float(g_size.height()) - size - 14.f * scale;

	return Rectf(Vec2f(left, top), size, size);
}

bool cursorOverActionBar() {
	
	if(!thirdPerson()) {
		return false;
	}
	
	for(size_t i = 0; i < BAR_SLOTS; i++) {
		if(slotRect(i).contains(Vec2f(DANAEMouse))) {
			return true;
		}
	}
	
	return false;
}

float actionBarLeft() {

	if(!thirdPerson()) {
		return 0.f;
	}

	lookForBarArt();

	return g_barFrame ? barFrameRect().left : slotRect(0).left;
}

//! The label under a key, which is whatever that slot is actually bound to.
static std::string slotKeyName(size_t index) {

	InputKeyId key = config.actions[MMO_ACTIONS[index]].key[0];
	if(key == ActionKey::UNUSED) {
		key = config.actions[MMO_ACTIONS[index]].key[1];
	}
	if(key == ActionKey::UNUSED) {
		return std::string();
	}

	return std::string(Input::getKeyName(key));
}

/*
 * The spell book's icons are ink, not pictures: they are multiplied into the
 * page rather than laid on top of it, so every slot has to give one a pale
 * ground to be ink on, exactly as the parchment does.
 *
 * The icons themselves are the game's own, for every spell. Painted ones of
 * ours were tried and put away again: the game has an icon for every spell
 * already, drawn by the people who drew the book, and three of ours beside
 * forty of theirs only made the bar look half finished.
 */
static const Color SLOT_PARCHMENT = Color(190, 172, 140);
static const Color SLOT_PARCHMENT_DIM = Color(96, 88, 74);
static const Color SLOT_STONE = Color(38, 34, 30);
static const Color SLOT_EDGE = Color(24, 20, 16);
static const Color SLOT_EDGE_LIVE = Color(226, 188, 92);


/*!
 * \param castable the character knows this spell and can pay for it. A spell
 *                 they cannot cast is dimmed rather than hidden, so the bar
 *                 still reads as the same five things in the same five places.
 */
static void drawSlotIcon(const Rectf & rect, SpellType spell, bool castable) {

	if(!spellicons[spell].tc) {
		return;
	}

	/*
	 * A book icon is ink: it is multiplied into what is behind it, so on the
	 * dark floor of a socket there would be nothing to see. It gets the pale
	 * ground the book's own page would have given it. Dimming here means a
	 * duller page, not less ink - ink faded towards black would disappear
	 * entirely under this blend.
	 */
	EERIEDrawBitmap(rect, 0.00075f, nullptr, castable ? SLOT_PARCHMENT : SLOT_PARCHMENT_DIM);

	UseRenderState state(render2D().blend(BlendZero, BlendInvSrcColor).alphaCutout());
	EERIEDrawBitmap(rect, 0.0007f, spellicons[spell].tc, Color::white);

}

//! A sword for the swing, drawn rather than loaded: the game has no icon for a plain attack.
static void drawSwingMark(const Rectf & rect, Color color) {

	float w = rect.width();
	Rectf blade(Vec2f(rect.left + w * 0.18f, rect.top + w * 0.45f), w * 0.64f, w * 0.10f);
	EERIEDrawBitmap(blade, 0.0007f, nullptr, color);
	Rectf hilt(Vec2f(rect.left + w * 0.30f, rect.top + w * 0.26f), w * 0.09f, w * 0.48f);
	EERIEDrawBitmap(hilt, 0.0007f, nullptr, color);

}

void drawActionBar() {

	// nor a bar over the top of a cutscene
	if(!thirdPerson() || cameraBlocked()) {
		return;
	}

	if(!g_barLoaded) {
		loadBar();
	}

	lookForBarArt();

	float scale = minSizeRatio();
	bool editing = (player.Interface & INTER_PLAYERBOOK) != 0;

	if(g_barFrame) {
		UseRenderState state(render2D().alphaCutout());
		EERIEDrawBitmap(barFrameRect(), 0.0010f, g_barFrame, Color::white);
	}

	for(size_t i = 0; i < BAR_SLOTS; i++) {

		Rectf rect = slotRect(i);
		const Slot & slot = g_bar[i];

		bool castable = false;
		bool hover = editing && rect.contains(Vec2f(DANAEMouse));

		switch(slot.kind) {
			case Slot::Empty:
				break;
			case Slot::AutoAttack:
				castable = true;
				break;
			case Slot::Cast:
				castable = knowsSpell(slot.spell) && player.manaPool.current >= manaCost(slot.spell);
				break;
		}

		/*
		 * The socket lights for something happening, not for something merely
		 * possible: the mouse is on it, or it is the swing and the swing is
		 * running. Lighting every castable spell would leave the whole bar
		 * glowing most of the time, which says nothing.
		 */
		bool lit = hover || (slot.kind == Slot::AutoAttack && g_autoAttack);

		if(g_barFrame) {
			// the sockets are painted into the frame; only the lit one is added
			if(lit && g_slotArtLit) {
				float size = rect.width() * ART_LIT_SCALE;
				Rectf glow(rect.center() - Vec2f(size * 0.5f), size, size);
				UseRenderState state(render2D().alphaCutout());
				EERIEDrawBitmap(glow, 0.00085f, g_slotArtLit, Color::white);
			}
		} else if(TextureContainer * socket = (lit && g_slotArtLit) ? g_slotArtLit : g_slotArt) {
			UseRenderState state(render2D().alphaCutout());
			EERIEDrawBitmap(rect, 0.0009f, socket, Color::white);
		} else {
			float edge = 2.f * scale;
			Rectf backing(rect.topLeft() - Vec2f(edge), rect.width() + 2.f * edge,
			              rect.height() + 2.f * edge);
			EERIEDrawBitmap(backing, 0.0009f, nullptr, lit ? SLOT_EDGE_LIVE : SLOT_EDGE);
			EERIEDrawBitmap(rect, 0.0008f, nullptr, SLOT_STONE);
		}

		switch(slot.kind) {
			case Slot::Empty:
				break;
			case Slot::AutoAttack:
				if(g_attackArt) {
					/*
					 * Full strength while it is swinging, dimmed while it is
					 * not - the same language the spells speak, so the bar
					 * reads as one thing rather than a painting next to a
					 * diagram.
					 */
					UseRenderState state(render2D().alphaCutout());
					EERIEDrawBitmap(rect, 0.0007f, g_attackArt,
					                g_autoAttack ? Color::white : Color::gray(0.55f));
				} else {
					drawSwingMark(rect, g_autoAttack ? Color(232, 198, 120) : Color(120, 112, 98));
				}
				break;
			case Slot::Cast:
				drawSlotIcon(rect, slot.spell, castable);
				break;
		}

		if(hFontInGame) {

			std::string key = slotKeyName(i);
			if(!key.empty()) {
				drawTextCentered(hFontInGame,
				                 Vec2f(rect.right - 0.14f * rect.width(),
				                       rect.bottom - 0.16f * rect.height()),
				                 key, Color::white);
			}

			if(slot.kind == Slot::Cast && hover) {
				drawTextCentered(hFontInGame, Vec2f(rect.center().x, rect.top - 16.f * scale),
				                 std::string(getLocalised(spellicons[slot.spell].name)), Color::white);
			}

		}

	}

}

// -- dragging a spell out of the book --------------------------------------

static SpellType g_dragged = SPELL_NONE;

static void cancelSpellDrag() {
	g_dragged = SPELL_NONE;
}

void beginSpellDrag(SpellType spell) {
	if(thirdPerson()) {
		g_dragged = spell;
	}
}

/*
 * Editing the bar only happens with the spell book open, and that is on
 * purpose: out in the world every click already means something - swinging,
 * using, tracing a rune - and a bar that quietly ate one of those would be a
 * bug the player could not explain. With the book open the bar is furniture,
 * so a click on it can safely mean "put this here" and a right-click "take it
 * off again".
 */
static void updateBarEditing() {

	if(!thirdPerson()) {
		g_dragged = SPELL_NONE;
		return;
	}

	if(g_dragged != SPELL_NONE && !eeMousePressed1()) {

		for(size_t i = 1; i < BAR_SLOTS; i++) { // key one is the swing
			if(slotRect(i).contains(Vec2f(DANAEMouse))) {
				g_bar[i].kind = Slot::Cast;
				g_bar[i].spell = g_dragged;
				saveBar();
				break;
			}
		}

		g_dragged = SPELL_NONE;

	}

	if((player.Interface & INTER_PLAYERBOOK) && eeMouseDown2()) {
		for(size_t i = 1; i < BAR_SLOTS; i++) {
			if(slotRect(i).contains(Vec2f(DANAEMouse)) && g_bar[i].kind != Slot::Empty) {
				g_bar[i] = Slot();
				saveBar();
				break;
			}
		}
	}

}

void newGame() {
	g_camDistance = 0.f;
	stopAutoAttack();
	g_casting = SPELL_NONE;
	cancelSpellDrag();
	g_bar.fill(Slot());
	g_bar[0].kind = Slot::AutoAttack;
	g_barLoaded = true;
	saveBar();
}

size_t barSlots() {
	return BAR_SLOTS;
}

int barSlotOf(SpellType spell) {
	for(size_t i = 0; i < BAR_SLOTS; i++) {
		bool here = (spell == SPELL_NONE) ? (g_bar[i].kind == Slot::AutoAttack)
		                                  : (g_bar[i].kind == Slot::Cast && g_bar[i].spell == spell);
		if(here) {
			return int(i);
		}
	}
	return -1;
}

void putOnBar(size_t index, SpellType spell) {
	
	if(index == 0 || index >= BAR_SLOTS) {
		return; // key one is the swing, and is not given away
	}
	
	/*
	 * One thing does not sit on two keys. Pressing a second key for a spell
	 * already on the bar means moving it there; leaving the old copy behind
	 * would quietly fill the bar with duplicates of one spell.
	 */
	int had = barSlotOf(spell);
	if(had >= 0) {
		g_bar[size_t(had)] = Slot();
	}
	
	g_bar[index].kind = (spell == SPELL_NONE) ? Slot::AutoAttack : Slot::Cast;
	g_bar[index].spell = spell;
	saveBar();
	
}

void clearBarSlot(size_t index) {
	if(index > 0 && index < BAR_SLOTS) {
		g_bar[index] = Slot();
		saveBar();
	}
}

int barKeyPressed() {
	if(!thirdPerson()) {
		return -1;
	}
	for(size_t i = 0; i < BAR_SLOTS; i++) {
		if(GInput->actionNowPressed(MMO_ACTIONS[i])) {
			return int(i);
		}
	}
	return -1;
}

void drawBarSpell(const Rectf & rect, SpellType spell, bool castable) {
	lookForBarArt();
	drawSlotIcon(rect, spell, castable);
}

void drawBarAttack(const Rectf & rect, bool lit) {
	lookForBarArt();
	if(g_attackArt) {
		UseRenderState state(render2D().alphaCutout());
		EERIEDrawBitmap(rect, 0.0007f, g_attackArt, lit ? Color::white : Color::gray(0.55f));
	} else {
		drawSwingMark(rect, lit ? Color(232, 198, 120) : Color(120, 112, 98));
	}
}

void drawBarSocket(const Rectf & rect, size_t index) {
	
	if(index >= BAR_SLOTS) {
		return;
	}
	
	lookForBarArt();
	
	if(g_slotArt) {
		UseRenderState state(render2D().alphaCutout());
		EERIEDrawBitmap(rect, 0.0009f, g_slotArt, Color::white);
	} else {
		EERIEDrawBitmap(rect, 0.0009f, nullptr, SLOT_STONE);
	}
	
	const Slot & slot = g_bar[index];
	switch(slot.kind) {
		case Slot::Empty:
			break;
		case Slot::AutoAttack:
			drawBarAttack(rect, true);
			break;
		case Slot::Cast:
			drawSlotIcon(rect, slot.spell, true);
			break;
	}
	
}

void drawSpellDrag() {

	if(g_dragged == SPELL_NONE) {
		return;
	}

	// the same size it will be once it is dropped
	float size = slotRect(0).width();
	Rectf rect(Vec2f(DANAEMouse) - Vec2f(size * 0.5f), size, size);

	drawSlotIcon(rect, g_dragged, true);

}

} // namespace coop

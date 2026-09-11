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

#ifndef ARX_NET_COOPMMO_H
#define ARX_NET_COOPMMO_H

#include "core/Config.h"
#include "game/GameTypes.h"
#include "game/magic/Spell.h"
#include "math/Angle.h"
#include "math/Vector.h"

class Entity;

/*!
 * MMO controls: the camera behind the shoulder, a target that stays picked,
 * and a bar of twelve keys.
 *
 * None of this replaces how Arx plays. Every part of it is off unless
 * config.input.mmoMode is set, and with it off not one line here changes a
 * frame - the camera code takes its old branch, the target frame forgets
 * after a few seconds as it always did, and the number keys still belong to
 * precast. The point is a second way to play the same game, not a new game.
 */
namespace coop {

//! The whole of this file does nothing unless this is true.
[[nodiscard]] bool mmoMode();

/*!
 * True when MMO mode has taken this action's key for itself.
 *
 * The bar defaults to 1 2 3 4 5 6 7 8 9 0 - =, and the original game already
 * spends five of those on precast, cancelling a spell and turning pages, plus
 * Tab on drawing the weapon. Rather than move anybody's bindings, the handlers
 * for those actions ask this first: with MMO mode on, whoever shares a key
 * with the bar, the camera or targeting stands down for as long as it is on.
 * Rebind either side in the options and the answer follows, with no table here
 * to keep in step.
 */
[[nodiscard]] bool mmoTakesOver(ControlAction action);

// -- the camera ------------------------------------------------------------

/*!
 * Called first thing in the camera update. Returns true when this frame's
 * camera belongs to MMO third person, and releases the engine's external
 * view flag on the frame the mode is switched off.
 */
[[nodiscard]] bool beginCameraFrame();

//! True while the camera is out behind the body: the crosshair goes away, and the bow aims from the eye.
[[nodiscard]] bool thirdPerson();

/*!
 * Where the camera looks, which in third person is its own affair.
 *
 * This is the whole point of the MMO camera: the view has a heading of its
 * own instead of being another name for which way the character is facing.
 * The character can be turned to face what it is casting at, or swung round
 * by auto-attack, and the picture on screen does not move - the same
 * separation every MMO has and Arx never needed, because in first person the
 * two are necessarily the same thing.
 */
[[nodiscard]] Anglef cameraAngle();

/*!
 * The mouse is turning something this frame. True when the whole turn was
 * taken by the camera and the body must not move; false to let the engine's
 * own mouse-look run on and turn the character as well.
 *
 * \param keyTurn the turn came from the turn-left/turn-right keys rather than
 *                the mouse. Those turn the character, and the camera goes
 *                with them: a turn the player asked for by hand takes the
 *                view, a turn the game made for them (aiming a spell) does not.
 */
[[nodiscard]] bool mmoTurn(const Vec2f & rotation, bool keyTurn);

//! True while a mouse button is held for the camera: the cursor is grabbed for as long as it lasts.
[[nodiscard]] bool cameraDragging();

/*!
 * A and D turn the character instead of stepping sideways.
 *
 * True whenever the camera is out and the right button is not held, which is
 * what an MMO does and for a good reason: with the right button down the
 * mouse is already turning you, so the keys are free to be something more
 * useful. In first person Arx keeps its own strafe, untouched.
 */
[[nodiscard]] bool strafeIsTurn();

//! Both buttons held runs forward: the way an MMO lets you travel without the keyboard at all.
[[nodiscard]] bool mouseRunForward();

/*!
 * Takes over the mouse buttons while the camera is out.
 *
 * Returns true when it has dealt with them, and the engine's own handling is
 * then skipped for that frame. See the definition for why a click has to be
 * held back rather than acted on at once.
 */
bool mmoMouseButtons();

//! Where the camera goes, given the first-person eye and the way the player faces.
[[nodiscard]] Vec3f thirdPersonCameraPos(const Vec3f & eye, const Anglef & angle);

//! Remember the first-person eye, which the camera has left behind.
void notePlayerEye(const Vec3f & eye);

/*!
 * The player's eye, not the camera.
 *
 * Aiming a bow raycasts from the camera, which in third person is a couple of
 * hundred units behind the head and would send arrows off from over the
 * player's shoulder. Anything that means "where the player looks from" asks
 * for this instead.
 */
[[nodiscard]] Vec3f eyePos();

// -- the target ------------------------------------------------------------

//! Tab, or a click: the next living creature in front becomes the target.
void cycleTarget();

// -- the frame -------------------------------------------------------------

//! Once a frame before the camera: the wheel, the bar keys, auto-attack, the drag.
void updateMmo();

//! Drawn with the rest of the HUD, under the cursor and over the book.
void drawActionBar();

/*!
 * True while auto-attack wants the attack button down.
 *
 * The combat animations are driven entirely by whether the button is held -
 * hold to wind up, let go to strike - so auto-attack does not simulate a
 * swing, it holds and releases the same button a player would, and the game
 * works out the blow exactly as it always does.
 */
[[nodiscard]] bool autoAttackHeld();

// -- filling the bar -------------------------------------------------------

//! A spell was picked up off the open spell book page.
void beginSpellDrag(SpellType spell);

//! Drawn last of all, following the cursor.
void drawSpellDrag();

//! True while the cursor sits on the bar: a click there is about the bar, not about the world behind it.
[[nodiscard]] bool cursorOverActionBar();

/*!
 * The bar's left edge in pixels while it is drawn, else 0.
 *
 * The bag sits along the bottom middle of the screen and so does the bar, so
 * one of them has to move; this is what the inventory measures itself against
 * to stand beside it rather than under it.
 */
[[nodiscard]] float actionBarLeft();

} // namespace coop

#endif // ARX_NET_COOPMMO_H

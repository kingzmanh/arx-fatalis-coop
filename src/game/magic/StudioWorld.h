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

#ifndef ARX_GAME_MAGIC_STUDIOWORLD_H
#define ARX_GAME_MAGIC_STUDIOWORLD_H

#include <string>
#include <string_view>
#include <vector>

#include "io/resource/ResourcePath.h"

class Entity;

/*!
 * World edits from a text file the mod ships: data/game/studio-world.txt.
 *
 * The game's levels are Arkane's and stay untouched; this reads a list of
 * small orders of ours and applies them after a level has loaded and run its
 * scripts. "show" brings a hidden thing back into view, "put" creates an item
 * inside a body or a chest. Every line names exactly one thing, so the file
 * changes only what it says.
 */
void applyStudioWorld();

/*!
 * "keepbody <class>": a creature of that class whose death effect would burn
 * the body away keeps it instead, as a corpse that can be looted.
 */
[[nodiscard]] bool studioWorldKeepsBody(const Entity & entity);

/*!
 * A kept body just finished dying: on the next frame give it a pack with
 * what its keepbody line lists. Queued rather
 * than done on the spot, because the death effect ends inside the renderer.
 */
void studioWorldBodyKept(Entity & body);

//! Once a frame: the bodies queued by studioWorldBodyKept().
void studioWorldUpdate();

/*!
 * "wear <item class> <body mesh> [<skin from> <skin to>]": how a piece of
 * armour of that class looks on a body, overriding what its script says. For
 * pieces whose own look was never made, like the cm plate set.
 */
struct StudioWear {
	res::path mesh;
	std::string skinFrom;
	res::path skinTo;
};
[[nodiscard]] const StudioWear * studioWorldWear(std::string_view itemClass);

/*!
 * "face <texture> [<head mesh> [neck <u> <v> [<height>]]]": extra faces for
 * character creation, in file order; face number 7 is the first of them. The
 * texture is a head texture from the game's data. The mesh, when given, is
 * the head shape it was painted for (a path under graph/obj3d/interactive/npc/),
 * put on the body the way the game's own TWEAK HEAD does; without one the
 * hero's head is used. "neck" paints the lowest part of that head, so many
 * units high, with one spot of its texture: for heads painted with a collar.
 * Read on first use, before any level: the creation screen needs them.
 */
struct StudioFace {
	std::string texture;
	res::path headMesh;
	float neckU = -1.f;
	float neckV = -1.f;
	float neckHeight = 7.f;
};
[[nodiscard]] const std::vector<StudioFace> & studioWorldFaces();

#endif // ARX_GAME_MAGIC_STUDIOWORLD_H

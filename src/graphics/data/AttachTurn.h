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

#ifndef ARX_GRAPHICS_DATA_ATTACHTURN_H
#define ARX_GRAPHICS_DATA_ATTACHTURN_H

#include "graphics/GraphicsTypes.h"
#include "math/Quantizer.h"
#include "math/Types.h"

/*!
 * \brief A turn to give whatever hangs off an attach point.
 *
 * The format has no room for one. A weapon's position comes from the attach
 * point and its direction from the BONE that point rides on, and a bone has
 * only a place, never a facing of its own - so a sword's angle in a hand is
 * whatever the hero's hand animation says, on every body, for ever. That is
 * fine for the bodies Arkane modelled around it and wrong for anything else:
 * a hand held at a different angle carries the blade off with it, and no
 * amount of moving the point helps, because moving a point does not turn
 * anything.
 *
 * So the turn is kept beside the model, in a small text file the studio
 * writes - one line per point, three angles in degrees:
 *
 *     PRIMARY_ATTACH  0 -25 0
 *     WEAPON_ATTACH   0 0 90
 *
 * A model with no such file behaves exactly as before.
 */
namespace attachturn {

//! The extra turn for one attach point of one model, or nothing.
[[nodiscard]] glm::quat get(const EERIE_3DOBJ * object, VertexId point);

//! Forget what has been read, so a model edited while the game runs is re-read.
void clear();

} // namespace attachturn

#endif // ARX_GRAPHICS_DATA_ATTACHTURN_H

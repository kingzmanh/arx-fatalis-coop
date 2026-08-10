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

#ifndef ARX_NET_COOPINTERP_H
#define ARX_NET_COOPINTERP_H

#include "math/Angle.h"
#include "math/Vector.h"
#include "platform/Platform.h"

namespace coop {

//! One place something was reported to be, stamped with the reporter's clock.
struct MotionSample {
	s64 timeMs = 0;
	Vec3f pos = Vec3f(0.f);
	Anglef angle = Anglef();
};

/*!
 * A short reported-position history, drawn a step in the past.
 *
 * Nothing networked is drawn where the newest packet put it. Packets arrive
 * in beats, and a thing drawn at its newest known position stands still
 * between beats and teleports on each arrival. Instead every report is kept
 * with the reporter's own clock time, and the renderer walks through that
 * history slightly behind the present - far enough back that there is
 * normally a report on either side of the moment being drawn, so the thing
 * is always moving BETWEEN two truths, never waiting for one.
 */
struct MotionTrack {

	static constexpr size_t Capacity = 16;

	MotionSample samples[Capacity];
	size_t count = 0;
	size_t next = 0;

	//! Leftover display offset being faded out after a guess ended.
	Vec3f visualError = Vec3f(0.f);
	Vec3f lastRendered = Vec3f(0.f);
	bool wasExtrapolating = false;

	//! Chronological access; 0 is the oldest kept sample.
	[[nodiscard]] const MotionSample & at(size_t i) const {
		return samples[(next + Capacity - count + i) % Capacity];
	}

	[[nodiscard]] const MotionSample & newest() const {
		return at(count - 1);
	}

	void push(s64 timeMs, const Vec3f & pos, const Anglef & angle) {
		if(count != 0 && timeMs <= newest().timeMs) {
			return; // time only moves forward on a track
		}
		samples[next] = { timeMs, pos, angle };
		next = (next + 1) % Capacity;
		if(count < Capacity) {
			count++;
		}
	}

	void clear() {
		count = 0;
		next = 0;
		visualError = Vec3f(0.f);
		wasExtrapolating = false;
	}

};

//! Wrap-aware shortest-arc blend between two angles in degrees.
[[nodiscard]] float lerpDegrees(float a, float b, float t);

/*!
 * Where to draw the tracked thing at renderTime, on the reporter's clock.
 *
 * Interpolates between the two recorded samples around renderTime. When the
 * next report is late it briefly keeps going the way the thing was going -
 * a walk does not stop because a packet did - and when real reports resume,
 * the difference between the guess and the truth is faded out over the next
 * fraction of a second instead of popping.
 *
 * \param frameMs this frame's duration, for the fade.
 * \return false if the track holds no samples yet.
 */
bool sampleSmoothed(MotionTrack & track, s64 renderTime, float frameMs,
                    Vec3f & pos, Anglef & angle);

} // namespace coop

#endif // ARX_NET_COOPINTERP_H

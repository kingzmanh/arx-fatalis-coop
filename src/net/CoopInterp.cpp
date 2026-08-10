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

#include "net/CoopInterp.h"

#include <algorithm>
#include <cmath>

namespace coop {

float lerpDegrees(float a, float b, float t) {
	float diff = MAKEANGLE(b - a + 180.f) - 180.f;
	return MAKEANGLE(a + diff * t);
}

/*!
 * Spline through four positions: motion follows the true arc instead of
 * cutting a tiny corner at every sample. Standard Catmull-Rom.
 */
static Vec3f catmullRom(const Vec3f & p0, const Vec3f & p1, const Vec3f & p2, const Vec3f & p3,
                        float t) {
	float t2 = t * t;
	float t3 = t2 * t;
	return 0.5f * ((2.f * p1) + (-p0 + p2) * t
	             + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2
	             + (-p0 + 3.f * p1 - 3.f * p2 + p3) * t3);
}

static Anglef blendAngle(const Anglef & a, const Anglef & b, float t) {
	return Anglef(lerpDegrees(a.getPitch(), b.getPitch(), t),
	              lerpDegrees(a.getYaw(), b.getYaw(), t),
	              lerpDegrees(a.getRoll(), b.getRoll(), t));
}

bool sampleSmoothed(MotionTrack & track, s64 renderTime, float frameMs,
                    Vec3f & pos, Anglef & angle) {

	if(track.count == 0) {
		return false;
	}

	const MotionSample & oldest = track.at(0);
	const MotionSample & newest = track.newest();

	bool extrapolating = false;

	if(renderTime <= oldest.timeMs) {
		// Only just started hearing about this thing.
		pos = oldest.pos;
		angle = oldest.angle;
	} else if(renderTime >= newest.timeMs) {
		/*
		 * The next report is late. For a short moment keep going the way they
		 * were going, but only a short moment: beyond it, standing still is
		 * honester than guessing.
		 */
		pos = newest.pos;
		angle = newest.angle;
		if(track.count >= 2) {
			const MotionSample & prev = track.at(track.count - 2);
			s64 span = newest.timeMs - prev.timeMs;
			if(span > 0) {
				constexpr s64 MaxExtrapolationMs = 250;
				float aheadMs = float(std::min(renderTime - newest.timeMs, MaxExtrapolationMs));
				Vec3f velocity = (newest.pos - prev.pos) / float(span);
				pos = newest.pos + velocity * aheadMs;
				extrapolating = true;
			}
		}
	} else {
		// Somewhere in recorded history: find the two reports around the
		// drawn moment and stand between them.
		size_t after = 1;
		while(track.at(after).timeMs < renderTime) {
			after++;
		}
		const MotionSample & a = track.at(after - 1);
		const MotionSample & b = track.at(after);
		s64 span = b.timeMs - a.timeMs;
		float t = (span > 0) ? float(renderTime - a.timeMs) / float(span) : 1.f;
		t = glm::clamp(t, 0.f, 1.f);

		// Curve through the neighbouring samples when the beat is steady; a
		// spline over wildly uneven gaps bends the path instead of smoothing
		// it, and then a straight line is the honest choice.
		bool curved = false;
		if(after >= 2 && after + 1 < track.count && span > 0) {
			const MotionSample & p0 = track.at(after - 2);
			const MotionSample & p3 = track.at(after + 1);
			s64 gapBefore = a.timeMs - p0.timeMs;
			s64 gapAfter = p3.timeMs - b.timeMs;
			if(gapBefore > 0 && gapBefore < span * 3 && gapAfter > 0 && gapAfter < span * 3) {
				pos = catmullRom(p0.pos, a.pos, b.pos, p3.pos, t);
				curved = true;
			}
		}
		if(!curved) {
			pos = glm::mix(a.pos, b.pos, t);
		}

		angle = blendAngle(a.angle, b.angle, t);
	}

	/*
	 * When a guess ends, the real curve rarely lands exactly where the guess
	 * left off. Fold the difference into a display offset and fade it out, so
	 * what would have been a visible pop becomes a movement no one notices.
	 */
	if(track.wasExtrapolating && !extrapolating) {
		track.visualError = track.lastRendered - pos;
	}
	track.wasExtrapolating = extrapolating;

	if(track.visualError != Vec3f(0.f)) {
		track.visualError *= std::exp(-frameMs * 0.01f); // gone in ~0.3s
		if(glm::length(track.visualError) < 0.5f) {
			track.visualError = Vec3f(0.f);
		}
		pos += track.visualError;
	}

	track.lastRendered = pos;

	return true;
}

} // namespace coop

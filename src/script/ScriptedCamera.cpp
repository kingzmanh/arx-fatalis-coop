/*
 * Copyright 2011-2022 Arx Libertatis Team (see the AUTHORS file)
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
/* Based on:
===========================================================================
ARX FATALIS GPL Source Code
Copyright (C) 1999-2010 Arkane Studios SA, a ZeniMax Media company.

This file is part of the Arx Fatalis GPL Source Code ('Arx Fatalis Source Code'). 

Arx Fatalis Source Code is free software: you can redistribute it and/or modify it under the terms of the GNU General Public 
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Arx Fatalis Source Code is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied 
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Arx Fatalis Source Code.  If not, see 
<http://www.gnu.org/licenses/>.

In addition, the Arx Fatalis Source Code is also subject to certain additional terms. You should have received a copy of these 
additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Arx 
Fatalis Source Code. If not, please request a copy in writing from Arkane Studios at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing Arkane Studios, c/o 
ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

#include "script/ScriptedCamera.h"

#include "ai/Paths.h"
#include "core/Core.h"
#include "core/GameTime.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Camera.h"
#include "game/effect/Quake.h"
#include "graphics/Math.h"
#include "graphics/data/Mesh.h"
#include "graphics/effects/Fade.h"
#include "gui/Interface.h"
#include "scene/Interactive.h"
#include "net/CoopNet.h"
#include "net/CoopPlayer.h"

#include "script/ScriptUtils.h"


namespace script {

namespace {

class CameraActivateCommand : public Command {
	
public:
	
	CameraActivateCommand() : Command("cameraactivate") { }
	
	Result execute(Context & context) override {
		
		std::string target = context.getWord();
		
		DebugScript(' ' << target);
		
		/*
		 * The view itself, which is most of what a cutscene IS.
		 *
		 * Bars, dead controls and hidden hands were already handed to whoever
		 * the scene belongs to; this was not, so a scene declined here still
		 * took this screen's eyes and pointed them at the show. Everything
		 * else about it said "not yours" and the camera said otherwise, which
		 * on screen is the only vote that counts.
		 */
		bool ours = coop::presentsCutscene();

		if(target == "none") {
			if(coop::relaysCutscene() || coop::partnerCameraEntity()) {
				coop::reportCutsceneCamera(nullptr);
			}
			if(ours) {
				g_cameraEntity = nullptr;
			}
			return Success;
		}

		Entity * t = entities.getById(target, context.getEntity());
		if(!t || !(t->ioflags & IO_CAMERA)) {
			return Failed;
		}

		/*
		 * Resolved first, and only then sent.
		 *
		 * Cameras name themselves: the script that takes the view is the
		 * camera's own, and it says CAMERAACTIVATE SELF. "self" means whoever
		 * is running - which on the machine receiving it is nobody at all, so
		 * the word has to be turned into a name that means the same thing on
		 * both sides before it leaves.
		 */

		if(coop::relaysCutscene()) {
			coop::reportCutsceneCamera(t);
		}

		if(ours) {
			g_cameraEntity = t;
		}

		return Success;
	}
	
};

class CameraSmoothingCommand : public Command {
	
public:
	
	CameraSmoothingCommand() : Command("camerasmoothing", IO_CAMERA) { }
	
	Result execute(Context & context) override {
		
		float smoothing = context.getFloat();
		
		DebugScript(' ' << smoothing);
		
		context.getEntity()->_camdata->smoothing = smoothing;

		if(context.getEntity() == coop::partnerCameraEntity()) {
			coop::reportCutsceneCamera(context.getEntity());
		}

		return Success;
	}
	
};

class CinemascopeCommand : public Command {
	
public:
	
	CinemascopeCommand() : Command("cinemascope") { }
	
	Result execute(Context & context) override {
		
		bool smooth = false;
		HandleFlags("s") {
			if(flg & flag('s')) {
				smooth = true;
			}
		}
		
		bool enable = context.getBool();

		if(enable) {
			// bars going up on a scene from a cause-less run: the player who
			// walked up owns it (no-op otherwise)
			coop::adoptProximitySceneOwner(context.getEntity());
		}

		DebugScript(' ' << options << ' ' << enable);

		// The bars come down as part of the same locked story moment the guest
		// cannot run its way out of - see the note on SET_PLAYER_CONTROLS. They
		// are raised again at the end of a chain that never reaches a replica,
		// so a guest that lowered them would be left looking through them. The
		// viewer copy the host sends brings its own bars, and takes them away.
		// Bars come down for the audience only. Raising them is never refused:
		// giving the screen back can not be the thing that strands anyone.
		if(!coop::presentsCutscene()) {
			coop::noteCutsceneForPartner(enable);
			return Success;
		}

		// While a scene of ours plays over there, the screen is not ours to
		// redress: our own copy of the same script would raise the bars the
		// moment the other machine brought them down.
		if(coop::isSceneHeld()) {
			return Success;
		}

		if(enable && coop::isReplica()) {
			return Success;
		}

		cinematicBorder.set(enable, smooth);

		return Success;
	}
	
};

class CameraFocalCommand : public Command {
	
public:
	
	CameraFocalCommand() : Command("camerafocal", IO_CAMERA) { }
	
	Result execute(Context & context) override {
		
		float focal = glm::clamp(context.getFloat(), 100.f, 800.f);
		
		DebugScript(' ' << focal);
		
		context.getEntity()->_camdata->cam.focal = focal;
		
		return Success;
	}
	
};

class CameraTranslateTargetCommand : public Command {
	
public:
	
	CameraTranslateTargetCommand() : Command("cameratranslatetarget", IO_CAMERA) { }
	
	Result execute(Context & context) override {
		
		float x = context.getFloat();
		float y = context.getFloat();
		float z = context.getFloat();
		
		DebugScript(' ' << x << ' ' << y << ' ' << z);
		
		context.getEntity()->_camdata->translatetarget = Vec3f(x, y, z);

		if(context.getEntity() == coop::partnerCameraEntity()) {
			coop::reportCutsceneCamera(context.getEntity());
		}

		return Success;
	}
	
};

class WorldFadeCommand : public Command {
	
public:
	
	WorldFadeCommand() : Command("worldfade") { }
	
	Result execute(Context & context) override {
		
		std::string inout = context.getWord();
		const PlatformDuration duration = std::chrono::duration<float, std::milli>(context.getFloat());
		
		/*
		 * The fade dresses the screen of whoever is travelling. When the
		 * other player trips it, darkening THIS screen would black out a
		 * player who is going nowhere; their machine runs its own curtain.
		 */
		if(coop::isPartnerScriptContext()) {
			/*
			 * ...and for a fade OUT, their machine must also stop simulating
			 * their fall NOW, exactly as the level load is about to stop ours.
			 * This is what keeps the other player from finishing a fall the
			 * level designers assumed nobody could finish.
			 */
			if(inout == "out") {
				coop::sendTravelHold(s32(toMsi(duration)));
			} else if(inout == "in") {
				// Fading back in means whatever this was, it was not a travel -
				// let them go now instead of waiting for the safety timeout.
				coop::sendTravelCancel();
			}
			return Success;
		}
		
		if(inout == "out") {
			
			Color3f color;
			color.r = context.getFloat();
			color.g = context.getFloat();
			color.b = context.getFloat();
			fadeSetColor(color);
			
			fadeRequestStart(FadeType_Out, duration);
			
			DebugScript(" out " << toMsf(duration) << ' ' << color.r << ' ' << color.g << ' ' << color.b);
		} else if(inout == "in") {
			
			fadeRequestStart(FadeType_In, duration);
			
			DebugScript(" in " << toMsf(duration));
		} else {
			ScriptWarning << "unexpected fade direction: " << inout;
			return Failed;
		}
		
		return Success;
	}
	
};

class QuakeCommand : public Command {
	
public:
	
	QuakeCommand() : Command("quake") { }
	
	Result execute(Context & context) override {
		
		float intensity = context.getFloat();
		GameDuration duration = std::chrono::duration<float, std::milli>(context.getFloat());
		float period = context.getFloat();
		
		DebugScript(' ' << intensity << ' ' << toMsf(duration) << ' ' << period);
		
		AddQuakeFX(intensity, duration, period, true);
		
		return Success;
	}
	
};

} // anonymous namespace

void setupScriptedCamera() {
	
	ScriptEvent::registerCommand(std::make_unique<CameraActivateCommand>());
	ScriptEvent::registerCommand(std::make_unique<CameraSmoothingCommand>());
	ScriptEvent::registerCommand(std::make_unique<CinemascopeCommand>());
	ScriptEvent::registerCommand(std::make_unique<CameraFocalCommand>());
	ScriptEvent::registerCommand(std::make_unique<CameraTranslateTargetCommand>());
	ScriptEvent::registerCommand(std::make_unique<WorldFadeCommand>());
	ScriptEvent::registerCommand(std::make_unique<QuakeCommand>());
	
}

} // namespace script

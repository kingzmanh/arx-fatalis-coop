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

#include "script/ScriptedControl.h"

#include <cstdlib>

#include "ai/Anchors.h"

#include "core/Core.h"
#include "core/GameTime.h"
#include "game/EntityManager.h"
#include "physics/Attractors.h"
#include "physics/Collisions.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourcePath.h"
#include "scene/GameSound.h"
#include "net/CoopNet.h"
#include "scene/Interactive.h"
#include "scene/LinkedObject.h"
#include "script/ScriptUtils.h"
#include "cinematic/CinematicController.h"
#include "window/RenderWindow.h"
#include "core/Application.h"
#include <sstream>
#include "game/Player.h"


extern bool GLOBAL_MAGIC_MODE;

namespace script {

namespace {

class ActivatePhysicsCommand : public Command {
	
public:
	
	ActivatePhysicsCommand() : Command("activatephysics", AnyEntity) { }
	
	Result execute(Context & context) override {
		
		DebugScript("");
		
		ARX_INTERACTIVE_ActivatePhysics(*context.getEntity());
		
		return Success;
	}
	
};

class AttractorCommand : public Command {
	
public:
	
	AttractorCommand() : Command("attractor") { }
	
	Result execute(Context & context) override {
		
		std::string target = context.getWord();
		
		std::string power = context.getWord();
		
		float val = 0.f;
		float radius = 0.f;
		
		if(power != "off") {
			val = context.getFloatVar(power);
			radius = context.getFloat();
		}
		
		DebugScript(' ' << target << ' ' << val << ' ' << radius);
		
		if(Entity * entity = entities.getById(target, context.getEntity())) {
			ARX_SPECIAL_ATTRACTORS_Add(*entity, val * 0.01f, radius);
			return Success;
		} else {
			ScriptWarning << "Cannot add or remove attractor for non-existent target " << target;
			return Failed;
		}
		
	}
	
};

class AmbianceCommand : public Command {
	
public:
	
	AmbianceCommand() : Command("ambiance") { }
	
	Result execute(Context & context) override {
		
		float volume = 1.f;
		SoundLoopMode loop = ARX_SOUND_PLAY_LOOPED;
		
		HandleFlags("nv") {
			if(flg & flag('v')) {
				volume = context.getFloat() * 0.01f;
			}
			if(flg & flag('n')) {
				loop = ARX_SOUND_PLAY_ONCE;
			}
		}
		
		res::path ambiance = res::path::load(context.getWord());
		DebugScript(' ' << options << ' ' << volume << ' ' << ambiance);
		
		if(ambiance == "kill") {
			if(!options.empty()) {
				ScriptError << "flags cannot be used with ambiance kill";
				return Failed;
			}
			ARX_SOUND_KillAmbiances();
			return Success;
		}
		
		if(!ARX_SOUND_PlayScriptAmbiance(ambiance, loop, volume)) {
			ScriptWarning << "unable to find " << ambiance;
			return Failed;
		}
		
		return Success;
	}
	
};

class AnchorBlockCommand : public Command {
	
public:
	
	AnchorBlockCommand() : Command("anchorblock", AnyEntity) { }
	
	Result execute(Context & context) override {
		
		bool blocked = context.getBool();
		
		DebugScript(' ' << blocked);
		
		ANCHOR_BLOCK_By_IO(context.getEntity(), blocked);
		
		return Success;
	}
	
};

class AttachCommand : public Command {
	
public:
	
	AttachCommand() : Command("attach") { }
	
	Result execute(Context & context) override {
		
		std::string slaveId = context.getWord();
		Entity * slave = entities.getById(slaveId, context.getEntity());
		
		std::string slaveVertex = context.getWord(); // source action_point
		
		std::string masterId = context.getWord();
		Entity * master = entities.getById(masterId, context.getEntity());
		
		std::string masterVertex = context.getWord();
		
		DebugScript(' ' << slaveId << ' ' << slaveVertex << ' ' << masterId << ' ' << masterVertex);
		
		if(arx_unlikely(!master || !master->obj || !slave || !slave->obj)) {
			ScriptWarning << "Cannot link " << slaveId << " to " << masterId << ": missing object";
			return Failed;
		}
		
		linkEntities(*master, masterVertex, *slave, slaveVertex);
		
		return Success;
	}
	
};

class CineCommand : public Command {
	
public:
	
	CineCommand() : Command("cine") { }
	
	Result execute(Context & context) override {
		
		bool preload = false;
		HandleFlags("p") {
			if(flg & flag('p')) {
				preload = true;
			}
		}
		
		std::string name = context.getWord();

		DebugScript(' ' << options << " \"" << name << '"');

		/*
		 * ARX_NO_CINE=1 turns every cinematic into a no-op.
		 *
		 * Level 10 is the set the opening film was shot on: the cursor entity
		 * it places runs "CINE INTRODUCTION" the moment it spawns, so arriving
		 * there plays the intro and the level ends with it. With this set you
		 * can stand in the place instead of watching the film made in it.
		 */
		// "kill" and "play" are how a cinematic is stopped and resumed - swallowing
		// those would leave a film that cannot be ended rather than one that never
		// starts. Only an actual film is refused.
		if(g_noCinematics && name != "kill" && name != "play") {
			LogWarning << "[nocine] skipping cinematic \"" << name << '"';
			return Success;
		}

		if(name == "kill") {
			cinematicKill();
		} else if(name == "play") {
			cinematicRequestStart();
		} else {
			
			if(g_resources->getFile(res::path("graph/interface/illustrations") / (name + ".cin"))) {
				cinematicPrepare(name + ".cin", preload);
			} else {
				ScriptWarning << "unable to find cinematic \"" << name << '"';
				return Failed;
			}
		}
		
		return Success;
	}
	
};

class SetGroupCommand : public Command {
	
public:
	
	SetGroupCommand() : Command("setgroup", AnyEntity) { }
	
	Result execute(Context & context) override {
		
		bool rem = false;
		HandleFlags("r") {
			rem = test_flag(flg, 'r');
		}
		
		std::string group = context.getStringVar(context.getWord());
		
		DebugScript(' ' << options << ' ' << group);
		
		Entity & io = *context.getEntity();
		if(group == "door") {
			if(rem) {
				io.gameFlags &= ~GFLAG_DOOR;
			} else {
				io.gameFlags |= GFLAG_DOOR;
			}
		}
		
		if(group.empty()) {
			ScriptWarning << "missing group";
			return Failed;
		}
		
		if(rem) {
			io.groups.erase(group);
		} else {
			io.groups.insert(group);
		}
		
		return Success;
	}
	
};

class ZoneParamCommand : public Command {
	
public:
	
	ZoneParamCommand() : Command("zoneparam") { }
	
	Result execute(Context & context) override {
		
		std::string command = context.getWord();
		
		if(command == "ambiance") {
			
			res::path ambiance = res::path::load(context.getWord());
			
			DebugScript(" ambiance " << ambiance);
			
			bool ret = ARX_SOUND_PlayZoneAmbiance(ambiance);
			if(!ret) {
				ScriptWarning << "unable to find ambiance " << ambiance;
			}
			
		} else {
			ScriptWarning << "unknown command: " << command;
			return Failed;
		}
		
		return Success;
	}
	
};

class MagicCommand : public Command {
	
public:
	
	MagicCommand() : Command("magic") { }
	
	Result execute(Context & context) override {
		
		GLOBAL_MAGIC_MODE = context.getBool();
		
		DebugScript(' ' << GLOBAL_MAGIC_MODE);
		
		return Success;
	}
	
};

class DetachCommand : public Command {
	
public:
	
	DetachCommand() : Command("detach") { }
	
	Result execute(Context & context) override {
		
		std::string slaveId = context.getWord(); // source IO
		std::string masterId = context.getWord(); // target IO
		
		DebugScript(' ' << slaveId << ' ' << masterId);
		
		Entity * slave = entities.getById(slaveId, context.getEntity());
		if(!slave || !slave->obj) {
			ScriptWarning << "unknown source: " << slaveId;
			return Failed;
		}
		
		Entity * master = entities.getById(masterId, context.getEntity());
		if(!master || !master->obj) {
			ScriptWarning << "unknown target: " << masterId;
			return Failed;
		}
		
		if(slave->owner() == master) {
			unlinkEntity(*slave);
		}
		
		if(slave->show != SHOW_FLAG_ON_PLAYER &&
		   slave->show != SHOW_FLAG_LINKED &&
		   slave->show != SHOW_FLAG_IN_INVENTORY) {
			slave->show = SHOW_FLAG_IN_SCENE;
		}
		
		return Success;
	}
	
};

} // anonymous namespace

/*!
 * "replayscenes" - let every story moment happen again.
 *
 * A scene plays once per playthrough and is written down, which is right for
 * playing and useless for testing: the first attempt eats the thing the second
 * needs. Load a save from before the scene, type this on BOTH machines, and it
 * can be walked into again.
 */
class ReplayScenesCommand : public Command {

public:

	ReplayScenesCommand() : Command("replayscenes") { }

	Result execute(Context & /* context */) override {
		coop::forgetCutscenes();
		return Success;
	}

};

class NoCineCommand : public Command {

public:

	NoCineCommand() : Command("nocine") { }

	Result execute(Context & context) override {

		std::string arg = context.getWord();

		if(arg == "on" || arg == "1") {
			g_noCinematics = true;
		} else if(arg == "off" || arg == "0") {
			g_noCinematics = false;
		} else if(arg.empty() || arg == "toggle") {
			g_noCinematics = !g_noCinematics;
		} else {
			ScriptWarning << "expected on, off or toggle";
			return Failed;
		}

		// Levels decide what to create as they load, so this takes effect on the
		// next one - which is the point: "nocine on" then travel, and the place
		// arrives without the film crew.
		LogWarning << "[nocine] cinematics are now "
		           << (g_noCinematics ? "OFF - takes effect on the next level load"
		                              : "ON - back to normal");

		return Success;
	}

};

/*!
 * "here"      - remember where I am standing
 * "here back" - take me back there, across a level change if need be
 *
 * Travel lands on markers, and there is no marker where a player happens to be,
 * so going somewhere to look at it is a one way trip unless the exact spot is
 * kept. This keeps it.
 */
class HereCommand : public Command {

public:

	HereCommand() : Command("here") { }

	Result execute(Context & context) override {

		std::string arg = context.getWord();

		if(arg.empty() || arg == "set" || arg == "save") {
			g_rememberedSpot.area = g_currentArea;
			g_rememberedSpot.pos = player.pos;
			g_rememberedSpot.yaw = player.angle.getYaw();
			g_rememberedSpot.valid = true;
			// Built as a command you can keep: several spots can be collected this
			// way, where "here back" only ever remembers the last one. It also
			// goes straight to the clipboard, so it can be pasted into notes -
			// reading coordinates off a console and typing them back is exactly
			// the sort of thing that gets a digit wrong.
			std::ostringstream line;
			line << "warp " << u32(g_currentArea) << ' '
			     << long(player.pos.x) << ' ' << long(player.pos.y) << ' '
			     << long(player.pos.z) << ' ' << long(player.angle.getYaw());

			bool copied = false;
			if(mainApp && mainApp->getWindow()) {
				mainApp->getWindow()->setClipboardText(line.str());
				copied = true;
			}

			LogWarning << "[here] " << line.str()
			           << (copied ? "   (copied to clipboard)" : "   (clipboard unavailable)");
			return Success;
		}

		if(arg == "back" || arg == "return") {

			if(!g_rememberedSpot.valid) {
				ScriptWarning << "nothing remembered yet - type \"here\" first";
				return Failed;
			}

			if(g_rememberedSpot.area == g_currentArea) {
				// Same level: no need to reload anything, just stand there again.
				player.pos = g_rememberedSpot.pos;
				player.desiredangle.setYaw(g_rememberedSpot.yaw);
				player.angle.setYaw(g_rememberedSpot.yaw);
				LogWarning << "[here] back to " << player.pos.x << ',' << player.pos.y
				           << ',' << player.pos.z << " (same level)";
			} else {
				g_rememberedSpot.pending = true;
				g_teleportToArea = g_rememberedSpot.area;
				TELEPORT_TO_POSITION.clear();      // no marker; the spot decides
				TELEPORT_TO_ANGLE = long(g_rememberedSpot.yaw);
				CHANGE_LEVEL_ICON = ChangeLevelNow;
				LogWarning << "[here] travelling back to area " << u32(g_rememberedSpot.area);
			}

			return Success;
		}

		ScriptWarning << "expected nothing, or \"back\"";
		return Failed;
	}

};

/*!
 * "warp <area> <x> <y> <z> [yaw]" - stand exactly there, in that level.
 *
 * The stock teleport can only land on a MARKER, and levels have markers only
 * where the designers put doors. This takes the coordinates themselves, so any
 * spot can be written down and returned to later - which is what "here" prints.
 */
class WarpCommand : public Command {

public:

	WarpCommand() : Command("warp") { }

	Result execute(Context & context) override {

		float area = context.getFloat();
		float x = context.getFloat();
		float y = context.getFloat();
		float z = context.getFloat();
		float yaw = context.getFloat();

		if(area < 0.f) {
			ScriptWarning << "usage: warp <area> <x> <y> <z> [yaw]";
			return Failed;
		}

		AreaId destination = AreaId(u32(area));
		Vec3f position(x, y, z);

		if(destination == g_currentArea) {
			// Already here: no reload, just stand there.
			player.pos = position;
			player.desiredangle.setYaw(yaw);
			player.angle.setYaw(yaw);
			LogWarning << "[warp] moved to " << x << ',' << y << ',' << z;
		} else {
			g_rememberedSpot.pos = position;
			g_rememberedSpot.yaw = yaw;
			g_rememberedSpot.pending = true;   // arrival uses the spot, not a marker
			g_teleportToArea = destination;
			TELEPORT_TO_POSITION.clear();
			TELEPORT_TO_ANGLE = long(yaw);
			CHANGE_LEVEL_ICON = ChangeLevelNow;
			LogWarning << "[warp] travelling to area " << u32(destination) << " at "
			           << x << ',' << y << ',' << z;
		}

		return Success;
	}

};

void setupScriptedControl() {
	
	ScriptEvent::registerCommand(std::make_unique<ActivatePhysicsCommand>());
	ScriptEvent::registerCommand(std::make_unique<NoCineCommand>());
	ScriptEvent::registerCommand(std::make_unique<ReplayScenesCommand>());
	ScriptEvent::registerCommand(std::make_unique<HereCommand>());
	ScriptEvent::registerCommand(std::make_unique<WarpCommand>());
	ScriptEvent::registerCommand(std::make_unique<AttractorCommand>());
	ScriptEvent::registerCommand(std::make_unique<AmbianceCommand>());
	ScriptEvent::registerCommand(std::make_unique<AnchorBlockCommand>());
	ScriptEvent::registerCommand(std::make_unique<AttachCommand>());
	ScriptEvent::registerCommand(std::make_unique<CineCommand>());
	ScriptEvent::registerCommand(std::make_unique<SetGroupCommand>());
	ScriptEvent::registerCommand(std::make_unique<ZoneParamCommand>());
	ScriptEvent::registerCommand(std::make_unique<MagicCommand>());
	ScriptEvent::registerCommand(std::make_unique<DetachCommand>());
	
}

} // namespace script

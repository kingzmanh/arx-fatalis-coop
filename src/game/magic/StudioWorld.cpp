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

#include "game/magic/StudioWorld.h"

#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/Core.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Inventory.h"
#include "io/log/Logger.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourcePath.h"
#include "net/CoopNet.h"
#include "scene/Interactive.h"
#include "script/Script.h"
#include "util/String.h"

static const res::path g_studioWorldFile = "game/studio-world.txt";

static res::path classPathOf(std::string_view text); // defined with the item orders below

//! Creature classes whose bodies stay when their death effect would remove them, and what the body carries.
//! The body keeps its own look whatever is taken from it: a creature's armour is its mesh, not a layer.
static std::map<std::string, std::vector<std::string>> g_keepBodies;

//! Bodies that just finished dying, to be given their pack on the next frame.
static std::vector<EntityHandle> g_bodiesToDress;

//! Extra faces, in file order; read once, on first use.
static std::vector<StudioFace> g_faces;
static bool g_facesRead = false;

const std::vector<StudioFace> & studioWorldFaces() {
	if(!g_facesRead && g_resources) {
		g_facesRead = true;
		std::istringstream lines(g_resources->read(g_studioWorldFile));
		std::string line;
		while(std::getline(lines, line)) {
			size_t hash = line.find('#');
			if(hash != std::string::npos) {
				line.erase(hash);
			}
			std::istringstream words(line);
			std::string order, texture, mesh;
			if(!(words >> order >> texture) || util::toLowercase(order) != "face") {
				continue;
			}
			StudioFace & face = g_faces.emplace_back();
			face.texture = util::toLowercase(texture);
			if(words >> mesh) {
				// As a script writes it: the name of a mesh, .teo, found as .ftl
				face.headMesh = res::path::load("graph/obj3d/interactive/npc/" + util::toLowercase(mesh));
				if(face.headMesh.ext().empty()) {
					face.headMesh.append(".teo");
				}
			}
			std::string neck;
			if(words >> neck && util::toLowercase(neck) == "neck") {
				words >> face.neckU >> face.neckV;
				words >> face.neckHeight;
			}
		}
	}
	return g_faces;
}

//! How pieces of armour look on a body, by item class, where the file says so.
static std::map<std::string, StudioWear> g_wear;

const StudioWear * studioWorldWear(std::string_view itemClass) {
	if(g_wear.empty()) {
		return nullptr;
	}
	auto found = g_wear.find(util::toLowercase(std::string(itemClass)));
	return found == g_wear.end() ? nullptr : &found->second;
}

bool studioWorldKeepsBody(const Entity & entity) {
	return !g_keepBodies.empty() && g_keepBodies.count(std::string(entity.className())) != 0;
}

void studioWorldBodyKept(Entity & body) {
	g_bodiesToDress.push_back(body.index());
}

static void fillBody(Entity & body) {

	if(!body.inventory) {
		body.inventory = std::make_unique<Inventory>(&body, Vec2s(3, 11));
	}
	auto listed = g_keepBodies.find(std::string(body.className()));
	if(listed == g_keepBodies.end() || coop::isGuest()) {
		return; // a guest gets the pack's contents with the rest of the world
	}
	for(const std::string & what : listed->second) {
		Entity * item = AddItem(classPathOf(what));
		if(!item) {
			LogWarning << "studio world: could not create " << what << " for " << body.idString();
			continue;
		}
		item->scriptload = 1;
		SendInitScriptEvent(item);
		if(!body.inventory->insert(item)) {
			item->destroy();
		}
	}

}

void studioWorldUpdate() {
	if(g_bodiesToDress.empty()) {
		return;
	}
	std::vector<EntityHandle> bodies;
	bodies.swap(g_bodiesToDress);
	for(EntityHandle handle : bodies) {
		if(Entity * body = entities.get(handle)) {
			fillBody(*body);
		}
	}
}

/*
 * Every rune is one class; which rune it is comes from a text variable its
 * own script reads when it starts. The sigil is the script language's mark
 * for a local text variable, one byte, as the parser expects it.
 */
static const char * const RuneNameVar = "\xa3" "rune_name";
static const char * const RuneClass = "graph/obj3d/interactive/items/magic/rune_aam/rune_aam";

//! A class path as the file may write it: full, or short of the common root.
static res::path classPathOf(std::string_view text) {
	std::string path = util::toLowercase(std::string(text));
	for(char & c : path) {
		if(c == '\\') {
			c = '/';
		}
	}
	if(path.rfind("graph/", 0) != 0) {
		// Short of the root: "items/armor/x/x", or shorter still, "armor/x/x"
		bool hasKind = false;
		for(const char * kind : { "items/", "npc/", "fix_inter/", "system/", "player/" }) {
			if(path.rfind(kind, 0) == 0) {
				hasKind = true;
			}
		}
		path = "graph/obj3d/interactive/" + std::string(hasKind ? "" : "items/") + path;
	}
	return res::path::load(path);
}

//! Whether the container already holds one of these, so a reload does not add another.
static bool alreadyInside(Entity & container, const res::path & classPath, std::string_view runeName) {
	for(auto slot : container.inventory->slotsInGrid()) {
		Entity * held = slot.entity;
		if(!held || held->classPath() != classPath) {
			continue;
		}
		if(runeName.empty() || GETVarValueText(held->m_variables, RuneNameVar) == runeName) {
			return true;
		}
	}
	return false;
}

static bool showEntity(std::string_view id) {
	Entity * entity = entities.getById(id);
	if(!entity) {
		LogWarning << "studio world: nothing called " << id << " in this level";
		return false;
	}
	if(entity->show != SHOW_FLAG_HIDDEN && entity->show != SHOW_FLAG_MEGAHIDE) {
		return false; // already in view
	}
	// What the script command "objecthide ... no" does
	entity->gameFlags &= ~GFLAG_MEGAHIDE;
	entity->show = SHOW_FLAG_IN_SCENE;
	entity->updateOwner();
	return true;
}

static bool putInside(std::string_view containerId, std::string_view what, std::string_view runeName) {
	Entity * container = entities.getById(containerId);
	if(!container) {
		LogWarning << "studio world: nothing called " << containerId << " in this level";
		return false;
	}
	if(!container->inventory) {
		LogWarning << "studio world: " << containerId << " has no inventory to put things in";
		return false;
	}
	res::path classPath = runeName.empty() ? classPathOf(what) : res::path::load(RuneClass);
	if(alreadyInside(*container, classPath, runeName)) {
		return false;
	}
	Entity * item = AddItem(classPath);
	if(!item) {
		LogWarning << "studio world: could not create " << classPath;
		return false;
	}
	item->scriptload = 1;
	if(!runeName.empty()) {
		// Before its scripts run: the rune reads this to become the rune it is
		SETVarValueText(item->m_variables, RuneNameVar, std::string(runeName));
	}
	SendInitScriptEvent(item);
	if(!container->inventory->insert(item)) {
		LogWarning << "studio world: no room in " << containerId << " for " << classPath;
		item->destroy();
		return false;
	}
	return true;
}

void applyStudioWorld() {

	std::string text = g_resources ? g_resources->read(g_studioWorldFile) : std::string();
	if(text.empty()) {
		return;
	}

	long here = g_currentArea ? long(g_currentArea.handleData()) : -1;
	long level = -1; // -1: the orders that follow apply in any level
	size_t shown = 0, put = 0, lineNo = 0;

	std::istringstream lines(text);
	std::string line;
	while(std::getline(lines, line)) {
		lineNo++;
		size_t hash = line.find('#');
		if(hash != std::string::npos) {
			line.erase(hash);
		}
		std::istringstream words(line);
		std::vector<std::string> word;
		for(std::string w; words >> w;) {
			word.push_back(w);
		}
		if(word.empty()) {
			continue;
		}
		std::string order = util::toLowercase(word[0]);
		if(order == "level" && word.size() >= 2) {
			level = std::atol(word[1].c_str());
			continue;
		}
		if(order == "keepbody" && word.size() >= 2) {
			// Not tied to a level: the class is the same wherever it dies
			std::vector<std::string> & carries = g_keepBodies[util::toLowercase(word[1])];
			carries.assign(word.begin() + 2, word.end());
			continue;
		}
		if(order == "face") {
			continue; // read by studioWorldFaces(), before any level
		}
		if(order == "wear" && word.size() >= 3) {
			// Not tied to a level either: a piece looks the same wherever it is worn
			StudioWear & wear = g_wear[util::toLowercase(word[1])];
			wear.mesh = res::path::load(word[2]);
			if(word.size() >= 5) {
				wear.skinFrom = util::toLowercase(word[3]);
				wear.skinTo = res::path::load(word[4]);
			}
			continue;
		}
		if(level != -1 && level != here) {
			continue;
		}
		if(order == "show" && word.size() >= 2) {
			if(showEntity(util::toLowercase(word[1]))) {
				shown++;
			}
			continue;
		}
		if(order == "put" && word.size() >= 3) {
			/*
			 * Created on the machine whose world it is. A guest gets it with the
			 * rest of the world; made on both, it would exist twice.
			 */
			if(coop::isGuest()) {
				continue;
			}
			bool rune = (word.size() >= 4 && util::toLowercase(word[2]) == "rune");
			std::string_view what = rune ? std::string_view(word[3]) : std::string_view(word[2]);
			if(putInside(util::toLowercase(word[1]), what, rune ? util::toLowercase(std::string(what)) : "")) {
				put++;
			}
			continue;
		}
		LogWarning << "studio world: line " << lineNo << " not understood: " << line;
	}

	if(shown || put) {
		LogInfo << "studio world: level " << here << ": " << shown << " shown, " << put << " put";
	}

}

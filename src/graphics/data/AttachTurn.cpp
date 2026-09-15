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

#include "graphics/data/AttachTurn.h"

#include <map>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "io/log/Logger.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourcePath.h"
#include "util/String.h"

namespace attachturn {

namespace {

//! One model's turns, by the name of the point they belong to.
typedef std::map<std::string, glm::quat> Turns;

std::map<std::string, Turns> g_read;

const Turns & turnsFor(const res::path & model) {

	std::string key = model.string();
	auto known = g_read.find(key);
	if(known != g_read.end()) {
		return known->second;
	}

	Turns turns;

	/*
	 * Beside the model, named after it - but a model does not know where it
	 * was read from. EERIE_3DOBJ::file is the name written INSIDE the file,
	 * which is the .teo name the level editor used, and ARX_FTL_Load is what
	 * puts "game/" in front of it and swaps the extension for .ftl. So the
	 * same two steps have to happen here, and the bare name is tried as well
	 * for anything that does not follow that shape.
	 */
	std::string text;
	res::path bare = res::path(model).set_ext("attach");
	for(const res::path & file : { (res::path("game") / bare), bare }) {
		if(PakFile * entry = g_resources->getFile(file)) {
			text = entry->read();
			LogInfo << "attach turns: reading " << file;
			break;
		}
	}

	for(std::string_view line : util::splitIgnoreEmpty(text, '\n')) {

		std::string_view trimmed = util::trim(line);
		if(trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
			continue;
		}

		std::string name;
		float pitch = 0.f, yaw = 0.f, roll = 0.f;
		int field = 0;
		for(std::string_view word : util::splitIgnoreEmpty(trimmed, ' ')) {
			std::string piece(word);
			switch(field) {
				case 0: name = util::toLowercase(piece); break;
				case 1: pitch = float(std::atof(piece.c_str())); break;
				case 2: yaw = float(std::atof(piece.c_str())); break;
				case 3: roll = float(std::atof(piece.c_str())); break;
				default: break;
			}
			field++;
		}
		if(name.empty() || field < 4) {
			continue;
		}

		glm::quat turn = glm::quat(glm::vec3(glm::radians(pitch), glm::radians(yaw),
		                                     glm::radians(roll)));
		turns[name] = turn;
	}

	if(!turns.empty()) {
		LogInfo << "attach turns: " << turns.size() << " for " << model;
	}

	return g_read.emplace(std::move(key), std::move(turns)).first->second;
}

} // anonymous namespace

glm::quat get(const EERIE_3DOBJ * object, VertexId point) {

	if(!object || !point) {
		return quat_identity();
	}

	const Turns & turns = turnsFor(object->file);
	if(turns.empty()) {
		return quat_identity();
	}

	for(const EERIE_ACTIONLIST & action : object->actionlist) {
		if(action.idx == point) {
			auto found = turns.find(util::toLowercase(action.name));
			if(found != turns.end()) {
				return found->second;
			}
			break;
		}
	}

	return quat_identity();
}

void clear() {
	g_read.clear();
}

} // namespace attachturn

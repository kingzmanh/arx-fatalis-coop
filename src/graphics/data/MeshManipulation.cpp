/*
 * Copyright 2011-2022 Arx Libertatis Team (see the AUTHORS file)
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
// Code: Cyril Meynier
//
// Copyright (c) 1999-2000 ARKANE Studios SA. All rights reserved

#include "graphics/data/MeshManipulation.h"

#include <stddef.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "game/Entity.h"

#include "graphics/BaseGraphicsTypes.h"
#include "graphics/GraphicsTypes.h"
#include "graphics/Vertex.h"
#include "graphics/data/Mesh.h"
#include "graphics/data/TextureContainer.h"

#include "io/resource/PakReader.h"
#include "io/log/Logger.h"
#include "game/EntityManager.h"

#include "math/Types.h"
#include "math/Vector.h"

#include "platform/Platform.h"

#include "scene/Object.h"

void EERIE_MESH_TWEAK_Skin(EERIE_3DOBJ * obj, const res::path & s1, const res::path & s2) {
	
	LogDebug("Tweak Skin " << s1 << " " << s2);
	
	if(obj == nullptr || s1.empty() || s2.empty()) {
		LogError << "Tweak Skin got NULL Pointer";
		return;
	}
	
	LogDebug("Tweak Skin " << s1 << " " << s2);
	
	res::path skintochange = "graph/obj3d/textures" / s1;
	
	res::path skinname = "graph/obj3d/textures" / s2;
	TextureContainer * tex = TextureContainer::Load(skinname);
	if(!tex) {
		return;
	}
	
	if(obj->originalMaterials.empty()) {
		obj->originalMaterials.reserve(obj->materials.size());
		for(TextureContainer * texture : obj->materials) {
			obj->originalMaterials.emplace_back(texture ? texture->m_texName : std::string_view());
		}
	}
	
	arx_assert(obj->originalMaterials.size() == obj->materials.size());
	
	bool found = false;
	
	for(MaterialId id : obj->materials.handles()) {
		if(obj->originalMaterials[id] == skintochange) {
			obj->materials[id] = tex;
			found = true;
		}
	}
	
	if(found) {
		return;
	}
	
	for(TextureContainer * & texture : obj->materials) {
		if(texture->m_texName == skintochange) {
			texture = tex;
		}
	}
	
}

bool IsInSelection(const EERIE_3DOBJ * obj, VertexId vert, VertexSelectionId tw) {
	
	if(!obj || !tw) {
		return false;
	}
	
	const EERIE_SELECTIONS & sel = obj->selections[tw];
	
	return std::find(sel.selected.begin(), sel.selected.end(), vert) != sel.selected.end();
}

/*
 * Finding a vertex by where it is, without walking the whole list to do it.
 *
 * Every one of these lookups was a scan from the front, which is nothing on
 * the bodies Arkane built - the hero is 703 vertices - and ruinous on a
 * generated one. Building a body of 43,000 vertices asked for roughly a
 * thousand million position comparisons and stopped the game for a second
 * every time armour was put on.
 *
 * Three things have to be reproduced exactly, and all three are easy to get
 * wrong:
 *
 *   - WHICH match wins. The scan returned the FIRST vertex at a spot, so the
 *     index keeps them in the order they arrived and hands back the front of
 *     the list, never whichever the container happens to hold first.
 *   - ALL the matches, for addVertexToGroup, which does not stop at the first.
 *     So each place keeps a list, not a single id.
 *   - Negative zero and NaN. -0.f == 0.f is true but their bits differ, so a
 *     key made of raw bits would split a seam that used to join; the sign is
 *     flattened before keying. NaN is equal to nothing, itself included, so a
 *     place holding one is never indexed and never matches - which is exactly
 *     what the scan did.
 */
class PlaceIndex {
	
public:
	
	void add(Vec3f where, VertexId vertex) {
		if(isNaN(where)) {
			return;   // never matched before, must never match now
		}
		m_at[key(where)].push_back(vertex);
	}
	
	//! The first vertex at that place, as a scan from the front would find it.
	[[nodiscard]] VertexId first(Vec3f where) const {
		const std::vector<VertexId> * here = all(where);
		return here ? here->front() : VertexId();
	}
	
	//! Every vertex at that place, in the order they were added.
	[[nodiscard]] const std::vector<VertexId> * all(Vec3f where) const {
		if(isNaN(where)) {
			return nullptr;
		}
		auto found = m_at.find(key(where));
		return (found == m_at.end()) ? nullptr : &found->second;
	}
	
	//! Faces are matched by the three places they join, the same way.
	[[nodiscard]] bool hasFace(Vec3f a, Vec3f b, Vec3f c) const {
		return m_faces.find(faceKey(a, b, c)) != m_faces.end();
	}
	
	void addFace(Vec3f a, Vec3f b, Vec3f c) {
		m_faces.insert(faceKey(a, b, c));
	}
	
private:
	
	typedef std::array<u32, 3> Key;
	typedef std::array<Key, 3> FaceKey;
	
	static bool isNaN(Vec3f where) {
		return where.x != where.x || where.y != where.y || where.z != where.z;
	}
	
	static u32 bits(float value) {
		if(value == 0.f) {
			value = 0.f;   // -0.f compares equal to 0.f, so it must key the same
		}
		u32 out = 0;
		std::memcpy(&out, &value, sizeof(out));
		return out;
	}
	
	static Key key(Vec3f where) {
		return { bits(where.x), bits(where.y), bits(where.z) };
	}
	
	static FaceKey faceKey(Vec3f a, Vec3f b, Vec3f c) {
		return { key(a), key(b), key(c) };
	}
	
	std::map<Key, std::vector<VertexId>> m_at;
	std::set<FaceKey> m_faces;
	
};

/*
 * The old way, kept on purpose.
 *
 * Small objects are built BOTH ways and the answers compared, so the index is
 * checked against the thing it replaced every time the hero or an NPC puts on
 * armour - which is the code path the whole shipped game depends on. On a body
 * small enough for this to be free, being right matters more than being quick.
 */
static const size_t VerifyBelow = 4000;

static VertexId findVertexSlowly(const EERIE_3DOBJ & obj, Vec3f vertex) {
	
	for(VertexId index : obj.vertexlist.handles()) {
		if(obj.vertexlist[index].v == vertex) {
			return index;
		}
	}
	
	return { };
}

static VertexId getEquivalentVertex(const EERIE_3DOBJ & obj, const PlaceIndex & index, Vec3f vertex) {
	
	VertexId found = index.first(vertex);
	
	if(obj.vertexlist.size() <= VerifyBelow) {
		VertexId honest = findVertexSlowly(obj, vertex);
		if(found != honest) {
			LogWarning << "[tweak] the vertex index disagreed with the scan at ("
			           << vertex.x << ", " << vertex.y << ", " << vertex.z
			           << "); keeping the scan's answer";
			return honest;
		}
	}
	
	return found;
}

static VertexId addVertex(EERIE_3DOBJ & obj, PlaceIndex & index, const EERIE_VERTEX & vertex) {
	
	if(VertexId found = getEquivalentVertex(obj, index, vertex.v)) {
		return found;
	}
	
	obj.vertexlist.push_back(vertex);
	index.add(vertex.v, obj.vertexlist.last());
	
	return obj.vertexlist.last();
}

static long ObjectAddFace(EERIE_3DOBJ * obj, PlaceIndex & index, const EERIE_FACE * face,
                          const EERIE_3DOBJ * srcobj) {
	
	Vec3f a = srcobj->vertexlist[face->vid[0]].v;
	Vec3f b = srcobj->vertexlist[face->vid[1]].v;
	Vec3f c = srcobj->vertexlist[face->vid[2]].v;
	
	// Check Already existing faces
	bool known = index.hasFace(a, b, c);
	
	if(obj->facelist.size() <= VerifyBelow) {
		bool honest = false;
		for(const EERIE_FACE & existing : obj->facelist) {
			if(obj->vertexlist[existing.vid[0]].v == a && obj->vertexlist[existing.vid[1]].v == b
			   && obj->vertexlist[existing.vid[2]].v == c) {
				honest = true;
				break;
			}
		}
		if(known != honest) {
			LogWarning << "[tweak] the face index disagreed with the scan; keeping the scan";
			known = honest;
		}
	}
	
	if(known) {
		return -1;
	}
	
	EERIE_FACE & newface = obj->facelist.emplace_back(*face);
	newface.vid[0] = addVertex(*obj, index, srcobj->vertexlist[face->vid[0]]);
	newface.vid[1] = addVertex(*obj, index, srcobj->vertexlist[face->vid[1]]);
	newface.vid[2] = addVertex(*obj, index, srcobj->vertexlist[face->vid[2]]);
	index.addFace(a, b, c);
	newface.material = MaterialId(0);
	
	for(MaterialId material : obj->materials.handles()) {
		if(face->material && size_t(face->material) < srcobj->materials.size()
		   && obj->materials[material] == srcobj->materials[face->material]) {
			newface.material = material;
			break;
		}
	}
	
	return obj->facelist.size() - 1;
}

static void addNamedVertex(EERIE_3DOBJ & obj, PlaceIndex & index, std::string_view name,
                           const EERIE_VERTEX & vertex) {
	
	VertexId newvert = addVertex(obj, index, vertex);
	
	for(const EERIE_ACTIONLIST & action : obj.actionlist) {
		if(action.name == name) {
			return;
		}
	}
	
	EERIE_ACTIONLIST & action = obj.actionlist.emplace_back();
	action.name = name;
	action.idx = newvert;
	
}

MaterialId addMaterial(EERIE_3DOBJ & obj, TextureContainer * material) {
	
	if(!material) {
		return { };
	}
	
	for(MaterialId id : obj.materials.handles()) {
		if(obj.materials[id] == material) {
			return id;
		}
	}
	
	obj.materials.push_back(material);
	
	return obj.materials.last();
}

static void addVertexToGroup(EERIE_3DOBJ & obj, const PlaceIndex & index, VertexGroup & group,
                             const EERIE_VERTEX & vertex) {
	
	// This one does NOT stop at the first: a place shared by several vertices
	// puts all of them in the group, and the index hands them back in the
	// order they were added, which is the order a scan would have met them.
	const std::vector<VertexId> * here = index.all(vertex.v);
	
	if(obj.vertexlist.size() <= VerifyBelow) {
		std::vector<VertexId> honest;
		for(VertexId candidate : obj.vertexlist.handles()) {
			if(obj.vertexlist[candidate].v == vertex.v) {
				honest.push_back(candidate);
			}
		}
		const std::vector<VertexId> empty;
		if((here ? *here : empty) != honest) {
			LogWarning << "[tweak] the vertex index disagreed with the scan over a shared place";
		}
	}
	
	if(!here) {
		return;
	}
	
	for(VertexId found : *here) {
		if(std::find(group.indexes.begin(), group.indexes.end(), found) == group.indexes.end()) {
			group.indexes.push_back(found);
		}
	}
	
}

static void copySelection(const EERIE_3DOBJ & source, VertexSelectionId sourceSelection,
                          EERIE_3DOBJ & dest, const PlaceIndex & index,
                          VertexSelectionId destSelection) {
	
	for(VertexId sourceVertex : source.selections[sourceSelection].selected) {
		if(VertexId destVertex = getEquivalentVertex(dest, index, source.vertexlist[sourceVertex].v)) {
			auto & selection = dest.selections[destSelection].selected;
			if(std::find(selection.begin(), selection.end(), destVertex) == selection.end()) {
				selection.push_back(destVertex);
			}
		}
	}
	
}

static std::unique_ptr<EERIE_3DOBJ> CreateIntermediaryMesh(const EERIE_3DOBJ * obj1, const EERIE_3DOBJ * obj2,
                                                           TweakFlag tw) {
	
	VertexSelectionId sel_head1;
	VertexSelectionId sel_head2;
	VertexSelectionId sel_torso1;
	VertexSelectionId sel_torso2;
	VertexSelectionId sel_legs1;
	VertexSelectionId sel_legs2;
	
	// First we retreive selection groups indexes
	for(VertexSelectionId selection : obj1->selections.handles()) {
		if(obj1->selections[selection].name == "head") {
			sel_head1 = selection;
		} else if(obj1->selections[selection].name == "chest") {
			sel_torso1 = selection;
		} else if(obj1->selections[selection].name == "leggings") {
			sel_legs1 = selection;
		}
	}
	
	for(VertexSelectionId selection : obj2->selections.handles()) {
		if(obj2->selections[selection].name == "head") {
			sel_head2 = selection;
		} else if(obj2->selections[selection].name == "chest") {
			sel_torso2 = selection;
		} else if(obj2->selections[selection].name == "leggings") {
			sel_legs2 = selection;
		}
	}
	
	if(!sel_head1 || !sel_head2 || !sel_torso1 || !sel_torso2 || !sel_legs1 || !sel_legs2) {
		return nullptr;
	}
	
	VertexSelectionId tw1;
	VertexSelectionId tw2;
	VertexSelectionId iw1;
	VertexSelectionId jw1;
	
	if(tw == TWEAK_HEAD) {
		tw1 = sel_head1;
		tw2 = sel_head2;
		iw1 = sel_torso1;
		jw1 = sel_legs1;
	}
	
	if(tw == TWEAK_TORSO) {
		tw1 = sel_torso1;
		tw2 = sel_torso2;
		iw1 = sel_head1;
		jw1 = sel_legs1;
	}
	
	if(tw == TWEAK_LEGS) {
		tw1 = sel_legs1;
		tw2 = sel_legs2;
		iw1 = sel_torso1;
		jw1 = sel_head1;
	}
	
	if(!tw1 || !tw2) {
		return { };
	}
	
	if(!getNamedVertex(obj1, "head2chest") ||
	   !getNamedVertex(obj2, "head2chest") ||
	   !getNamedVertex(obj1, "chest2leggings") ||
	   !getNamedVertex(obj2, "chest2leggings")) {
		return { };
	}
	
	// Work will contain the Tweaked object
	std::unique_ptr<EERIE_3DOBJ> work = std::make_unique<EERIE_3DOBJ>();
	PlaceIndex index;   // where every vertex of work is, so nothing has to be scanned for
	
	// Linked objects are linked to this object.
	if(obj1->linked.size() > obj2->linked.size()) {
		work->linked = obj1->linked;
	} else {
		work->linked = obj2->linked;
	}
	
	// Is the origin of object in obj1 or obj2 ? Retreives it for work object
	if(IsInSelection(obj1, obj1->origin, tw1)) {
		work->origin = addVertex(*work, index, obj2->vertexlist[obj2->origin]);
	} else {
		work->origin = addVertex(*work, index, obj1->vertexlist[obj1->origin]);
	}
	
	// Recreate Action Points included in work object.for Obj1
	for(size_t i = 0; i < obj1->actionlist.size(); i++) {
		const EERIE_ACTIONLIST & action = obj1->actionlist[i];
		if(IsInSelection(obj1, action.idx, iw1) ||
		   IsInSelection(obj1, action.idx, jw1) ||
		   action.name == "head2chest" ||
		   action.name == "chest2leggings") {
			addNamedVertex(*work, index, action.name, obj1->vertexlist[action.idx]);
		}
	}
	
	// Do the same for Obj2
	for(size_t i = 0; i < obj2->actionlist.size(); i++) {
		const EERIE_ACTIONLIST & action = obj2->actionlist[i];
		if(IsInSelection(obj2, action.idx, tw2) ||
		   action.name == "head2chest" ||
		   action.name == "chest2leggings") {
			addNamedVertex(*work, index, action.name, obj2->vertexlist[action.idx]);
		}
	}
	
	// Recreate Vertex using Obj1 Vertexes
	for(VertexId vertex : obj1->vertexlist.handles()) {
		if(IsInSelection(obj1, vertex, iw1) || IsInSelection(obj1, vertex, jw1)) {
			addVertex(*work, index, obj1->vertexlist[vertex]);
		}
	}
	
	// The same for Obj2
	for(VertexId vertex : obj2->vertexlist.handles()) {
		if(IsInSelection(obj2, vertex, tw2)) {
			addVertex(*work, index, obj2->vertexlist[vertex]);
		}
	}
	
	// Look in Faces for forgotten Vertexes... AND
	// Re-Create TextureContainers Infos
	// We look for texturecontainers included in the future tweaked object
	TextureContainer * tc = nullptr;
	for(const EERIE_FACE & face : obj1->facelist) {
		if((IsInSelection(obj1, face.vid[0], iw1) || IsInSelection(obj1, face.vid[0], jw1)) &&
		   (IsInSelection(obj1, face.vid[1], iw1) || IsInSelection(obj1, face.vid[1], jw1)) &&
		   (IsInSelection(obj1, face.vid[2], iw1) || IsInSelection(obj1, face.vid[2], jw1))) {
			if(face.material && tc != obj1->materials[face.material]) {
				tc = obj1->materials[face.material];
				addMaterial(*work, tc);
			}
			ObjectAddFace(work.get(), index, &face, obj1);
		}
	}
	
	for(const EERIE_FACE & face : obj2->facelist) {
		if(IsInSelection(obj2, face.vid[0], tw2) ||
		   IsInSelection(obj2, face.vid[1], tw2) ||
		   IsInSelection(obj2, face.vid[2], tw2)) {
			if(face.material && tc != obj2->materials[face.material]) {
				tc = obj2->materials[face.material];
				addMaterial(*work, tc);
			}
			ObjectAddFace(work.get(), index, &face, obj2);
		}
	}
	
	// Recreate Groups
	work->grouplist.resize(std::max(obj1->grouplist.size(), obj2->grouplist.size()));
	
	for(VertexGroupId group : obj1->grouplist.handles()) {
		work->grouplist[group].name = obj1->grouplist[group].name;
		if(VertexId vertex = getEquivalentVertex(*work, index, obj1->vertexlist[obj1->grouplist[group].origin].v)) {
			work->grouplist[group].m_blobShadowSize = obj1->grouplist[group].m_blobShadowSize;
			if(IsInSelection(obj1, obj1->grouplist[group].origin, iw1) ||
			   IsInSelection(obj1, obj1->grouplist[group].origin, jw1)) {
				work->grouplist[group].origin = vertex;
			}
		}
	}
	
	for(VertexGroupId group : obj2->grouplist.handles()) {
		if(size_t(group) >= obj1->grouplist.size()) {
			work->grouplist[group].name = obj2->grouplist[group].name;
		}
		if(VertexId vertex = getEquivalentVertex(*work, index, obj2->vertexlist[obj2->grouplist[group].origin].v)) {
			work->grouplist[group].m_blobShadowSize = obj2->grouplist[group].m_blobShadowSize;
			if(IsInSelection(obj2, obj2->grouplist[group].origin, tw2)) {
				work->grouplist[group].origin = vertex;
			}
		}
	}
	
	// Recreate Selection Groups (only the 3 selections needed to reiterate mesh tweaking)
	work->selections.resize(3);
	work->selections[VertexSelectionId(0)].name = "head";
	work->selections[VertexSelectionId(1)].name = "chest";
	work->selections[VertexSelectionId(2)].name = "leggings";
	if(tw == TWEAK_HEAD) {
		copySelection(*obj2, sel_head2, *work, index, VertexSelectionId(0));
	} else {
		copySelection(*obj1, sel_head1, *work, index, VertexSelectionId(0));
	}
	if(tw == TWEAK_TORSO) {
		copySelection(*obj2, sel_torso2, *work, index, VertexSelectionId(1));
	} else {
		copySelection(*obj1, sel_torso1, *work, index, VertexSelectionId(1));
	}
	if(tw == TWEAK_LEGS) {
		copySelection(*obj2, sel_legs2, *work, index, VertexSelectionId(2));
	} else {
		copySelection(*obj1, sel_legs1, *work, index, VertexSelectionId(2));
	}
	
	// Now recreates other selections...
	for(VertexSelectionId selection : obj1->selections.handles()) {
		
		if(EERIE_OBJECT_GetSelection(work.get(), obj1->selections[selection].name)) {
			continue;
		}
		
		work->selections.emplace_back().name = obj1->selections[selection].name;
		copySelection(*obj1, selection, *work, index,  work->selections.last());
		
		if(VertexSelectionId ii = EERIE_OBJECT_GetSelection(obj2, obj1->selections[selection].name)) {
			copySelection(*obj2, ii, *work, index,  work->selections.last());
		}
		
	}
	
	for(VertexSelectionId selection : obj2->selections.handles()) {
		
		if(EERIE_OBJECT_GetSelection(work.get(), obj2->selections[selection].name)) {
			continue;
		}
		
		work->selections.emplace_back().name = obj2->selections[selection].name;
		copySelection(*obj2, selection, *work, index,  work->selections.last());
		
	}
	
	// Recreate Animation-groups vertex
	for(VertexGroupId group : obj1->grouplist.handles()) {
		for(VertexId vertex : obj1->grouplist[group].indexes) {
			addVertexToGroup(*work, index, work->grouplist[group], obj1->vertexlist[vertex]);
		}
	}
	
	for(VertexGroupId group : obj2->grouplist.handles()) {
		for(VertexId vertex : obj2->grouplist[group].indexes) {
			addVertexToGroup(*work, index, work->grouplist[group], obj2->vertexlist[vertex]);
		}
	}
	
	work->vertexWorldPositions.resize(work->vertexlist.size());
	work->vertexClipPositions.resize(work->vertexlist.size());
	work->vertexColors.resize(work->vertexlist.size());
	
	return work;
}

void EERIE_MESH_TWEAK_Do(Entity * io, TweakType tw, const res::path & path) {
	
	if(!io || !io->obj) {
		return;
	}
	
	
	res::path ftl_file = ("game" / path).set_ext("ftl");
	if((!g_resources->getFile(ftl_file)) && (!g_resources->getFile(path))) {
		return;
	}
	
	if(path.empty() && tw == TWEAK_REMOVE) {
		if(io->tweaky) {
			delete io->obj;
			io->obj = io->tweaky;
			EERIE_Object_Precompute_Fast_Access(io->obj);
			io->tweaky = nullptr;
		}
		return;
	}
	
	if(!(tw & (TWEAK_HEAD | TWEAK_TORSO | TWEAK_LEGS))) {
		return;
	}
	
	std::unique_ptr<EERIE_3DOBJ> tobj = loadObject(path);
	if(!tobj) {
		return;
	}
	
	std::unique_ptr<EERIE_3DOBJ> result;
	if(tw == (TWEAK_HEAD | TWEAK_TORSO | TWEAK_LEGS)) {
		
		result = std::move(tobj); // Replace the entire mesh
		
	} else {
		
		if(tw & TWEAK_HEAD) {
			result = CreateIntermediaryMesh(io->obj, tobj.get(), TWEAK_HEAD);
			if(!result) {
				return;
			}
		}
		if(tw & TWEAK_TORSO) {
			result = CreateIntermediaryMesh(result ? result.get() : io->obj, tobj.get(), TWEAK_TORSO);
			if(!result) {
				return;
			}
		}
		if(tw & TWEAK_LEGS) {
			result = CreateIntermediaryMesh(result ? result.get() : io->obj, tobj.get(), TWEAK_LEGS);
			if(!result) {
				return;
			}
		}
		
		EERIE_Object_Precompute_Fast_Access(result.get());
		
		EERIE_CreateCedricData(result.get());
		
		// TODO also do this for the other branch?
		io->animBlend.lastanimtime = 0;
		io->animBlend.m_active = false;
		
	}
	
	if(!io->tweaky) {
		io->tweaky = io->obj;
	} else if(io->tweaky != io->obj) {
		delete io->obj;
	}
	
	io->obj = result.release();
	
}

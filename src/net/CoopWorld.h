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

#ifndef ARX_NET_COOPWORLD_H
#define ARX_NET_COOPWORLD_H

#include <string>
#include <string_view>
#include <vector>

#include "graphics/BaseGraphicsTypes.h"
#include "math/Vector.h"
#include "net/CoopProtocol.h"
#include "platform/Platform.h"

class Entity;

namespace coop {

/*!
 * Replication of the shared world.
 *
 * Entities are addressed by their id string ("goblin_lord_0001") rather than by
 * their index in the entity manager. Indices are assigned in load order and are
 * reused as entities come and go, so the same index means different things on
 * the two machines within seconds of play. The id string is stable, is what the
 * savegame already keys on, and is what scripts use.
 */

//! Serialise the state of every entity in the current area worth replicating.
/*!
 * \param full when set, ignore the sent-state cache and carry every entity;
 *             otherwise carry only what changed since the last snapshot.
 */
void writeEntitySnapshot(Writer & writer, bool full);

//! Apply a snapshot from the authority. Silently ignores entities we do not have.
void readEntitySnapshot(Reader & reader, u32 serverTimeMs);

/*!
 * Guest: describe the replicated world as this machine sees it, so the
 * authority can audit it. Host: compare such a report against the truth,
 * log every divergence, and queue the diverged entities for resending.
 * With difference-only snapshots, a silently diverged entity would otherwise
 * never heal - this is the safety net that makes divergence impossible to
 * miss and self-repairing.
 */
void writeWorldAudit(Writer & writer);
void applyWorldAudit(Reader & reader);

//! Perform a sound or particle burst the authority's simulation produced.
void applyWorldFx(Reader & reader);

/*!
 * Glide replicated entities toward the last position the authority reported.
 *
 * Snapshots arrive some fifteen times a second while the screen refreshes four
 * times as often. Without this an approaching enemy would visibly jump from
 * one snapshot to the next; with it the frames in between are filled in, and a
 * fight on the guest's screen reads the same as on the host's.
 */
void smoothReplicatedEntities();

/*!
 * Is this item held, dragged, or still coming to rest after this player threw
 * it? Such an item is simulated here and never corrected from the other side.
 */
[[nodiscard]] bool ownsLocally(const Entity * entity);

//! Forget interpolation targets, e.g. on a level change.
void resetReplication();

/*!
 * Stop tracking an entity as part of the shared world.
 *
 * Once an item is in a player's pack it is their property, not scenery, and
 * the other machine has removed it from the world entirely. Left in the
 * replication registry it would still be listed in the periodic audit, the
 * authority would answer "that does not exist here", and the item would be
 * destroyed in its owner's hands.
 */
void forgetReplicatedEntity(std::string_view entityId);

/*!
 * Serialise the parts of the game state that represent story progress:
 * global script variables, the quest log, the keyring and map markers.
 *
 * This is what makes it one playthrough rather than two. Each machine keeps its
 * own savegame - so each player's progress lives on their own disk - but the
 * story flags are the host's and are pushed to the guest on join and whenever
 * they change.
 */
void writeGlobalState(Writer & writer);
void readGlobalState(Reader & reader);

//! Handle a guest's request to run an entity's action script.
void applyActionRequest(std::string_view entityId);

//! Handle a guest taking an item out of the shared world.
void applyTakeRequest(std::string_view entityId);

/*!
 * Handle a guest giving one of their items to a world entity.
 *
 * Runs the receiving entity's COMBINE script here, where the quest lives, in
 * the giver's name. The item is in their pack and so does not exist here; a
 * stand-in carrying the same id and class stands in for it just long enough
 * for the script to look at it, and its fate decides whether the real one
 * leaves their pack.
 */
void applyCombineRequest(std::string_view sourceId, std::string_view sourceClass,
                         std::string_view targetId);

//! The thing we offered was taken: drop our copy.
void applyCombineTaken(std::string_view sourceId);

/*!
 * Host: the other player hands gold to one of ours. Run the entity's script in
 * the giver's name with the giver's purse - the wallet it checks and the
 * wallet it charges are both theirs, and the quest state it advances is the
 * world's.
 */
void applyCombineGold(std::string_view targetId, long giverGold);

//! Handle a guest talking to a creature: it answers in their name.
void applyChatRequest(std::string_view npcId);

//! A script on the other machine gave our player an item; make it and take it.
void applyGiveItem(std::string_view classPath, s16 count);

/*!
 * Move our copy of an item the other player has just put down.
 *
 * The entity is found by id, because it is the same entity on both machines.
 * Only when we genuinely do not have it - it came from a part of the world we
 * have never loaded - is one created from the class path instead.
 */
void applyItemDropped(std::string_view entityId, std::string_view classPath, s16 count,
                      float durability, const Vec3f & at, float angleYaw,
                      const Vec3f & velocity);

/*!
 * Remember that we just changed this entity ourselves.
 *
 * Snapshots take a moment to reflect a change we made, and in that moment the
 * authority is still describing the world as it was. Applying that would undo
 * what the player just did, in front of them. Entities noted here are left
 * alone until the other side has had time to agree.
 */
void noteLocalEdit(std::string_view entityId);

//! Handle a guest's melee or spell hit on a world entity.
void applyHitRequest(std::string_view entityId, float damage, u32 damageType);

//! Apply an entity destruction announced by the authority.
void applyEntityGone(std::string_view entityId);

//! Create the mirror of an entity the authority has just spawned.
void applyEntitySpawn(std::string_view classPath, s32 instance, const Vec3f & at,
                      float angleYaw, s16 count, float durability);

//! Apply a spell the other player cast.
void applyRemoteSpell(int spellType, float level, u32 flags, s64 durationMs,
                      std::string_view targetId, std::string_view casterId);
void applyRemoteSpellEnd(int spellType);

/*!
 * True while a replicated change is being applied.
 *
 * The hooks that report local world changes to the other side check this so
 * that applying a remote change does not immediately echo it back.
 */
[[nodiscard]] bool isApplyingRemote();

//! RAII guard for the flag above.
class ApplyScope {
public:
	ApplyScope();
	~ApplyScope();
	ApplyScope(const ApplyScope &) = delete;
	ApplyScope & operator=(const ApplyScope &) = delete;
};

} // namespace coop

#endif // ARX_NET_COOPWORLD_H

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

#ifndef ARX_NET_COOPPROTOCOL_H
#define ARX_NET_COOPPROTOCOL_H

#include <stddef.h>
#include <string.h>

#include <string>
#include <string_view>
#include <vector>

#include "math/Angle.h"
#include "math/Vector.h"
#include "platform/Platform.h"

namespace coop {

/*!
 * Bumped whenever the wire format changes in a way that makes an older build
 * misread a newer one. Both sides check this during the handshake and refuse
 * the connection rather than desyncing silently later.
 */
constexpr u32 ProtocolVersion = 31;

//! Default listen port, used when the address the joining player typed has none.
constexpr unsigned short DefaultPort = 27100;

/*!
 * ENet channels.
 *
 * Snapshots are sent unreliably: a dropped one is replaced by the next one a
 * few milliseconds later, and waiting for a retransmit would show a stale
 * position instead of a fresh one. Everything that changes the world - an item
 * taken, a door opened, experience earned - goes on a reliable channel, because
 * losing one of those permanently desynchronises the two worlds.
 */
enum Channel : u8 {
	ChannelControl  = 0, //!< handshake, area changes, save transfer: reliable ordered
	ChannelSnapshot = 1, //!< avatar and entity state: unreliable, newest wins
	ChannelEvent    = 2, //!< world mutations, damage, spells, rewards: reliable
	/*
	 * Speech, on a channel of its own and never retransmitted. A lost moment of
	 * voice is a tiny gap the ear barely registers; the same moment arriving
	 * late, after the words either side of it, is worse than silence. Its own
	 * channel so a burst of talking never delays anyone's position.
	 */
	ChannelVoice    = 3,
	ChannelCount    = 4
};

enum MessageType : u8 {

	// -- handshake and session ------------------------------------------------
	MsgHello        = 1,  //!< client -> host: protocol version and player name
	MsgWelcome      = 2,  //!< host -> client: accepted, here is my name and area
	MsgReject       = 3,  //!< host -> client: refused, with a human readable reason
	MsgPing         = 4,
	MsgPong         = 5,
	MsgBye          = 6,  //!< clean disconnect notice

	// -- shared world state transfer -----------------------------------------
	MsgWorldBegin   = 10, //!< a savegame blob follows, with its total size
	MsgWorldChunk   = 11,
	MsgWorldEnd     = 12,
	MsgWorldRequest = 13, //!< client -> host: send me the world, I am (re)joining
	MsgAreaState    = 14, //!< client -> host: here is the area I just left, merge it
	MsgAreaRequest  = 15, //!< client -> host: I am entering this area, send its state

	// -- per frame replication ------------------------------------------------
	MsgAvatar       = 20, //!< the sender's own body: where it is and what it is doing
	MsgEntities     = 21, //!< host -> client: authoritative entity state for the area

	// -- world mutations -------------------------------------------------------
	MsgAction       = 30, //!< client -> host: run this entity's action script for me
	MsgTake         = 31, //!< an item left the shared world into someone's pack
	MsgDrop         = 32, //!< an item entered the shared world from someone's pack
	MsgEntityGone   = 33, //!< host -> client: this entity is destroyed
	MsgEntitySpawn  = 34, //!< host -> client: this entity now exists
	MsgDoor         = 35, //!< a door or lever changed state

	// -- combat ----------------------------------------------------------------
	MsgDamagePlayer = 40, //!< routed to the machine that owns the victim's health
	MsgHitEntity    = 41, //!< client -> host: I hit this world entity
	MsgPlayerDied   = 42,
	MsgPlayerRevive = 43,
	MsgReviveAsk    = 44, //!< a spell asks the other machine to raise its player
	MsgComeHere     = 45, //!< a spell asks the other player to be moved here

	// -- magic -----------------------------------------------------------------
	MsgSpellCast    = 50,
	MsgSpellEnd     = 51,

	// -- progression and story --------------------------------------------------
	MsgReward       = 60, //!< experience and gold earned, granted to both players
	MsgQuest        = 61, //!< a quest log entry was added
	MsgKeyring      = 62, //!< a key was added to the keyring
	MsgMapMarker    = 63,
	MsgNotify       = 64, //!< short text shown to the other player
	MsgChat         = 65,
	MsgRunes        = 66, //!< either -> other: every rune I know; magic is learned together

	// -- travel ----------------------------------------------------------------
	MsgTravel       = 70, //!< host -> guest: a door says you may travel (area, marker, angle)
	MsgTravelCancel = 71, //!< host -> guest: you stepped out of the door, forget it

	// -- trigger reporting -----------------------------------------------------
	// The guest knows the exact frame its real body crosses a trigger; waiting
	// for the host to notice the replicated body adds a third of a second. So
	// the guest reports the crossing and the host simulates it immediately -
	// events from the client, simulation on the authority.
	MsgZoneEnter    = 72, //!< guest -> host: my player just entered this zone
	MsgZoneLeave    = 73, //!< guest -> host: my player just left this zone
	MsgPlayerPush   = 74, //!< RETIRED - the push turned chasing creatures into thrusters; number stays reserved
	MsgWorldAudit   = 75, //!< guest -> host: here is what my world looks like; fix what diverged
	MsgWorldFx      = 76, //!< host -> guest: a sound or particle burst the simulation produced
	MsgTravelHold   = 77, //!< host -> guest: a travel has begun for you; freeze and fade NOW
	MsgCutsceneSeen = 78, //!< either -> other: this story sequence is consumed for BOTH of us
	MsgPartnerEffect = 79, //!< either -> other: your player receives this effect (heal, hunger, ...)
	MsgCutscenePlay = 80, //!< either -> other: watch this story speech with me, camera and all
	MsgPlayerTouchNpc = 81, //!< guest -> host: my player is pressing against this creature
	MsgVoice        = 82, //!< either -> other: a moment of speech, to come out of their mouth
	MsgLightIgnite  = 83, //!< host -> guest: a level light was lit or put out

	// -- handing things over ---------------------------------------------------
	// Giving an item to someone is how most of this game's quests actually
	// advance, and the quest lives on the authority. So the give travels, the
	// script runs there, and the two answers that matter - did they keep it,
	// and did they hand something back - travel home.
	MsgCombine      = 84, //!< guest -> host: I am giving this item to that entity
	MsgCombineTaken = 85, //!< host -> guest: they kept it; it leaves your pack
	MsgGiveItem     = 86, //!< host -> guest: a script over here gave you this
	MsgCutsceneCamera = 87, //!< either -> other: a scene that is yours is looking through this camera
	MsgSceneHold    = 88, //!< either -> other: a scene of yours has begun / ended; be still for it
	MsgSceneSkip    = 89, //!< watcher -> performer: I pressed skip; move it along
	MsgTalkTo       = 90, //!< guest -> host: I am talking to this creature; answer me
	MsgPartyFollow  = 91, //!< host -> guest: the story force-moved a player; come stand at this spot
	MsgCombineGold  = 92 //!< guest -> host: I am handing gold to that entity; here is my purse

};

//! Reasons a host can turn a joining player away.
enum RejectReason : u8 {
	RejectVersion = 1,
	RejectFull    = 2,
	RejectBusy    = 3
};

/*!
 * Little endian byte writer.
 *
 * Arx already assumes a little endian host everywhere it memcpy()s save
 * structures, so matching that here keeps one convention across the codebase
 * instead of two.
 */
class Writer {

	std::vector<u8> m_data;

public:

	explicit Writer(MessageType type) {
		m_data.reserve(64);
		put(u8(type));
	}

	void put(u8 value) { m_data.push_back(value); }

	void put(bool value) { put(u8(value ? 1 : 0)); }

	void put(s8 value) { put(u8(value)); }

	void put(u16 value) {
		m_data.push_back(u8(value & 0xff));
		m_data.push_back(u8((value >> 8) & 0xff));
	}

	void put(s16 value) { put(u16(value)); }

	void put(u32 value) {
		for(int i = 0; i < 4; i++) {
			m_data.push_back(u8((value >> (i * 8)) & 0xff));
		}
	}

	void put(s32 value) { put(u32(value)); }

	void put(u64 value) {
		for(int i = 0; i < 8; i++) {
			m_data.push_back(u8((value >> (i * 8)) & 0xff));
		}
	}

	void put(s64 value) { put(u64(value)); }

	void put(float value) {
		u32 bits;
		static_assert(sizeof(bits) == sizeof(value));
		std::memcpy(&bits, &value, sizeof(bits));
		put(bits);
	}

	void put(const Vec3f & value) {
		put(value.x);
		put(value.y);
		put(value.z);
	}

	void put(const Anglef & value) {
		put(value.getPitch());
		put(value.getYaw());
		put(value.getRoll());
	}

	//! Strings are length prefixed so they can contain anything, including nothing.
	void put(std::string_view value) {
		u16 length = u16(std::min<size_t>(value.size(), 0xffff));
		put(length);
		m_data.insert(m_data.end(), value.begin(), value.begin() + length);
	}

	void putRaw(const void * data, size_t size) {
		const u8 * bytes = static_cast<const u8 *>(data);
		m_data.insert(m_data.end(), bytes, bytes + size);
	}

	[[nodiscard]] const u8 * data() const { return m_data.data(); }
	[[nodiscard]] size_t size() const { return m_data.size(); }

};

/*!
 * Little endian byte reader.
 *
 * Every read is bounds checked and a read past the end sets the reader to a
 * failed state rather than throwing or reading garbage. A truncated or hostile
 * packet therefore yields zeroes and a false ok(), which callers check once at
 * the end instead of after every field.
 */
class Reader {

	const u8 * m_data;
	size_t m_size;
	size_t m_pos = 0;
	bool m_ok = true;

	bool take(size_t count) {
		if(!m_ok || m_pos + count > m_size) {
			m_ok = false;
			return false;
		}
		return true;
	}

public:

	Reader(const u8 * data, size_t size)
		: m_data(data)
		, m_size(size)
	{ }

	[[nodiscard]] bool ok() const { return m_ok; }

	[[nodiscard]] size_t remaining() const { return m_ok ? m_size - m_pos : 0; }

	[[nodiscard]] u8 getU8() {
		if(!take(1)) {
			return 0;
		}
		return m_data[m_pos++];
	}

	[[nodiscard]] bool getBool() { return getU8() != 0; }

	[[nodiscard]] s8 getS8() { return s8(getU8()); }

	[[nodiscard]] u16 getU16() {
		if(!take(2)) {
			return 0;
		}
		u16 value = u16(m_data[m_pos]) | u16(u16(m_data[m_pos + 1]) << 8);
		m_pos += 2;
		return value;
	}

	[[nodiscard]] s16 getS16() { return s16(getU16()); }

	[[nodiscard]] u32 getU32() {
		if(!take(4)) {
			return 0;
		}
		u32 value = 0;
		for(int i = 0; i < 4; i++) {
			value |= u32(m_data[m_pos + size_t(i)]) << (i * 8);
		}
		m_pos += 4;
		return value;
	}

	[[nodiscard]] s32 getS32() { return s32(getU32()); }

	[[nodiscard]] u64 getU64() {
		if(!take(8)) {
			return 0;
		}
		u64 value = 0;
		for(int i = 0; i < 8; i++) {
			value |= u64(m_data[m_pos + size_t(i)]) << (i * 8);
		}
		m_pos += 8;
		return value;
	}

	[[nodiscard]] s64 getS64() { return s64(getU64()); }

	[[nodiscard]] float getFloat() {
		u32 bits = getU32();
		float value;
		static_assert(sizeof(bits) == sizeof(value));
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

	[[nodiscard]] Vec3f getVec3f() {
		float x = getFloat();
		float y = getFloat();
		float z = getFloat();
		return Vec3f(x, y, z);
	}

	[[nodiscard]] Anglef getAnglef() {
		float pitch = getFloat();
		float yaw = getFloat();
		float roll = getFloat();
		return Anglef(pitch, yaw, roll);
	}

	[[nodiscard]] std::string getString() {
		size_t length = getU16();
		if(!take(length)) {
			return std::string();
		}
		std::string value(reinterpret_cast<const char *>(m_data + m_pos), length);
		m_pos += length;
		return value;
	}

	//! Hands back a view of the rest of the packet without copying it.
	[[nodiscard]] const u8 * getRaw(size_t count) {
		if(!take(count)) {
			return nullptr;
		}
		const u8 * result = m_data + m_pos;
		m_pos += count;
		return result;
	}

};

} // namespace coop

#endif // ARX_NET_COOPPROTOCOL_H

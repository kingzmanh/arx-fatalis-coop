/*
 * Copyright 2014-2022 Arx Libertatis Team (see the AUTHORS file)
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

#ifndef ARX_GAME_MAGIC_SPELL_H
#define ARX_GAME_MAGIC_SPELL_H

#include <string>
#include <string_view>

#include "game/Damage.h"
#include "graphics/BaseGraphicsTypes.h"
#include "graphics/effects/SpellEffects.h"
#include "scene/Light.h"
#include "util/Flags.h"


class TextureContainer;

// Spell list
enum SpellType {
	
	// LEVEL 1
	SPELL_MAGIC_SIGHT,           // = 11,
	SPELL_MAGIC_MISSILE,         // = 12,
	SPELL_IGNIT,                 // = 13,
	SPELL_DOUSE,                 // = 14,
	SPELL_ACTIVATE_PORTAL,       // = 15,
	
	// LEVEL 2
	SPELL_HEAL,                  // = 21,
	SPELL_DETECT_TRAP,           // = 22,
	SPELL_ARMOR,                 // = 23,
	SPELL_LOWER_ARMOR,           // = 24,
	SPELL_HARM,                  // = 25,
	
	// LEVEL 3
	SPELL_SPEED,                 // = 31,
	SPELL_DISPELL_ILLUSION,      // = 32,
	SPELL_FIREBALL,              // = 33,
	SPELL_CREATE_FOOD,           // = 34,
	SPELL_ICE_PROJECTILE,        // = 35,
	
	// LEVEL 4
	SPELL_BLESS,                 // = 41,
	SPELL_DISPELL_FIELD,         // = 42,
	SPELL_FIRE_PROTECTION,       // = 43,
	SPELL_TELEKINESIS,           // = 44,
	SPELL_CURSE,                 // = 45,
	SPELL_COLD_PROTECTION,       // = 46,
	
	// LEVEL 5
	SPELL_RUNE_OF_GUARDING,      // = 51,
	SPELL_LEVITATE,              // = 52,
	SPELL_CURE_POISON,           // = 53,
	SPELL_REPEL_UNDEAD,          // = 54,
	SPELL_POISON_PROJECTILE,     // = 55,
	
	// LEVEL 6
	SPELL_RAISE_DEAD,            // = 61,
	SPELL_PARALYSE,              // = 62,
	SPELL_CREATE_FIELD,          // = 63,
	SPELL_DISARM_TRAP,           // = 64,
	SPELL_SLOW_DOWN,             // = 65, //secret
	
	// LEVEL 7
	SPELL_FLYING_EYE,            // = 71,
	SPELL_FIRE_FIELD,            // = 72,
	SPELL_ICE_FIELD,             // = 73,
	SPELL_LIGHTNING_STRIKE,      // = 74,
	SPELL_CONFUSE,               // = 75,
	
	// LEVEL 8
	SPELL_INVISIBILITY,          // = 81,
	SPELL_MANA_DRAIN,            // = 82,
	SPELL_EXPLOSION,             // = 83,
	SPELL_ENCHANT_WEAPON,        // = 84,
	SPELL_LIFE_DRAIN,            // = 85, //secret
	
	// LEVEL 9
	SPELL_SUMMON_CREATURE,       // = 91,
	SPELL_NEGATE_MAGIC,          // = 92,
	SPELL_INCINERATE,            // = 93,
	SPELL_MASS_PARALYSE,         // = 94,
	
	// LEVEL 10
	SPELL_MASS_LIGHTNING_STRIKE, // = 101,
	SPELL_CONTROL_TARGET,        // = 102,
	SPELL_FREEZE_TIME,           // = 103,
	SPELL_MASS_INCINERATE,       // = 104
	
	
	/*
	 * Spells written down rather than compiled in.
	 *
	 * Empty until game/studio-spells.txt says otherwise, which is what
	 * lets somebody with no compiler add a spell - see StudioSpells.h.
	 */
	SPELL_STUDIO_000, SPELL_STUDIO_001, SPELL_STUDIO_002, SPELL_STUDIO_003, SPELL_STUDIO_004,
	SPELL_STUDIO_005, SPELL_STUDIO_006, SPELL_STUDIO_007, SPELL_STUDIO_008, SPELL_STUDIO_009,
	SPELL_STUDIO_010, SPELL_STUDIO_011, SPELL_STUDIO_012, SPELL_STUDIO_013, SPELL_STUDIO_014,
	SPELL_STUDIO_015, SPELL_STUDIO_016, SPELL_STUDIO_017, SPELL_STUDIO_018, SPELL_STUDIO_019,
	SPELL_STUDIO_020, SPELL_STUDIO_021, SPELL_STUDIO_022, SPELL_STUDIO_023, SPELL_STUDIO_024,
	SPELL_STUDIO_025, SPELL_STUDIO_026, SPELL_STUDIO_027, SPELL_STUDIO_028, SPELL_STUDIO_029,
	SPELL_STUDIO_030, SPELL_STUDIO_031, SPELL_STUDIO_032, SPELL_STUDIO_033, SPELL_STUDIO_034,
	SPELL_STUDIO_035, SPELL_STUDIO_036, SPELL_STUDIO_037, SPELL_STUDIO_038, SPELL_STUDIO_039,
	SPELL_STUDIO_040, SPELL_STUDIO_041, SPELL_STUDIO_042, SPELL_STUDIO_043, SPELL_STUDIO_044,
	SPELL_STUDIO_045, SPELL_STUDIO_046, SPELL_STUDIO_047, SPELL_STUDIO_048, SPELL_STUDIO_049,
	SPELL_STUDIO_050, SPELL_STUDIO_051, SPELL_STUDIO_052, SPELL_STUDIO_053, SPELL_STUDIO_054,
	SPELL_STUDIO_055, SPELL_STUDIO_056, SPELL_STUDIO_057, SPELL_STUDIO_058, SPELL_STUDIO_059,
	SPELL_STUDIO_060, SPELL_STUDIO_061, SPELL_STUDIO_062, SPELL_STUDIO_063, SPELL_STUDIO_064,
	SPELL_STUDIO_065, SPELL_STUDIO_066, SPELL_STUDIO_067, SPELL_STUDIO_068, SPELL_STUDIO_069,
	SPELL_STUDIO_070, SPELL_STUDIO_071, SPELL_STUDIO_072, SPELL_STUDIO_073, SPELL_STUDIO_074,
	SPELL_STUDIO_075, SPELL_STUDIO_076, SPELL_STUDIO_077, SPELL_STUDIO_078, SPELL_STUDIO_079,
	SPELL_STUDIO_080, SPELL_STUDIO_081, SPELL_STUDIO_082, SPELL_STUDIO_083, SPELL_STUDIO_084,
	SPELL_STUDIO_085, SPELL_STUDIO_086, SPELL_STUDIO_087, SPELL_STUDIO_088, SPELL_STUDIO_089,
	SPELL_STUDIO_090, SPELL_STUDIO_091, SPELL_STUDIO_092, SPELL_STUDIO_093, SPELL_STUDIO_094,
	SPELL_STUDIO_095, SPELL_STUDIO_096, SPELL_STUDIO_097, SPELL_STUDIO_098, SPELL_STUDIO_099,
	
	SPELL_FAKE_SUMMON,           // special =105
	
	SPELL_NONE = -1
};

constexpr size_t SPELL_TYPES_COUNT = SPELL_FAKE_SUMMON + 1;
constexpr SpellType SPELL_STUDIO_FIRST = SPELL_STUDIO_000;
constexpr SpellType SPELL_STUDIO_LAST = SPELL_STUDIO_099;

enum SpellcastFlag {
	SPELLCAST_FLAG_NODRAW         = 1 << 0,
	SPELLCAST_FLAG_NOANIM         = 1 << 1,
	SPELLCAST_FLAG_NOMANA         = 1 << 2,
	SPELLCAST_FLAG_PRECAST        = 1 << 3,
	SPELLCAST_FLAG_LAUNCHPRECAST  = 1 << 4,
	SPELLCAST_FLAG_NOCHECKCANCAST = 1 << 5,
	SPELLCAST_FLAG_NOSOUND        = 1 << 6,
	SPELLCAST_FLAG_RESTORE        = 1 << 7,
	SPELLCAST_FLAG_ORPHAN         = 1 << 8,
	SPELLCAST_FLAG_NODAMAGE       = 1 << 9,
};
DECLARE_FLAGS(SpellcastFlag, SpellcastFlags)
DECLARE_FLAGS_OPERATORS(SpellcastFlags)

class alignas(16) Spell {
	
public:
	
	// We can't use alignof(glm::mat4x4) directly because MSVC sucks
	static_assert(alignof(glm::mat4x4) <= 16, "need to increase alignment");
	
	Spell(const Spell &) = delete;
	Spell & operator=(const Spell &) = delete;
	
	Spell();
	
	virtual ~Spell() = default;
	
	virtual bool CanLaunch() {
		return true;
	}
	virtual void Launch() = 0;
	virtual void Update() { }
	virtual void End() { }
	
	virtual Vec3f getPosition() const;
	Vec3f getCasterPosition() const;
	Vec3f getTargetPosition() const;
	
	void updateCasterHand();
	void updateCasterPosition();
	
	void requestEnd() {
		m_hasDuration = true;
		m_duration = 0;
	}
	
	[[nodiscard]] std::string_view className() const noexcept;
	[[nodiscard]] std::string idString() const noexcept;
	
	SpellHandle m_thisHandle;
	s32 m_instance;
	
	EntityHandle m_caster; //!< Number of the source interactive obj (0==player)
	EntityHandle m_target; //!< Number of the target interactive obj if any
	float m_level; //!< Level of Magic 1-10
	
	VertexId m_hand_group;
	Vec3f m_hand_pos; //!< Only valid if hand_group>=0
	Vec3f m_caster_pos;
	
	SpellType m_type;
	
	GameInstant m_timcreation;
	
	bool m_hasDuration;
	GameDuration m_duration;
	GameDuration m_elapsed;
	
	float m_fManaCostPerSecond;
	
	SpellcastFlags m_flags;
	audio::SourcedSample m_snd_loop;
	
	GameDuration m_launchDuration;
	
	std::vector<EntityHandle> m_targets;
	
protected:
	
	Vec3f getTargetPos(EntityHandle source, EntityHandle target);
	
};

#endif // ARX_GAME_MAGIC_SPELL_H

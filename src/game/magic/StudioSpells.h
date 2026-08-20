/*
 * Copyright 2026 Arx Fatalis Co-op contributors
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
/*!
 * \file
 * Spells that are written down rather than compiled in.
 *
 * Every spell Arkane shipped is a class: its runes, its cost, its look and
 * what it does are all decided when the game is built. That is fine for a
 * finished game and useless for a tool - it means only someone with a
 * compiler can add a spell, which is nobody who downloads a mod.
 *
 * So the game carries a hundred empty spells. Each one asks a plain text
 * file, beside the paks like every other loose file, what it is:
 *
 *     spell frostbolt
 *     name   Frostbolt
 *     runes  AAM FRIDD COSUM
 *     level  3
 *     kind   damage
 *     amount 25
 *     radius 150
 *     types  MAGICAL COLD
 *     visual Heal
 *     sound  magic_spell_fire_launch.wav
 *
 * Nothing here can do anything the engine could not do before; it moves the
 * decisions from build time to load time, which is the whole difference
 * between a tool for the person with the source and a tool for everyone.
 *
 * Whatever the form cannot say is said in script: a cast slot sends CUSTOM
 * with its own name to the player, so an .asl block finishes the job and
 * reaches the co-op layer for free.
 */
#ifndef ARX_GAME_MAGIC_STUDIOSPELLS_H
#define ARX_GAME_MAGIC_STUDIOSPELLS_H

#include <array>
#include <string>
#include <string_view>

#include "audio/AudioTypes.h"
#include "game/Damage.h"
#include "game/magic/Rune.h"
#include "game/magic/Spell.h"
#include "game/magic/SpellRecognition.h"
#include "graphics/effects/Fissure.h"
#include "graphics/particle/ParticleSystem.h"

//! How many spells can be written down without building the game.
//! What a written-down spell costs to cast, whether or not the file says.
float studioSpellManaCost(SpellType type);

constexpr size_t STUDIO_SPELL_COUNT = size_t(SPELL_STUDIO_LAST)
                                      - size_t(SPELL_STUDIO_FIRST) + 1;

//! One spell as the file describes it. Undefined slots stay silent.
struct StudioSpellDef {

	bool defined = false;
	std::string key;                 //!< what its script block is called
	std::string name;                //!< what the book shows
	std::array<Rune, MAX_SPELL_SYMBOLS> symbols = { RUNE_NONE, RUNE_NONE,
	                                                RUNE_NONE, RUNE_NONE,
	                                                RUNE_NONE, RUNE_NONE };
	long level = 1;
	std::string kind = "custom";     //!< damage, heal, summon or custom
	DamageType damageTypes = 0;
	float amount = 0.f;
	float radius = 0.f;
	std::string visual;              //!< a particle preset, or SummonRift
	std::string sound;               //!< a wav in sfx/
	std::string icon;                //!< a texture for the book
	//! What it costs to cast. Below zero means nobody said, so charge by level.
	float mana = -1.f;
	//! Whether that number is mana, or a share of the caster's whole pool.
	bool manaIsShare = false;

};

extern std::array<StudioSpellDef, STUDIO_SPELL_COUNT> g_studioSpells;

//! True for the hundred slots, false for everything Arkane shipped.
[[nodiscard]] bool isStudioSpell(SpellType type);

//! The definition behind a slot, or a silent one if that slot is empty.
[[nodiscard]] const StudioSpellDef & studioSpell(SpellType type);

//! Read the file. Called once, before the runes and the icons are set up.
void studioSpellsLoad();

/*!
 * The one class every written-down spell uses.
 *
 * It looks at its own slot to find out what it is, which is why a hundred
 * spells need one class rather than a hundred.
 */
class StudioSpell final : public Spell {

public:

	void Launch() override;
	void End() override;
	void Update() override;

private:

	[[nodiscard]] const StudioSpellDef & def() const { return studioSpell(m_type); }

	Vec3f m_pos;
	ParticleSystem m_particles;
	CSummonCreature m_rift;
	LightHandle m_light;
	bool m_useRift = false;
	//! A partner to pull, once the rift has opened.
	bool m_askPending = false;

};

#endif // ARX_GAME_MAGIC_STUDIOSPELLS_H

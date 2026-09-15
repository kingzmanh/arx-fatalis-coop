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

#include "game/magic/StudioSpells.h"

#include <map>

#include "audio/Audio.h"
#include "core/GameTime.h"
#include "game/Damage.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/NPC.h"
#include "net/CoopPlayer.h"
#include "graphics/particle/ParticleEffects.h"
#include "scene/Interactive.h"
#include "game/Player.h"
#include "game/Spells.h"
#include "game/effect/ParticleSystems.h"
#include "graphics/Math.h"
#include "graphics/particle/ParticleParams.h"
#include "util/String.h"
#include "io/log/Logger.h"
#include "math/RandomVector.h"
#include "physics/Collisions.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourcePath.h"
#include "scene/Object.h"
#include "scene/GameSound.h"
#include "net/CoopNet.h"
#include "net/CoopWorld.h"
#include "script/Script.h"

std::array<StudioSpellDef, STUDIO_SPELL_COUNT> g_studioSpells;

static const res::path g_studioSpellFile = "game/studio-spells.txt";
static const res::path g_sfxPath = "sfx";

bool isStudioSpell(SpellType type) {
	return type >= SPELL_STUDIO_FIRST && type <= SPELL_STUDIO_LAST;
}

const StudioSpellDef & studioSpell(SpellType type) {
	static const StudioSpellDef silent;
	if(!isStudioSpell(type)) {
		return silent;
	}
	return g_studioSpells[size_t(type) - size_t(SPELL_STUDIO_FIRST)];
}

// ---------------------------------------------------------------- reading --
//
// The file names things the way a person would - AAM, COLD, Heal - so these
// three tables turn those words back into the engine's own values. A word
// that is not here is refused out loud rather than quietly ignored, because
// a spell that silently does nothing is the worst thing this could ship.

static Rune runeByName(std::string_view name) {
	static const std::map<std::string_view, Rune> runes = {
		{ "AAM", RUNE_AAM }, { "NHI", RUNE_NHI }, { "MEGA", RUNE_MEGA },
		{ "YOK", RUNE_YOK }, { "TAAR", RUNE_TAAR }, { "KAOM", RUNE_KAOM },
		{ "VITAE", RUNE_VITAE }, { "VISTA", RUNE_VISTA },
		{ "STREGUM", RUNE_STREGUM }, { "MORTE", RUNE_MORTE },
		{ "COSUM", RUNE_COSUM }, { "COMUNICATUM", RUNE_COMUNICATUM },
		{ "MOVIS", RUNE_MOVIS }, { "TEMPUS", RUNE_TEMPUS },
		{ "FOLGORA", RUNE_FOLGORA }, { "SPACIUM", RUNE_SPACIUM },
		{ "TERA", RUNE_TERA }, { "CETRIUS", RUNE_CETRIUS },
		{ "RHAA", RUNE_RHAA }, { "FRIDD", RUNE_FRIDD },
		{ "AKBAA", RUNE_AKBAA },
	};
	auto it = runes.find(name);
	return (it == runes.end()) ? RUNE_NONE : it->second;
}

static DamageType damageTypeByName(std::string_view name) {
	static const std::map<std::string_view, DamageType> types = {
		{ "GENERIC", DAMAGE_TYPE_GENERIC }, { "FIRE", DAMAGE_TYPE_FIRE },
		{ "MAGICAL", DAMAGE_TYPE_MAGICAL }, { "LIGHTNING", DAMAGE_TYPE_LIGHTNING },
		{ "COLD", DAMAGE_TYPE_COLD }, { "POISON", DAMAGE_TYPE_POISON },
		{ "GAS", DAMAGE_TYPE_GAS }, { "METAL", DAMAGE_TYPE_METAL },
		{ "WOOD", DAMAGE_TYPE_WOOD }, { "STONE", DAMAGE_TYPE_STONE },
		{ "ACID", DAMAGE_TYPE_ACID }, { "ORGANIC", DAMAGE_TYPE_ORGANIC },
		{ "DRAIN_LIFE", DAMAGE_TYPE_DRAIN_LIFE },
		{ "DRAIN_MANA", DAMAGE_TYPE_DRAIN_MANA },
		{ "PUSH", DAMAGE_TYPE_PUSH }, { "FIELD", DAMAGE_TYPE_FIELD },
		{ "FAKEFIRE", DAMAGE_TYPE_FAKEFIRE },
		{ "FAKESPELL", DAMAGE_TYPE_FAKESPELL },
		{ "NO_FIX", DAMAGE_TYPE_NO_FIX },
		{ "PER_SECOND", DAMAGE_TYPE_PER_SECOND },
	};
	auto it = types.find(name);
	return (it == types.end()) ? DamageType(0) : it->second;
}

static bool particleByName(std::string_view name, ParticleParam & out) {
	static const std::map<std::string_view, ParticleParam> looks = {
		{ "Heal", ParticleParam_Heal },
		{ "CreateFood", ParticleParam_CreateFood },
		{ "CurePoison", ParticleParam_CurePoison },
		{ "FireFieldBase", ParticleParam_FireFieldBase },
		{ "FireFieldFlame", ParticleParam_FireFieldFlame },
		{ "MagicMissileExplosion", ParticleParam_MagicMissileExplosion },
		{ "MagicMissileExplosionMar", ParticleParam_MagicMissileExplosionMar },
	};
	auto it = looks.find(name);
	if(it == looks.end()) {
		return false;
	}
	out = it->second;
	return true;
}

static std::string_view trim(std::string_view s) {
	while(!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		s.remove_prefix(1);
	}
	while(!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
		s.remove_suffix(1);
	}
	return s;
}

//! Split "runes AAM FRIDD COSUM" into its key and the rest of the line.
static std::pair<std::string_view, std::string_view> split(std::string_view line) {
	size_t at = line.find_first_of(" \t");
	if(at == std::string_view::npos) {
		return { line, std::string_view() };
	}
	return { line.substr(0, at), trim(line.substr(at)) };
}

void studioSpellsLoad() {

	for(StudioSpellDef & def : g_studioSpells) {
		def = StudioSpellDef();
	}

	std::string text = g_resources ? g_resources->read(g_studioSpellFile) : std::string();
	if(text.empty()) {
		return;                 // nobody has written any spells; nothing to do
	}

	StudioSpellDef * current = nullptr;
	size_t used = 0;
	size_t line_no = 0;

	std::string_view rest(text);
	while(!rest.empty()) {

		size_t end = rest.find('\n');
		std::string_view line = trim(rest.substr(0, end));
		rest = (end == std::string_view::npos) ? std::string_view()
		                                       : rest.substr(end + 1);
		line_no++;

		if(line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}

		auto [key, value] = split(line);

		if(key == "spell") {
			if(used >= g_studioSpells.size()) {
				LogWarning << "studio spells: no room for " << value
				           << " - the game carries " << g_studioSpells.size();
				current = nullptr;
				continue;
			}
			current = &g_studioSpells[used++];
			*current = StudioSpellDef();
			current->defined = true;
			current->key = value;
			current->name = value;
			continue;
		}

		if(!current) {
			LogWarning << "studio spells: line " << line_no
			           << " comes before any 'spell' line";
			continue;
		}

		if(key == "name") {
			current->name = value;
		} else if(key == "level") {
			current->level = std::max(1L, std::min(10L, long(std::atol(std::string(value).c_str()))));
		} else if(key == "kind") {
			current->kind = value;
		} else if(key == "reach") {
			current->reach = float(std::atof(std::string(value).c_str()));
		} else if(key == "secret") {
			current->secret = (value == "yes" || value == "true" || value == "1");
		} else if(key == "hunts") {
			current->hunts.clear();
			for(std::string_view word : util::splitIgnoreEmpty(value, ' ')) {
				current->hunts.emplace_back(word);
			}
		} else if(key == "creature") {
			current->creature = value;
		} else if(key == "seconds") {
			current->seconds = float(std::atof(std::string(value).c_str()));
		} else if(key == "amount") {
			current->amount = float(std::atof(std::string(value).c_str()));
		} else if(key == "mana") {
			// "15%" is a share of the pool; "15" is fifteen mana
			std::string amount(value);
			current->manaIsShare = !amount.empty() && amount.back() == '%';
			if(current->manaIsShare) {
				amount.pop_back();
			}
			current->mana = float(std::atof(amount.c_str()));
		} else if(key == "radius") {
			current->radius = float(std::atof(std::string(value).c_str()));
		} else if(key == "visual") {
			current->visual = value;
		} else if(key == "sound") {
			current->sound = value;
		} else if(key == "icon") {
			current->icon = value;
		} else if(key == "runes" || key == "types") {
			size_t at = 0;
			std::string_view words = value;
			while(!words.empty()) {
				size_t cut = words.find_first_of(" \t");
				std::string_view word = trim(words.substr(0, cut));
				words = (cut == std::string_view::npos) ? std::string_view()
				                                        : trim(words.substr(cut));
				if(word.empty()) {
					continue;
				}
				if(key == "runes") {
					Rune rune = runeByName(word);
					if(rune == RUNE_NONE) {
						LogWarning << "studio spells: " << current->key
						           << " asks for a rune called " << word;
					} else if(at < current->symbols.size()) {
						current->symbols[at++] = rune;
					}
				} else {
					DamageType type = damageTypeByName(word);
					if(type == 0) {
						LogWarning << "studio spells: " << current->key
						           << " asks for a damage type called " << word;
					} else {
						current->damageTypes |= type;
					}
				}
			}
		} else {
			LogWarning << "studio spells: " << current->key
			           << " has a line this game does not understand: " << key;
		}
	}

	LogInfo << "studio spells: " << used << " read from " << g_studioSpellFile;
}

/*!
 * What it costs to cast, in mana.
 *
 * The file may say a number, or a share of the pool with a percent sign after
 * it. When it says nothing, twice the spell's level: the game's own one-shot
 * spells sit on that line - heal 4 at level 2, cure poison 10 at level 5,
 * raise dead 12 at level 6 - and a spell that costs nothing is not a spell,
 * it is a button.
 *
 * A share is measured against the pool's MAXIMUM. Against what is left it
 * could always be paid - the pool halves and halves and never empties - and
 * the whole point of a cost is that it can run out.
 *
 * The pool is the player's. The other machine launches a copy of every spell
 * its partner casts and those copies are told not to charge; the cast was
 * already paid for where it happened.
 */
float studioSpellManaCost(SpellType type) {

	if(!isStudioSpell(type)) {
		return 0.f;
	}

	const StudioSpellDef & spell = studioSpell(type);
	if(!spell.defined) {
		return 0.f;
	}

	if(spell.mana < 0.f) {
		return float(spell.level) * 2.f;
	}

	if(spell.manaIsShare) {
		return player.manaPool.max * spell.mana * 0.01f;
	}

	return spell.mana;
}

// ---------------------------------------------------------------- casting --

/*
 * How long the ground stands open before anything comes out of it. Raise Dead
 * waits three seconds for its own, and a creature that appears the instant the
 * words are said reads as having nothing to do with the picture.
 */
static const GameDuration SUMMON_DELAY = 3s;

/*
 * The arc has to be told which spell owns it when it is made, because that
 * is how it finds the caster to leave the damage under.
 */
StudioSpell::StudioSpell()
	: m_bolt(this)
{ }

//! Where a creature's heart is, for a bolt to be thrown from or at.
static Vec3f chestOf(Entity * entity) {
	
	if(!entity) {
		return Vec3f(0.f);
	}
	
	if(entity == entities.player()) {
		return player.pos + Vec3f(0.f, 70.f, 0.f);
	}
	
	if(VertexGroupId chest = EERIE_OBJECT_GetGroup(entity->obj, "chest")) {
		return entity->obj->vertexWorldPositions[entity->obj->grouplist[chest].origin].v;
	}
	
	return entity->pos + Vec3f(0.f, -120.f, 0.f);
	
}

//! What this spell is being thrown at: what it was cast on, or what the
//! caster is fighting.
Entity * StudioSpell::quarry() const {
	
	Entity * caster = entities.get(m_caster);
	Entity * mark = entities.get(m_target);
	
	if((!mark || mark == caster) && caster && (caster->ioflags & IO_NPC)) {
		mark = entities.get(caster->targetinfo);
	}
	
	return (mark == caster) ? nullptr : mark;
	
}

void StudioSpell::Launch() {

	const StudioSpellDef & spell = def();

	if(!spell.sound.empty() && !(m_flags & SPELLCAST_FLAG_NOSOUND)) {
		audio::SampleHandle sample = audio::createSample(g_sfxPath / spell.sound);
		if(sample != audio::SampleHandle()) {
			ARX_SOUND_PlaySFX(sample, &m_caster_pos);
		}
	}

	/*
	 * As long as the picture, and no longer.
	 *
	 * A spell with no duration is one the game never takes off the list by
	 * itself - that is for armour and invisibility, which end when something
	 * dispels them. These end on their own, so every cast used to sit in the
	 * spell list being updated for the rest of the level.
	 */
	m_hasDuration = true;

	/*
	 * Where you are looking, not where you stand.
	 *
	 * The same three hundred units in front that the game's own summoning
	 * spell uses (SummonCreatureSpell::GetTargetAndBeta) - a spell that goes
	 * off at the caster's feet reads as a spell that did nothing.
	 */
	float facing;
	if(m_caster == EntityHandle_Player) {
		m_pos = player.basePosition();
		facing = player.angle.getYaw();
	} else {
		m_pos = entities[m_caster]->pos;
		facing = entities[m_caster]->angle.getYaw();
	}
	m_pos += angleToVectorXZ(facing) * 300.f;

	m_useRift = (spell.visual == "SummonRift");
	m_useGrave = (spell.visual == "RaiseDead");
	m_useBolt = (spell.kind == "lightning");
	m_duration = (m_useRift || m_useGrave) ? 2s : 1s;
	
	/*
	 * A summoning lasts as long as its creature does, because the spell
	 * ending is what takes the creature away again. Everything else is as
	 * long as its picture.
	 */
	if(spell.kind == "summon" && spell.seconds > 0.f) {
		m_duration = std::chrono::milliseconds(long(spell.seconds * 1000.f));
		/*
		 * The creature is not here yet. The seconds the file asks for are
		 * seconds on its feet, so the time the ground stands open is added on
		 * top of them rather than taken out of them.
		 */
		if(m_useRift || m_useGrave) {
			m_duration += SUMMON_DELAY;
		}
	}
	if(m_useRift) {
		m_rift.Create(m_pos, MAKEANGLE(facing));
		m_rift.SetDuration(2s, 500ms, 1500ms);
		m_rift.SetColorBorder(Color3f::red);
		m_rift.SetColorRays1(Color3f::red);
		m_rift.SetColorRays2(Color3f::yellow * 0.5f);
	} else if(m_useGrave) {
		// The same tear in the ground Raise Dead makes, in the same colours
		m_grave.Create(m_pos, MAKEANGLE(facing));
		m_grave.SetDuration(2s, 500ms, 1800ms);
		m_grave.SetColorBorder(Color3f::gray(0.5f));
		m_grave.SetColorRays1(Color3f::gray(0.5f));
		m_grave.SetColorRays2(Color3f::red);
		// ... and the same black light under it, which Update turns red
		if(EERIE_LIGHT * light = dynLightCreate(m_light)) {
			light->intensity = 1.3f;
			light->fallend = 450.f;
			light->fallstart = 380.f;
			light->rgb = Color3f::black;
			light->pos = m_pos - Vec3f(0.f, 100.f, 0.f);
			light->duration = 200ms;
			light->creationTime = g_gameTime.now();
		}
	} else if(m_useBolt) {
		
		/*
		 * Built the way Lightning Strike builds its own: an arc five hundred
		 * units long pointing straight ahead, which Update then turns towards
		 * whatever is being fought.
		 */
		m_bolt.setReach(spell.reach);
		m_bolt.Create(Vec3f(0.f), Vec3f(0.f, 0.f, -500.f));
		m_bolt.SetDuration(spell.seconds > 0.f
		                   ? std::chrono::milliseconds(long(spell.seconds * 1000.f))
		                   : GameDuration(2s));
		m_bolt.m_isMassLightning = false;
		m_duration = m_bolt.m_duration;
		{
			Entity * mark = quarry();
			LogInfo << "studio spells: " << spell.key << " thrown by "
			        << ::idString(entities.get(m_caster)) << " at " << ::idString(mark)
			        << " reach " << spell.reach << " for " << spell.amount << "/s";
		}
		
	} else {
		ParticleParam look = ParticleParam_Heal;
		if(!spell.visual.empty() && !particleByName(spell.visual, look)) {
			LogWarning << "studio spells: " << spell.key
			           << " asks for a look called " << spell.visual;
		}
		m_particles.SetPos(m_pos);
		m_particles.SetParams(g_particleParameters[look]);
	}

	if(spell.kind == "summon" && !spell.creature.empty()) {
		
		/*
		 * If there is an opening in the ground, the creature waits for it and
		 * Update brings it up. If the spell has no such picture there is
		 * nothing to wait for, so it arrives at once.
		 */
		if(m_useRift || m_useGrave) {
			m_summonPending = true;
		} else {
			callUpCreature();
		}
		
	} else if(spell.kind == "damage" && spell.amount > 0.f) {
		DamageParameters damage;
		damage.pos = m_pos;
		damage.radius = (spell.radius > 0.f) ? spell.radius : 120.f;
		damage.damages = spell.amount;
		damage.area = DAMAGE_FULL;
		damage.duration = 1000ms;   // the area lingers a second, as it always did
		damage.source = m_caster;
		damage.flags = DAMAGE_FLAG_DONT_HURT_SOURCE;
		damage.type = spell.damageTypes ? spell.damageTypes : DAMAGE_TYPE_MAGICAL;
		DamageCreate(this, damage);
	} else if(spell.kind == "heal" && spell.amount > 0.f) {
		if(m_caster == EntityHandle_Player) {
			player.lifePool.current = std::min(player.lifePool.max,
			                                   player.lifePool.current + spell.amount);
		}
		if(spell.radius > 0.f) {
			for(Entity & other : entities(IO_NPC)) {
				if(!other._npcdata || glm::distance(other.pos, m_pos) > spell.radius) {
					continue;
				}
				other._npcdata->lifePool.current =
					std::min(other._npcdata->lifePool.max,
					         other._npcdata->lifePool.current + spell.amount);
			}
		}
	}

	/*
	 * The two things single player Arx could not do at all, and a co-op mod
	 * must: raise the other player, and bring them here. Both are asked of
	 * the machine that owns that body, because it is the only one entitled
	 * to answer.
	 *
	 * Only ever asked by the machine that cast the spell. The other machine
	 * launches a copy of every spell its partner casts, so that a fireball
	 * they throw is a fireball you see coming - and that copy asking for the
	 * same thing back is how one summons pulled BOTH players to the spot: the
	 * copy on their side politely summoned the caster.
	 */
	if(coop::isApplyingRemote()) {
		// their cast, shown here; the asking belongs to their machine
	} else if(spell.kind == "revive_partner") {
		coop::askPartnerRevive();
	} else if(spell.kind == "pull_partner") {
		/*
		 * Asked for halfway through, not now.
		 *
		 * The rift is what the spell looks like; someone arriving before it
		 * has opened reads as the spell having nothing to do with it. The
		 * game's own summoning does the same - the fissure animates, and the
		 * creature steps out of it when it is at its widest.
		 */
		m_askPending = true;
		/*
		 * Where the caster is standing, not where the spell was aimed.
		 *
		 * The rift opens at the aim point because that is a picture and a
		 * picture cannot hurt anybody. A player is another matter: this
		 * project's own rule, written in the join code, is that no position
		 * is ever computed for a body - the geometry will happily suggest a
		 * spot inside a wall, and the first cast of this spell killed the
		 * player it was meant to help.
		 *
		 * Feet, not eyes: player.pos is the eye, and a body placed at eye
		 * height stands a head too high.
		 */
	}

	/*
	 * Whatever the file could not say, the script says.
	 *
	 * The player's own script hears CUSTOM with this spell's name, which is
	 * how summoning, teleporting, or anything touching the other player is
	 * written - in ASL, changed without a compiler, by whoever installed the
	 * mod rather than whoever built it.
	 */
	if(!spell.key.empty() && !coop::isApplyingRemote()) {
		SendIOScriptEvent(nullptr, entities.player(), SM_CUSTOM,
		                  ScriptParameters("spell_" + spell.key));
	}

}

/*
 * Step for step what RaiseDeadSpell::Update does when its fissure is at its
 * widest: make sure there is room, put the creature there with a teleport
 * rather than by writing its position - a written position leaves the engine
 * thinking the creature is still in the room it was built in, and it will not
 * walk out of that - tell it who called it, and tell its script it was
 * summoned.
 */
void StudioSpell::callUpCreature() {
	
	const StudioSpellDef & spell = def();
	
	/*
	 * Only the machine that owns this area brings a creature into it. The other
	 * one runs a copy of every spell its partner casts so the picture matches,
	 * and a creature invented by that copy would stand on one screen only. The
	 * real one arrives through entity replication a beat later. The game's own
	 * summoning is guarded the same way.
	 */
	if(coop::isReplica()) {
		return;
	}
	
	Cylinder phys = Cylinder(m_pos, 50.f, -200.f);
	if(glm::abs(CheckAnythingInCylinder(phys, nullptr, CFLAG_JUST_TEST)) >= 30.f) {
		ARX_SOUND_PlaySFX(g_snd.MAGIC_FIZZLE);
		LogInfo << "studio spells: " << spell.key << " found nowhere to put "
		        << spell.creature;
		return;
	}
	
	Entity * called = AddNPC(res::path::load(spell.creature), -1, IO_IMMEDIATELOAD);
	if(!called) {
		LogWarning << "studio spells: " << spell.key << " could not call up "
		           << spell.creature;
		return;
	}
	
	ARX_INTERACTIVE_HideGore(called);
	RestoreInitialIOStatusOfIO(called);
	called->_npcdata->summoner = m_caster;
	called->ioflags |= IO_NOSAVE;
	called->scriptload = 1;
	m_summoned = called->index();
	ARX_INTERACTIVE_Teleport(called, phys.origin);
	SendInitScriptEvent(called);
	ARX_NPC_SetSummonHunts(*called, spell.hunts);
	SendIOScriptEvent(entities.get(m_caster), called, SM_SUMMONED);
	
	MakeCoolFx(m_pos + arx::randomVec3f() * 100.f + Vec3f(-50.f, 50.f, -50.f));
	
	/*
	 * It arrives knowing what to fight: whatever the caster has picked. Left
	 * to choose for itself it asks for the nearest creature in sight, and the
	 * nearest creature to a summoner is the summoner - the player carries the
	 * same mark as any other living thing here.
	 *
	 * Magic and distance, and no fighting flag: that is the behaviour the game
	 * gives its own casters. With the fighting flag it walks up and swings its
	 * fists, which is not what a summoned mage is for.
	 */
	if(Entity * quarry = coop::targetEntity(); quarry && quarry->_npcdata) {
		called->targetinfo = quarry->index();
		called->_npcdata->movemode = RUNMODE;
		ARX_NPC_Behaviour_Change(called, BEHAVIOUR_MAGIC | BEHAVIOUR_DISTANT, 0);
		LogInfo << "studio spells: it goes for " << quarry->idString();
	}
	
	LogInfo << "studio spells: " << spell.key << " called up " << called->idString()
	        << " for " << spell.seconds << "s";
	
}

void StudioSpell::End() {
	
	endLightDelayed(m_light, 500ms);
	
	// Whatever was called up goes back, in its own smoke, as the game's own does
	if(Entity * called = entities.get(m_summoned)) {
		ARX_SOUND_PlaySFX(g_snd.SPELL_ELECTRIC, &called->pos);
		AddRandomSmoke(*called, 100);
		MakeCoolFx(called->pos - Vec3f(0.f, 100.f, 0.f));
		called->destroyOne();
		m_summoned = EntityHandle();
	}
	
}

void StudioSpell::Update() {
	
	// The opening is at its widest: whatever was called climbs out of it now
	if(m_summonPending && m_elapsed >= SUMMON_DELAY) {
		m_summonPending = false;
		m_light = { };
		callUpCreature();
	}
	
	// halfway: the rift is open, and where you were looking is where it opened
	if(m_askPending && m_elapsed >= m_duration / 2) {
		m_askPending = false;
		coop::askPartnerHere(m_pos);
	}
	
	if(m_useRift) {
		m_rift.Update(g_gameTime.lastFrameDuration());
		m_rift.Render();
	} else if(m_useBolt) {
		
		/*
		 * Both angles, not one.
		 *
		 * The game's own bolt takes its left-and-right angle from the caster's
		 * body and only works out the up-and-down one. A creature turns towards
		 * what it is fighting at a third of a degree a millisecond, so one that
		 * casts the moment it decides to is still side-on, and its bolt goes
		 * past. Ours is aimed at where the thing actually is.
		 */
		Entity * caster = entities.get(m_caster);
		Vec3f from = caster ? chestOf(caster) : m_caster_pos;
		
		if(Entity * mark = quarry()) {
			Vec3f to = chestOf(mark);
			float flat = glm::distance(getXZ(to), getXZ(from));
			m_bolt.m_beta = MAKEANGLE(180.f + glm::degrees(getAngle(to.x, to.z, from.x, from.z)));
			/*
			 * The up-and-down angle is the height between the two chests over
			 * the flat run between them, and nothing else.
			 *
			 * Arkane's own bolt puts the difference in z into that run as well
			 * - getAngle(from.y, from.z, to.y, to.z + flat) - which is only
			 * right when the target happens to lie along x. Along -z the two
			 * terms cancel exactly and the angle snaps to ninety degrees; close
			 * up, both are small enough that which way the remainder points
			 * changes from frame to frame, and the bolt whips about.
			 */
			if(flat > 20.f) {
				m_bolt.m_alpha = MAKEANGLE(glm::degrees(getAngle(0.f, 0.f, to.y - from.y, flat)));
			} else {
				// Standing on top of each other: there is no run to speak of,
				// so any angle is as good as another. Level is the calm one.
				m_bolt.m_alpha = 0.f;
			}
		} else {
			m_bolt.m_beta = caster ? caster->angle.getYaw() : 0.f;
			m_bolt.m_alpha = 0.f;
		}
		
		m_bolt.m_pos = from;
		m_bolt.m_fDamage = (def().amount > 0.f) ? def().amount : 4.f;
		m_bolt.Update(g_gameTime.lastFrameDuration());
		m_bolt.Render();
		
	} else if(m_useGrave) {
		m_grave.Update(g_gameTime.lastFrameDuration());
		m_grave.Render();
		// Raise Dead's light: black at first, then the red of what is coming up
		if(EERIE_LIGHT * light = lightHandleGet(m_light)) {
			light->intensity = 3.f;
			light->fallend = 500.f;
			light->fallstart = 400.f;
			light->rgb = Color3f(0.8f, 0.2f, 0.2f);
			light->duration = 800ms;
			light->creationTime = g_gameTime.now();
		}
	} else {
		m_particles.Update(g_gameTime.lastFrameDuration());
		m_particles.Render();
	}
}

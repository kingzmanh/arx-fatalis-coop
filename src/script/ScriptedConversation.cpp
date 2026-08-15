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

#include "script/ScriptedConversation.h"

#include <sstream>

#include "core/Localisation.h"
#include "graphics/Math.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/NPC.h"
#include "gui/Speech.h"
#include "gui/Interface.h"
#include "gui/Notification.h"
#include "io/resource/ResourcePath.h"
#include "scene/GameSound.h"
#include "script/ScriptUtils.h"
#include "net/CoopNet.h"
#include "net/CoopPlayer.h"
#include "game/Player.h"


extern Vec3f LASTCAMPOS;
extern Anglef LASTCAMANGLE;

namespace script {

namespace {

class ConversationCommand : public Command {
	
public:
	
	ConversationCommand() : Command("conversation") { }
	
	Result execute(Context & context) override {
		
		std::string param = context.getWord();
		if(param != "off") {
			LogWarning << "Invalid used of stubbed conversation command";
		}
		
		return Success;
	}
	
};

class PlayCommand : public Command {
	
public:
	
	PlayCommand() : Command("play", AnyEntity) { }
	
	Result execute(Context & context) override {
		
		bool unique = false;
		SoundLoopMode loop = ARX_SOUND_PLAY_ONCE;
		float pitch = 1.f;
		bool stop = false;
		bool no_pos = false;
		
		HandleFlags("ilpso") {
			unique = test_flag(flg, 'i');
			if(flg & flag('l')) {
				loop = ARX_SOUND_PLAY_LOOPED;
			}
			if(flg & flag('p')) {
				pitch = Random::getf(0.9f, 1.1f);
			}
			stop = test_flag(flg, 's');
			no_pos = test_flag(flg, 'o');
		}
		
		res::path sample = res::path::load(context.getStringVar(context.getWord())).set_ext("wav");
		
		DebugScript(' ' << options << ' ' << sample);
		
		Entity * io = context.getEntity();
		if(stop) {
			ARX_SOUND_Stop(io->m_sound);
			io->m_sound = audio::SourcedSample();
		} else {
			
			if(unique) {
				ARX_SOUND_Stop(io->m_sound);
				io->m_sound = audio::SourcedSample();
			}
			
			audio::SourcedSample num = audio::SourcedSample();
			bool tooFar = false;
			// TODO(broken-scripts) should be a flag instead of depending on the event
			if(no_pos || SM_INVENTORYUSE == context.getMessage()) {
				num = ARX_SOUND_PlayScript(sample, tooFar, nullptr, pitch, loop);
			} else {
				num = ARX_SOUND_PlayScript(sample, tooFar, io, pitch, loop);
			}
			
			if(unique) {
				io->m_sound = num;
			}
			
			if(!tooFar && num == audio::SourcedSample()) {
				ScriptWarning << "unable to load sound file " << sample;
				return Failed;
			}
			
		}
		
		return Success;
	}
	
};

class PlaySpeechCommand : public Command {
	
public:
	
	PlaySpeechCommand() : Command("playspeech") { }
	
	Result execute(Context & context) override {
		
		res::path sample = res::path::load(context.getWord());
		
		DebugScript(' ' << sample);
		
		Entity * io = context.getEntity();
		
		// TODO check if we actually need to succeed if tooFar becomes true
		bool tooFar = false;
		audio::SourcedSample num = ARX_SOUND_PlaySpeech(sample, &tooFar,
		                                                io && io->show == SHOW_FLAG_IN_SCENE ? io : nullptr);
		
		if(!tooFar && num == audio::SourcedSample()) {
			ScriptWarning << "unable to load sound file " << sample;
			return Failed;
		}
		
		return Success;
	}
	
};

class HeroSayCommand : public Command {
	
public:
	
	HeroSayCommand() : Command("herosay") { }
	
	Result execute(Context & context) override {
		
		HandleFlags("d") {
			if((flg & flag('d'))) {
				context.skipWord();
				return Success;
			}
		}
		
		std::string text = context.getStringVar(context.getWord());
		
		DebugScript(' ' << options << " \"" << text << '"');
		
		notification_add(std::string(toLocalizationKey(text)));
		
		return Success;
	}
	
	Result peek(Context & context) override {
		
		/*
		 * TODO Technically this command has a side effect so it should abort non-destructive execution.
		 * However, the on combine event for books unconditionally uses this command. resulting
		 * in all books lighting up in the inventory when an item is selected for combining.
		 */
		
		(void)context.getFlags();
		
		context.skipWord();
		
		return Success;
	}
	
};

class SetSpeakPitchCommand : public Command {
	
public:
	
	SetSpeakPitchCommand() : Command("setspeakpitch", IO_NPC) { }
	
	Result execute(Context & context) override {
		
		float pitch = context.getFloat();
		if(pitch < .6f) {
			pitch = .6f;
		}
		
		DebugScript(' ' << pitch);
		
		context.getEntity()->_npcdata->speakpitch = pitch;
		
		return Success;
	}
	
};

class SpeakCommand : public Command {
	
	static void computeACSPos(CinematicSpeech & acs, Entity * speaker, Entity * target) {
		
		if(speaker) {
			if(speaker->obj->fastaccess.view_attach) {
				acs.pos1 = speaker->obj->vertexWorldPositions[speaker->obj->fastaccess.view_attach].v;
			} else {
				acs.pos1 = speaker->pos + Vec3f(0.f, speaker->physics.cyl.height, 0.f);
			}
		}
		
		if(target) {
			if(target->obj->fastaccess.view_attach) {
				acs.pos2 = target->obj->vertexWorldPositions[target->obj->fastaccess.view_attach].v;
			} else {
				acs.pos2 = target->pos + Vec3f(0.f, target->physics.cyl.height, 0.f);
			}
		}
		
	}
	
	static void parseParams(CinematicSpeech & acs, Context & context, Entity * speaker) {
		
		std::string target = context.getWord();
		Entity * t = entities.getById(target, context.getEntity());
		
		acs.ionum = (t == nullptr) ? EntityHandle() : t->index();
		acs.startpos = context.getFloat();
		acs.endpos = context.getFloat();
		
		computeACSPos(acs, speaker, entities.get(acs.ionum));
	}
	
	void parseCinematicSpeech(CinematicSpeech & acs, Context & context, Entity * speaker) {
		
		std::string command = context.getWord();
		
		if(command == "keep") {
			acs.type = ARX_CINE_SPEECH_KEEP;
			acs.pos1 = LASTCAMPOS;
			acs.pos2.x = LASTCAMANGLE.getPitch();
			acs.pos2.y = LASTCAMANGLE.getYaw();
			acs.pos2.z = LASTCAMANGLE.getRoll();
			
		} else if(command == "zoom") {
			acs.type = ARX_CINE_SPEECH_ZOOM;
			acs.startangle.setPitch(context.getFloat());
			acs.startangle.setYaw(context.getFloat());
			acs.endangle.setPitch(context.getFloat());
			acs.endangle.setYaw(context.getFloat());
			acs.startpos = context.getFloat();
			acs.endpos = context.getFloat();
			acs.ionum = context.getEntity()->index();
			computeACSPos(acs, speaker, entities.get(acs.ionum));
			
		} else if(command == "ccctalker_l" || command == "ccctalker_r") {
			acs.type = (command == "ccctalker_r") ? ARX_CINE_SPEECH_CCCTALKER_R : ARX_CINE_SPEECH_CCCTALKER_L;
			parseParams(acs, context, speaker);
			
		} else if(command == "ccclistener_l" || command == "ccclistener_r") {
			acs.type = (command == "ccclistener_r") ? ARX_CINE_SPEECH_CCCLISTENER_R :  ARX_CINE_SPEECH_CCCLISTENER_L;
			parseParams(acs, context, speaker);
			
		} else if(command == "side" || command == "side_l" || command == "side_r") {
			acs.type = (command == "side_l") ? ARX_CINE_SPEECH_SIDE_LEFT : ARX_CINE_SPEECH_SIDE;
			parseParams(acs, context, speaker);
			acs.m_startdist = context.getFloat(); // startdist
			acs.m_enddist = context.getFloat(); // enddist
			acs.m_heightModifier = context.getFloat(); // height modifier
		} else {
			ScriptWarning << "unexpected command: " << command;
		}
		
	}
	
public:
	
	SpeakCommand() : Command("speak", AnyEntity) { }
	
	Result execute(Context & context) override {
		
		CinematicSpeech acs;
		acs.type = ARX_CINE_SPEECH_NONE;
		
		Entity * speaker = context.getEntity();
		
		SpeechFlags flags = 0;
		AnimationNumber mood = ANIM_TALK_NEUTRAL;
		HandleFlags("tuphaoc") {
			
			flags |= (flg & flag('t')) ? ARX_SPEECH_FLAG_NOTEXT : SpeechFlags(0);
			flags |= (flg & flag('u')) ? ARX_SPEECH_FLAG_UNBREAKABLE : SpeechFlags(0);
			if(flg & flag('p')) {
				speaker = entities.player();
			}
			if(flg & flag('h')) {
				mood = ANIM_TALK_HAPPY;
			}
			if(flg & flag('a')) {
				mood = ANIM_TALK_ANGRY;
			}
			
			// Crash when we set speak pitch to 1,
			// Variable use for a division, 0 is not possible
			flags |= (flg & flag('o')) ? ARX_SPEECH_FLAG_OFFVOICE : SpeechFlags(0);
			
			if(flg & flag('c')) {
				parseCinematicSpeech(acs, context, speaker);
			}
			
		}
		
		std::string text = context.getWord();
		
		if(text == "killall") {
			
			if(!options.empty()) {
				ScriptWarning << "unexpected options: " << options << " killall";
			}
			
			ARX_SPEECH_Reset();
			return Success;
		}
		
		std::string data(toLocalizationKey(context.getStringVar(text)));
		
		DebugScript(' ' << options << ' ' << data); // TODO debug more
		
		if(data.empty()) {
			ARX_SPEECH_ClearIOSpeech(*context.getEntity());
			return Success;
		}
		
		/*
		 * Sequence speeches - the ones with a follow-up command that resumes
		 * the script when the line ends - drive one-shot story moments while
		 * the player stands locked. Those are playthrough FACTS in co-op:
		 * lived once by anyone, lived for both. A ledgered one completes
		 * instantly, running its follow-up the same frame, so the second
		 * player is never parked under cutscene bars waiting for a line that
		 * cannot play. Ordinary barks have no follow-up and are untouched.
		 */
		bool sequence = BLOCK_PLAYER_CONTROLS || cinematicBorder.isActive()
		                || coop::isPartnerCutscene();
		/*
		 * The guest NEVER performs sequence cutscenes - not because of any
		 * ledger state, but categorically: its sequence machinery is muted
		 * whenever the host shares the area, so a started sequence can freeze
		 * it solid. Completing instantly advances the story identically, the
		 * ledger records it for both, and only the host's screen is a stage.
		 */
		if(sequence && (coop::isCutsceneSeen(data) || (coop::isGuest() && coop::isReplica()))) {
			LogInfo << "[coop] '" << data << "' completes instantly on this machine";
			coop::reportCutsceneSeen(data);
			if(size_t onspeechend = context.skipCommand(); onspeechend != size_t(-1)) {
				/*
				 * Whose scene this is travels with the SPOKEN line: a speech
				 * remembers it, and hands it back when it ends and resumes the
				 * script (see endSpeech). A line the ledger skips leaves no
				 * speech behind to carry it - so the rest of the chain ran
				 * ownerless, this machine took a scene belonging to the other
				 * player back mid-way, and the camera flicked between the two
				 * of them. Carried by hand here, exactly as a spoken line
				 * would have carried it.
				 */
				coop::ScopedPlayerContext owner(coop::isPartnerScriptContext()
				                                ? coop::avatarEntity() : nullptr);
				ScriptEvent::resume(context.getScript(), context.getEntity(), onspeechend);
			}
			return Success;
		}
		
		/*
		 * Whether a line is written on screen depends on whether the bars are
		 * down - and the bars belong to whoever the scene belongs to. Asked on
		 * this machine about a scene we declined, the answer is always "no
		 * subtitles", and that answer was then sent to the one person actually
		 * watching it. So remember what the SCRIPT asked for and let their
		 * screen decide the rest.
		 */
		bool quiet = (flags & ARX_SPEECH_FLAG_NOTEXT);

		if(!cinematicBorder.isActive()) {
			flags |= ARX_SPEECH_FLAG_NOTEXT;
		}
		
		Speech * speech = ARX_SPEECH_AddSpeech(*speaker, data, mood, flags);
		if(!speech) {
			return Failed;
		}
		
		speech->cine = acs;
		
		if(size_t onspeechend = context.skipCommand(); onspeechend != size_t(-1)) {
			speech->scriptEntity = context.getEntity();
			speech->script = context.getScript();
			speech->scriptPos = onspeechend;
			if(sequence) {
				coop::reportCutsceneSeen(data);
			}
		}
		
		/*
		 * Two different reasons to send a line across.
		 *
		 * A story moment goes to whoever the scene belongs to, which the
		 * cutscene setting decides. But an ordinary line spoken BECAUSE of the
		 * other player - the goblin answering the person who just handed him a
		 * form, or who just spoke to him - is theirs no matter what: it is
		 * being said to them, and they are the one machine that would otherwise
		 * never hear it, since the script it came from runs only here.
		 */
		bool storyMoment = sequence || acs.type != ARX_CINE_SPEECH_NONE;
		bool spokenToThem = coop::isPartnerScriptContext();

		if((storyMoment && coop::relaysCutscene()) || spokenToThem) {
			// The other machine gets a viewer copy - same speaker, same line,
			// same camera - whenever the scene is theirs to watch, and the
			// words with it unless the script itself asked for silence.
			SpeechFlags theirs = quiet ? flags : (flags & ~ARX_SPEECH_FLAG_NOTEXT);
			coop::reportCutscenePlay(std::string(context.getEntity()->idString()),
			                         data, long(mood), u32(theirs), acs);
		}
		
		return Success;
	}
	
	Result peek(Context & context) override {
		
		HandleFlags("tuphaoc") {
			
			if(flg & flag('c')) {
				CinematicSpeech acs;
				parseCinematicSpeech(acs, context, context.getEntity());
			}
			
		}
		
		std::string text = context.getWord();
		
		if(text == "killall") {
			return Success;
		}
		
		std::string data(toLocalizationKey(context.getStringVar(text)));
		
		if(data.empty()) {
			return Success;
		}
		
		std::string command = context.getCommand(false);
		
		size_t onspeechend = context.skipCommand();
		
		if((!command.empty() && command != "nop") || onspeechend != size_t(-1)) {
			return AbortDestructive;
		}
		
		return Success;
	}
	
};

class SetStrikeSpeechCommand : public Command {
	
public:
	
	SetStrikeSpeechCommand() : Command("setstrikespeech", AnyEntity) { }
	
	Result execute(Context & context) override {
		
		context.getEntity()->strikespeech = toLocalizationKey(context.getWord());
		
		DebugScript(' ' << context.getEntity()->strikespeech);
		
		return Success;
	}
	
};

} // anonymous namespace

void setupScriptedConversation() {
	
	ScriptEvent::registerCommand(std::make_unique<ConversationCommand>());
	ScriptEvent::registerCommand(std::make_unique<PlayCommand>());
	ScriptEvent::registerCommand(std::make_unique<PlaySpeechCommand>());
	ScriptEvent::registerCommand(std::make_unique<HeroSayCommand>());
	ScriptEvent::registerCommand(std::make_unique<SetSpeakPitchCommand>());
	ScriptEvent::registerCommand(std::make_unique<SpeakCommand>());
	ScriptEvent::registerCommand(std::make_unique<SetStrikeSpeechCommand>());
	
}

} // namespace script

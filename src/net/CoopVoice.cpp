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

#include "net/CoopVoice.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>

#include <SDL.h>

#include <al.h>
#include <alc.h>

#if ARX_HAVE_OPUS
#include <opus.h>
#endif

#include "core/Config.h"
#include "core/GameTime.h"
#include "game/Entity.h"
#include "graphics/Math.h"
#include "input/Input.h"
#include "io/log/Logger.h"
#include "math/Vector.h"
#include "net/CoopNet.h"
#include "net/CoopPlayer.h"

namespace coop {
namespace voice {

#if !ARX_HAVE_OPUS

/*
 * Built without Opus, so there is no voice chat. Everything still answers, and
 * answers honestly: the menu shows why rather than offering a switch that does
 * nothing, and no microphone is ever opened.
 */
bool start() { return false; }
void stop() { }
void update() { }
void onPacket(const u8 *, size_t) { }
bool enabled() { return false; }
void setEnabled(bool) { }
bool openMic() { return false; }
void setOpenMic(bool) { }
bool transmitting() { return false; }
void setTesting(bool) { }
bool testing() { return false; }
float level() { return 0.f; }
int deviceCount() { return 0; }
const char * deviceName(int) { return "NONE"; }
int device() { return -1; }
void setDevice(int) { }
int nextDevice() { return -1; }
bool available() { return false; }
const char * problem() { return "BUILT WITHOUT VOICE CHAT"; }

#else

namespace {

/*
 * Opus is happiest at 48 kHz, and so is every sound card made this century, so
 * there is no resampling anywhere in this file. A frame is 20 milliseconds -
 * short enough that losing one is a click rather than a stutter, long enough
 * that the per-packet overhead is not most of the bandwidth.
 */
const int SampleRate = 48000;
const int FrameSamples = SampleRate / 50; // 20 ms
const int MaxPacket = 400;                // generous for 20 ms of speech

/*
 * How much speech to hold back before playing any of it.
 *
 * Packets do not arrive evenly spaced - the network bunches them up and spreads
 * them out - so playing each one the moment it lands produces gaps. Holding a
 * few frames smooths that out at the cost of a little delay. Three frames is
 * 60 ms, which is below what anyone notices in conversation.
 */
const size_t JitterTarget = 3;
const size_t JitterMax = 25; // half a second; beyond this we are just adding lag

//! Loudness above which open microphone decides someone is talking.
const float SpeakingLevel = 0.02f;

//! Keep sending for a moment after they stop, so word endings are not clipped.
const int HangoverFrames = 12; // 240 ms

SDL_AudioDeviceID g_capture = 0;
OpusEncoder * g_encoder = nullptr;
OpusDecoder * g_decoder = nullptr;

ALuint g_source = 0;
std::vector<ALuint> g_freeBuffers;

std::vector<s16> g_captured;          //!< raw microphone samples not yet framed
std::deque<std::vector<s16>> g_heard; //!< decoded frames waiting to be played
bool g_playing = false;               //!< the source is running and should stay fed

bool g_enabled = true;
bool g_openMic = false;
bool g_transmitting = false;
bool g_available = false;
int g_hangover = 0;
bool g_startFailed = false; //!< do not reopen a device that already refused
bool g_testing = false;     //!< microphone open purely so the player can check it
float g_level = 0.f;        //!< how loud the microphone is, for the meter
int g_device = -1;          //!< chosen microphone, -1 for whatever Windows prefers
const char * g_problem = "";

//! How loud a frame is, 0 to 1, as the plain average of the samples.
float loudness(const s16 * samples, int count) {

	if(count <= 0) {
		return 0.f;
	}

	double total = 0.0;
	for(int i = 0; i < count; i++) {
		total += std::abs(int(samples[i]));
	}

	return float(total / double(count) / 32768.0);

}

/*!
 * Where the other player's voice comes from.
 *
 * Their body, when there is one - which is what makes distance and direction
 * work. Arx feeds OpenAL world coordinates directly for the listener, so the
 * same coordinates go here with nothing to convert.
 */
bool partnerPosition(Vec3f & position) {

	Entity * body = avatarEntity();
	if(!body) {
		return false;
	}

	position = body->pos;

	// Out of the mouth rather than the feet, so the direction is right when
	// someone is standing above or below you.
	position.y -= 160.f;

	return true;

}

//! Take back any buffers OpenAL has finished with, so they can be refilled.
void recycleBuffers() {

	if(!g_source) {
		return;
	}

	ALint done = 0;
	alGetSourcei(g_source, AL_BUFFERS_PROCESSED, &done);
	while(done-- > 0) {
		ALuint buffer = 0;
		alSourceUnqueueBuffers(g_source, 1, &buffer);
		if(buffer) {
			g_freeBuffers.push_back(buffer);
		}
	}

}

void teardownAudio() {

	if(g_source) {
		alSourceStop(g_source);
		recycleBuffers();
		alDeleteSources(1, &g_source);
		g_source = 0;
	}

	if(!g_freeBuffers.empty()) {
		alDeleteBuffers(ALsizei(g_freeBuffers.size()), g_freeBuffers.data());
		g_freeBuffers.clear();
	}

}

/*!
 * Build the OpenAL source the other player's voice plays through.
 *
 * This borrows the context the game's audio system already made rather than
 * making one of its own - two contexts would mean two listeners, and the voice
 * would be positioned against a listener that never moves.
 */
bool setupAudio() {

	if(!alcGetCurrentContext()) {
		g_problem = "NO SOUND";
		return false;
	}

	alGetError(); // clear anything left by the game's own audio

	alGenSources(1, &g_source);
	if(alGetError() != AL_NO_ERROR || !g_source) {
		g_source = 0;
		g_problem = "NO SOUND";
		return false;
	}

	/*
	 * A voice carries further than a footstep but not for ever. These say: full
	 * volume within a couple of metres, fading to nothing by about twenty, which
	 * is far enough to call across a room and not so far as to carry through a
	 * whole level.
	 */
	alSourcef(g_source, AL_REFERENCE_DISTANCE, 200.f);
	alSourcef(g_source, AL_MAX_DISTANCE, 2000.f);
	alSourcef(g_source, AL_ROLLOFF_FACTOR, 1.f);
	alSourcei(g_source, AL_SOURCE_RELATIVE, AL_FALSE);
	alSourcef(g_source, AL_GAIN, 1.f);

	// A pool to cycle through; more than enough for the jitter buffer.
	g_freeBuffers.resize(JitterMax + 8);
	alGenBuffers(ALsizei(g_freeBuffers.size()), g_freeBuffers.data());
	if(alGetError() != AL_NO_ERROR) {
		g_freeBuffers.clear();
		teardownAudio();
		g_problem = "NO SOUND";
		return false;
	}

	return true;

}

//! Open the microphone. Failing here is not fatal - you can still listen.
bool setupCapture() {

	if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		g_problem = "NO AUDIO DEVICE";
		return false;
	}

	SDL_AudioSpec want;
	std::memset(&want, 0, sizeof(want));
	want.freq = SampleRate;
	want.format = AUDIO_S16SYS;
	want.channels = 1; // a voice is one voice; stereo would double the bandwidth
	want.samples = FrameSamples;
	want.callback = nullptr; // pulled in update(), so nothing crosses threads

	SDL_AudioSpec got;
	std::memset(&got, 0, sizeof(got));

	const char * wanted = nullptr;
	if(g_device >= 0 && g_device < SDL_GetNumAudioDevices(1)) {
		wanted = SDL_GetAudioDeviceName(g_device, 1);
	}

	g_capture = SDL_OpenAudioDevice(wanted, 1 /* capture */, &want, &got,
	                                0 /* no changes allowed */);
	if(g_capture == 0) {
		LogInfo << "[voice] no microphone: " << SDL_GetError();
		g_problem = "NO MICROPHONE";
		return false;
	}

	SDL_PauseAudioDevice(g_capture, 0);

	/*
	 * Say which microphone was actually opened, and what it agreed to.
	 *
	 * Asking for the default device gets whatever Windows currently calls the
	 * default, which is not always the one the player is talking into - a
	 * webcam, a monitor, or a virtual device left behind by some other program
	 * will all open happily and then deliver silence forever. Naming it in the
	 * log turns "the microphone does not work" into something answerable.
	 */
	LogInfo << "[voice] listening on \"" << (wanted ? wanted : "(system default)")
	        << "\", " << got.freq << " Hz, " << int(got.channels) << " channel(s)";

	int count = SDL_GetNumAudioDevices(1);
	for(int i = 0; i < count; i++) {
		const char * each = SDL_GetAudioDeviceName(i, 1);
		LogInfo << "[voice]   microphone " << i << ": " << (each ? each : "?");
	}

	return true;

}

//! Encode one frame and hand it to the network.
void sendFrame(const s16 * samples) {

	if(!g_encoder) {
		return;
	}

	unsigned char packet[MaxPacket];
	int written = opus_encode(g_encoder, samples, FrameSamples, packet, MaxPacket);
	if(written > 1) {
		coop::sendVoice(packet, size_t(written));
	}

}

//! Read the microphone and send whatever was said.
void captureAndSend() {

	if(!g_capture) {
		return;
	}

	bool talkKey = GInput && GInput->actionPressed(CONTROLS_CUST_COOP_TALK);

	Uint32 waiting = SDL_GetQueuedAudioSize(g_capture);

	/*
	 * While testing, report what is arriving every couple of seconds. Silence
	 * and nothing-at-all look identical on a meter, and they have completely
	 * different causes: no bytes means the device is not delivering, plenty of
	 * bytes at zero loudness means it is delivering silence.
	 */
	if(g_testing) {
		static int ticks = 0;
		if(++ticks >= 120) {
			ticks = 0;
			LogInfo << "[voice] mic test: " << waiting << " bytes waiting, level "
			        << g_level;
		}
	}

	if(waiting == 0) {
		return;
	}

	size_t before = g_captured.size();
	g_captured.resize(before + waiting / sizeof(s16));
	Uint32 got = SDL_DequeueAudio(g_capture, g_captured.data() + before, waiting);
	g_captured.resize(before + got / sizeof(s16));

	size_t offset = 0;
	while(g_captured.size() - offset >= size_t(FrameSamples)) {

		const s16 * frame = g_captured.data() + offset;
		offset += size_t(FrameSamples);

		/*
		 * The meter falls more slowly than it rises, because a meter that
		 * tracks the sound exactly flickers too fast to read. Rising instantly
		 * keeps it honest about whether the microphone is hearing anything.
		 */
		float loud = loudness(frame, FrameSamples);
		g_level = (loud > g_level) ? loud : (g_level * 0.8f + loud * 0.2f);

		if(g_testing) {
			// A test only listens. Nothing is encoded and nothing is sent.
			continue;
		}

		bool speak = talkKey;
		if(!speak && g_openMic) {
			if(loudness(frame, FrameSamples) > SpeakingLevel) {
				g_hangover = HangoverFrames;
			}
			speak = g_hangover > 0;
			if(g_hangover > 0) {
				g_hangover--;
			}
		} else if(talkKey) {
			g_hangover = 0;
		}

		g_transmitting = speak;
		if(speak) {
			sendFrame(frame);
		}

	}

	g_captured.erase(g_captured.begin(), g_captured.begin() + long(offset));

	/*
	 * If the game stalls - loading a level, say - the microphone keeps running
	 * and the backlog would all be sent at once as a burst of stale speech.
	 * Throw away anything beyond a reasonable amount rather than send it late.
	 */
	if(g_captured.size() > size_t(FrameSamples) * 25) {
		g_captured.clear();
	}

}

//! Put what was heard into the world, at the other player's mouth.
void playHeard() {

	if(!g_source) {
		return;
	}

	recycleBuffers();

	Vec3f at(0.f);
	if(partnerPosition(at)) {
		alSource3f(g_source, AL_POSITION, at.x, at.y, at.z);
	}

	/*
	 * Wait until a few frames have gathered before starting, then keep playing
	 * until the buffer runs dry. Starting on the first frame to arrive means
	 * running out immediately and stuttering through the whole sentence.
	 */
	if(!g_playing && g_heard.size() < JitterTarget) {
		return;
	}
	if(g_heard.empty()) {
		g_playing = false;
		return;
	}
	g_playing = true;

	while(!g_heard.empty() && !g_freeBuffers.empty()) {

		std::vector<s16> & frame = g_heard.front();
		ALuint buffer = g_freeBuffers.back();
		g_freeBuffers.pop_back();

		alBufferData(buffer, AL_FORMAT_MONO16, frame.data(),
		             ALsizei(frame.size() * sizeof(s16)), SampleRate);
		alSourceQueueBuffers(g_source, 1, &buffer);

		g_heard.pop_front();

	}

	ALint state = 0;
	alGetSourcei(g_source, AL_SOURCE_STATE, &state);
	if(state != AL_PLAYING) {
		alSourcePlay(g_source);
	}

}

} // anonymous namespace

bool start() {

	stop();

	int error = OPUS_OK;
	g_encoder = opus_encoder_create(SampleRate, 1, OPUS_APPLICATION_VOIP, &error);
	if(error != OPUS_OK || !g_encoder) {
		g_encoder = nullptr;
		g_problem = "VOICE UNAVAILABLE";
		return false;
	}

	// Plenty for speech, and small enough that it never competes with the game.
	opus_encoder_ctl(g_encoder, OPUS_SET_BITRATE(24000));
	// Tell Opus to expect some loss, so it protects the stream against it.
	opus_encoder_ctl(g_encoder, OPUS_SET_PACKET_LOSS_PERC(5));
	opus_encoder_ctl(g_encoder, OPUS_SET_INBAND_FEC(1));

	g_decoder = opus_decoder_create(SampleRate, 1, &error);
	if(error != OPUS_OK || !g_decoder) {
		stop();
		g_problem = "VOICE UNAVAILABLE";
		return false;
	}

	/*
	 * Playback is only needed to hear the other player. A microphone test needs
	 * nothing but the microphone, so a machine with no working output can still
	 * be used to find out whether its microphone is picking anything up.
	 */
	if(!setupAudio() && !g_testing) {
		stop();
		return false;
	}

	// Listening works even with no microphone, so this failing is not the end.
	bool mic = setupCapture();
	g_available = mic;
	if(mic) {
		g_problem = "";
		LogInfo << "[voice] microphone open, speech will carry from your body";
	}

	return true;

}

void stop() {

	if(g_capture) {
		SDL_CloseAudioDevice(g_capture);
		g_capture = 0;
	}

	teardownAudio();

	if(g_encoder) {
		opus_encoder_destroy(g_encoder);
		g_encoder = nullptr;
	}
	if(g_decoder) {
		opus_decoder_destroy(g_decoder);
		g_decoder = nullptr;
	}

	g_captured.clear();
	g_heard.clear();
	g_playing = false;
	g_transmitting = false;
	g_available = false;
	g_hangover = 0;
	g_level = 0.f;

}

void update() {

	if(!g_enabled) {
		return;
	}

	if(!coop::isPlaying() && !g_testing) {
		/*
		 * Nobody to talk to. Give the microphone back rather than sit holding it
		 * open through the whole main menu, and drop anything still queued so a
		 * later session does not open with the tail of an older one.
		 */
		if(g_encoder) {
			stop();
		}
		g_startFailed = false; // a new session deserves a fresh try
		return;
	}

	if(!g_encoder) {
		// Started here, on the first frame there is somebody to speak to,
		// rather than up front - opening a microphone nobody asked to use is
		// the sort of thing that makes players suspicious.
		if(g_startFailed || !start()) {
			g_startFailed = true;
			return;
		}
	}

	captureAndSend();

	if(!g_testing) {
		playHeard();
	}

}

void onPacket(const u8 * data, size_t size) {

	if(!g_enabled || !g_decoder || !data || size == 0) {
		return;
	}

	/*
	 * Drop the oldest rather than the newest when the buffer overruns. If we
	 * are this far behind, the old frames are no longer worth hearing and
	 * keeping them would only push the delay up permanently.
	 */
	while(g_heard.size() >= JitterMax) {
		g_heard.pop_front();
	}

	std::vector<s16> frame(FrameSamples);
	int samples = opus_decode(g_decoder, data, opus_int32(size), frame.data(),
	                          FrameSamples, 0);
	if(samples <= 0) {
		return;
	}

	frame.resize(size_t(samples));
	g_heard.push_back(std::move(frame));

}

bool enabled() {
	return g_enabled;
}

void setEnabled(bool on) {

	if(g_enabled == on) {
		return;
	}

	g_enabled = on;

	if(!on) {
		// Close the microphone rather than just ignore it: a player who turns
		// voice off expects the light on their headset to go out.
		stop();
	} else {
		// Opened by update() on the next frame, but only once there is somebody
		// to talk to.
		g_startFailed = false;
	}

}

bool openMic() {
	return g_openMic;
}

void setOpenMic(bool on) {
	g_openMic = on;
	g_hangover = 0;
}

bool transmitting() {
	return g_transmitting;
}

void setTesting(bool on) {

	if(g_testing == on) {
		return;
	}

	g_testing = on;
	g_level = 0.f;

	if(!on && !coop::isPlaying()) {
		// Give the microphone straight back rather than hold it for a session
		// that may never start.
		stop();
	}
	if(on) {
		g_startFailed = false;
	}

}

bool testing() {
	return g_testing;
}

float level() {
	return g_level;
}

int deviceCount() {
	return SDL_GetNumAudioDevices(1);
}

const char * deviceName(int index) {

	if(index < 0) {
		return "SYSTEM DEFAULT";
	}

	const char * name = SDL_GetAudioDeviceName(index, 1);
	return name ? name : "?";

}

int device() {
	return g_device;
}

void setDevice(int index) {

	if(g_device == index) {
		return;
	}

	g_device = index;
	g_level = 0.f;

	/*
	 * Reopen straight away rather than wait for the next session, so the meter
	 * answers immediately - the whole point of choosing is to see whether this
	 * one is the right one.
	 */
	bool wasTesting = g_testing;
	bool wasEnabled = g_enabled;
	stop();
	g_startFailed = false;
	if((wasTesting || coop::isPlaying()) && wasEnabled) {
		start();
	}

}

int nextDevice() {

	int count = SDL_GetNumAudioDevices(1);
	if(count <= 0) {
		return -1;
	}

	// Walks -1, 0, 1, ... and back to -1, so the system default stays reachable.
	int next = g_device + 1;
	if(next >= count) {
		next = -1;
	}

	setDevice(next);
	return g_device;

}

bool available() {
	return g_available;
}

const char * problem() {
	return g_problem;
}

#endif // ARX_HAVE_OPUS

} // namespace voice
} // namespace coop

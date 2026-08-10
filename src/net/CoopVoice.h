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

/*
 * Speaking to the other player, from wherever you happen to be standing.
 *
 * Not a chat channel - a voice in the world. What you say comes out of your
 * character's mouth, so it fades with distance and arrives from the direction
 * you are actually in. Walk far enough away and you cannot be heard at all;
 * stand behind someone and your voice comes from behind them.
 *
 * That falls out of the game's own audio system almost for free: Arx already
 * plays every torch and every footstep through OpenAL, positioned in the world,
 * and a voice is just another sound with a position. The work is getting the
 * words there - captured, made small enough to send, and put back together at
 * the other end without the gaps being audible.
 */

#ifndef ARX_NET_COOPVOICE_H
#define ARX_NET_COOPVOICE_H

#include <stddef.h>

#include "platform/Platform.h"

namespace coop {
namespace voice {

/*!
 * Start capturing and playing. Safe to call when there is no microphone, no
 * sound card, or no permission to use either - it simply reports that it could
 * not, and the game carries on without voice.
 */
bool start();

//! Stop, release the microphone, and forget anything still queued.
void stop();

//! Called once a frame: sends what was said, plays what was heard.
void update();

//! A moment of speech arrived from the other player.
void onPacket(const u8 * data, size_t size);

// -- what the player can change -------------------------------------------

//! Whether voice is used at all. Off keeps the microphone closed.
bool enabled();
void setEnabled(bool on);

/*!
 * Open microphone sends whenever you are actually speaking, judged by loudness.
 * With it off, nothing is sent unless the talk key is held - which is the safer
 * default, because a microphone that opens on its own will eventually broadcast
 * something its owner did not mean to send.
 */
bool openMic();
void setOpenMic(bool on);

//! True while speech is actually being sent, for showing the player a mic icon.
bool transmitting();

/*!
 * Open the microphone on its own, with nobody connected, so a player can see
 * whether it works before trusting it. Nothing is sent anywhere - the captured
 * sound is measured and thrown away.
 *
 * Worth having because every other way of finding out is bad: joining a game to
 * discover your microphone was muted wastes both players' time, and Windows
 * gives a silent stream rather than an error when an application is not allowed
 * to listen.
 */
void setTesting(bool on);
bool testing();

/*!
 * How loud the microphone is right now, 0 to 1, for drawing a level meter.
 * Live whenever the microphone is open - during a test or during a game.
 */
float level();

/*!
 * Which microphone to listen to.
 *
 * Letting Windows pick is not good enough. A modern machine is full of
 * microphones that are not microphones - a headset's surround driver, a
 * streaming filter, a VR headset left plugged in - and every one of them opens
 * without complaint and then delivers silence forever. The player is the only
 * one who knows which is real, so the player chooses.
 */
int deviceCount();
const char * deviceName(int index);

//! Index of the chosen microphone, or -1 for whichever Windows prefers.
int device();
void setDevice(int index);

//! Move to the next microphone in the list, wrapping round. Returns the new one.
int nextDevice();

//! True when a microphone was found and opened.
bool available();

//! Why voice is not working, for showing the player. Empty when it is fine.
const char * problem();

} // namespace voice
} // namespace coop

#endif // ARX_NET_COOPVOICE_H

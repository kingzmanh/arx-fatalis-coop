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

#include "gui/CoopChat.h"

#include "core/Core.h"
#include "graphics/Draw.h"
#include "graphics/DrawLine.h"
#include "graphics/font/Font.h"
#include "gui/Console.h"
#include "gui/Menu.h"
#include "gui/Text.h"
#include "input/Input.h"
#include "math/Rectangle.h"
#include "net/CoopNet.h"

CoopChat g_coopChat;

void CoopChat::open() {
	if(!m_open) {
		m_open = true;
		// the world keeps running - only the keyboard changes hands
		g_gameActionsSuppressed = true;
	}
}

void CoopChat::close() {
	if(m_open) {
		GInput->stopTextInput();
		m_open = false;
		g_gameActionsSuppressed = false;
		clear();
	}
}

void CoopChat::send() {

	// leading/trailing spaces are typing debris, not message
	std::string_view msg = text();
	while(!msg.empty() && msg.front() == ' ') {
		msg.remove_prefix(1);
	}
	while(!msg.empty() && msg.back() == ' ') {
		msg.remove_suffix(1);
	}

	if(!msg.empty()) {
		coop::sendChat(msg);
	}

	close();
}

bool CoopChat::keyPressed(Keyboard::Key key, KeyModifiers mod) {

	switch(key) {
		case Keyboard::Key_Enter:
		case Keyboard::Key_NumPadEnter: {
			send();
			return true;
		}
		case Keyboard::Key_Escape: {
			close();
			return true;
		}
		default: break;
	}

	return Base::keyPressed(key, mod);
}

void CoopChat::update() {

	if(!m_open) {
		// Y opens the chat - only in a running co-op game, never over the
		// console, and never in a menu where Y is just a letter
		if(coop::isPlaying() && ARXmenu.mode() == Mode_InGame && !g_console.isOpen()
		   && GInput->isKeyPressedNowPressed(Keyboard::Key_Y)) {
			open();
		}
		// claim the keyboard only from the NEXT frame, so the Y that opened
		// the chat is not also its first letter
		return;
	}

	if(!coop::isPlaying() || ARXmenu.mode() != Mode_InGame) {
		// the session or the world went away mid-sentence
		close();
		return;
	}

	s32 lineHeight = hFontInGame->getLineHeight();
	Rect box = g_size;
	box.top = box.bottom - lineHeight * 3;
	GInput->startTextInput(box, this);
}

void CoopChat::draw() {

	if(!m_open) {
		return;
	}

	UseRenderState state(render2D());

	// the parchment-gold the game speaks in
	Color gold = Color(232, 204, 142);
	Color dim = Color(140, 120, 82);
	Color background = Color::black;
	background.a = 140;

	float lineHeight = float(hFontInGame->getLineHeight());
	Vec2f pos(g_size.left + 18.f * g_sizeRatio.x,
	          g_size.bottom - 76.f * g_sizeRatio.y - lineHeight);

	{
		// a quiet dark strip so the words survive bright stone behind them
		Rectf back = Rectf(g_size);
		back.left = pos.x - 8.f;
		back.top = pos.y - 4.f;
		back.right = back.left + g_size.width() * 0.55f;
		back.bottom = back.top + lineHeight + 8.f;
		EERIEDrawBitmap(back, 0.f, nullptr, background);
	}

	Vec2i ipos(s32(pos.x), s32(pos.y));
	ipos.x += hFontInGame->draw(ipos, "» ", dim).advance();

	std::string_view shown = text();
	s32 textStart = ipos.x;
	hFontInGame->draw(ipos, shown, gold);

	// the cursor: a thin gold line where the next letter lands
	s32 curX = textStart + hFontInGame->getTextSize(shown.substr(0, cursorPos())).advance();
	drawLine(Vec2f(float(curX) + 1.f, pos.y),
	         Vec2f(float(curX) + 1.f, pos.y + lineHeight), 0.f, gold);
}

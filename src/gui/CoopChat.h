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

#ifndef ARX_GUI_COOPCHAT_H
#define ARX_GUI_COOPCHAT_H

#include "input/TextInput.h"

/*!
 * The co-op chat line.
 *
 * Y opens it, Enter sends what you wrote to the other player, Escape throws
 * it away. Messages - yours and theirs - come out through the game's own
 * notification text in the game's own font, so chat reads like Arx speaking,
 * not like a program taped over it.
 *
 * The world does NOT pause while you type; your partner covers you. While the
 * line is open every game action is suppressed, so spelling "sword" does not
 * draw one.
 */
class CoopChat final : protected BasicTextInput {
	typedef BasicTextInput Base;

	bool m_open = false;

	bool keyPressed(Keyboard::Key key, KeyModifiers mod) override;

	void send();

public:

	bool isOpen() const { return m_open; }

	void open();
	void close();

	void update();
	void draw();

};

extern CoopChat g_coopChat;

#endif // ARX_GUI_COOPCHAT_H

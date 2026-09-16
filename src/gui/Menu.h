/*
 * Copyright 2011-2019 Arx Libertatis Team (see the AUTHORS file)
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
// Code: Cyril Meynier
//
// Copyright (c) 1999-2001 ARKANE Studios SA. All rights reserved

#ifndef ARX_GUI_MENU_H
#define ARX_GUI_MENU_H

#include <string>

class TextureContainer;

enum MenuMode {
	Mode_InGame,
	Mode_MainMenu,
	Mode_Credits,
	Mode_CharacterCreation
};

struct ARX_MENU_DATA {
	
	MenuMode mode() {
		return m_currentMode;
	}
	void requestMode(MenuMode mode) {
		m_currentMode = mode;
	}
	
private:
	MenuMode m_currentMode;
};

extern ARX_MENU_DATA ARXmenu;
extern bool g_canResumeGame;

/*!
 * rief One life, one save, and no one to call for help.
 *
 * Chosen when the quest is started and never afterwards: it rides in the save
 * so a run cannot be quietly turned back into an ordinary one. While it is on
 * there is a single slot, Load and Save are not offered, hosting and joining
 * are refused, and dying deletes the save.
 *
 * Co-op is shut out on purpose rather than by oversight. A partner who can
 * revive you is a second life every time they reach your body, which is a
 * different game from the one this mode is for; and with one world shared
 * between two people it is not clear whose run a death should end. Solo only
 * until that has an answer.
 */
extern bool g_ironman;

//! The one slot an iron man run keeps, written over each time.
//! The name of the single slot an iron man run keeps.
extern const char * const ARX_IRONMAN_SLOT;

void ARX_IronmanSave();

/*!
 * rief End an iron man run: the save is deleted, for good.
 *
 * Called when the player dies. Nothing is hidden or renamed - the files go,
 * which is the whole point of the mode and the reason it is chosen once at
 * the start and cannot be turned off afterwards.
 */
void ARX_IronmanDied();

void ARX_Menu_Manage();
void ARX_Menu_Render();
void ARX_MENU_Launch(bool allowResume);
void ARX_Menu_Resources_Release();
void ARX_MENU_Clicked_CREDITS();

void ARX_MENU_Clicked_NEWQUEST();

#endif // ARX_GUI_MENU_H

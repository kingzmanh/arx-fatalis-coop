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

#ifndef ARX_CINEMATIC_CINEMATICCONTROLLER_H
#define ARX_CINEMATIC_CINEMATICCONTROLLER_H

#include <string_view>

void cinematicInit();
void cinematicDestroy();

/*!
 * When set, cinematics do not play and the entities that drive them - the
 * cursor that starts the intro, the cameras that take the view - are not
 * created when a level loads. Lets a player stand in a place that exists only
 * to be filmed. Starts from the ARX_NO_CINE environment variable and can be
 * changed at any time with the "nocine" script command.
 */
extern bool g_noCinematics;

void cinematicPrepare(std::string_view name, bool preload);

void cinematicRequestStart();

void cinematicKill();

void cinematicLaunchWaiting();

bool cinematicIsStopped();

void cinematicEnd();

bool isInCinematic();
void cinematicRender();

#endif // ARX_CINEMATIC_CINEMATICCONTROLLER_H

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
 * Asking the router to open the co-op port, so a host does not have to.
 *
 * Hosting a game normally means going into a router's settings and forwarding a
 * port by hand, which is where most people give up. Nearly every home router
 * will do it on request - through UPnP, NAT-PMP or PCP - and this asks.
 *
 * The request is answered by libplum on its own thread, but nothing here calls
 * into the game from that thread: the state is polled from the game's own tick
 * instead, so there is no locking to get wrong.
 */

#ifndef ARX_NET_COOPPORTMAP_H
#define ARX_NET_COOPPORTMAP_H

#include <string>

namespace coop {
namespace portmap {

enum class State {
	Idle,     //!< nothing has been asked for
	Asking,   //!< the router has been asked and has not answered yet
	Open,     //!< the port is forwarded
	Refused   //!< no router on this network will do it
};

/*!
 * Ask the router to forward \a port to this machine, and return immediately.
 * Poll state() for the answer. Asking twice for the same port does nothing.
 */
void open(unsigned short port);

//! Give the port back. Safe to call when nothing was ever opened.
void close();

//! Bring the state up to date. Call once per frame; costs nothing when idle.
void update();

State state();

/*!
 * The address the outside world would use to reach this machine, once known -
 * "86.21.9.4:38595". Empty until the router answers.
 */
const std::string & publicAddress();

/*!
 * True when the router opened the port but its own outside address is itself a
 * private one, which means the internet provider has put another layer of NAT
 * above it. The port really is open and it really does not help: players still
 * cannot reach this machine directly. Worth saying plainly rather than letting
 * someone wonder why nobody can join.
 */
bool behindSecondNat();

//! One line fit to show a player, describing whatever just happened.
std::string statusLine();

} // namespace portmap
} // namespace coop

#endif // ARX_NET_COOPPORTMAP_H

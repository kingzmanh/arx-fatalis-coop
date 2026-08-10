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

#include "net/CoopPortMap.h"

#include <cstdio>
#include <cstring>

#include <plum/plum.h>

#include "io/log/Logger.h"

namespace coop {
namespace portmap {

namespace {

bool g_started = false;      //!< plum_init has been called
int g_mapping = -1;          //!< the handle libplum gave us, -1 for none
unsigned short g_port = 0;
State g_state = State::Idle;
std::string g_public;
bool g_secondNat = false;

//! Route libplum's chatter into the game's own log rather than stdout.
void logFromPlum(plum_log_level_t level, const char * message) {
	switch(level) {
		case PLUM_LOG_LEVEL_ERROR:
		case PLUM_LOG_LEVEL_FATAL: LogError << "[portmap] " << message; break;
		case PLUM_LOG_LEVEL_WARN:  LogWarning << "[portmap] " << message; break;
		default:                   LogDebug("[portmap] " << message); break;
	}
}

/*!
 * Is this one of the addresses reserved for private networks?
 *
 * A router that reports one of these as its outside address is not actually on
 * the internet - there is another router above it, belonging to the internet
 * provider, and the port we just opened stops at that one.
 */
bool isPrivateAddress(const char * host) {

	unsigned a = 0, b = 0, c = 0, d = 0;
	if(std::sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
		return false; // not a plain IPv4 address; assume it is reachable
	}

	if(a == 10) {
		return true;                              // 10.0.0.0/8
	}
	if(a == 192 && b == 168) {
		return true;                              // 192.168.0.0/16
	}
	if(a == 172 && b >= 16 && b <= 31) {
		return true;                              // 172.16.0.0/12
	}
	if(a == 100 && b >= 64 && b <= 127) {
		return true;   // 100.64.0.0/10, the range providers use for their own NAT
	}
	if(a == 169 && b == 254) {
		return true;                              // link-local, nothing routes here
	}
	if(a == 127) {
		return true;
	}

	return false;

}

} // anonymous namespace

void open(unsigned short port) {

	if(g_mapping >= 0 && g_port == port) {
		return; // already asked for this one
	}

	close();

	if(!g_started) {
		plum_config_t config;
		std::memset(&config, 0, sizeof(config));
		config.log_level = PLUM_LOG_LEVEL_WARN;
		config.log_callback = logFromPlum;
		if(plum_init(&config) < 0) {
			LogWarning << "[portmap] could not start; the port will have to be forwarded by hand";
			g_state = State::Refused;
			return;
		}
		g_started = true;
	}

	plum_mapping_t mapping;
	std::memset(&mapping, 0, sizeof(mapping));
	mapping.protocol = PLUM_IP_PROTOCOL_UDP; // ENet speaks UDP
	mapping.internal_port = port;

	// No callback: the answer is collected in update(), on the game's thread.
	g_mapping = plum_create_mapping(&mapping, nullptr);
	if(g_mapping < 0) {
		LogWarning << "[portmap] the router could not be asked for port " << port;
		g_state = State::Refused;
		return;
	}

	g_port = port;
	g_state = State::Asking;
	g_public.clear();
	g_secondNat = false;
	LogInfo << "[portmap] asking the router to open UDP " << port;

}

void close() {

	if(g_mapping >= 0) {
		plum_destroy_mapping(g_mapping);
		g_mapping = -1;
		LogInfo << "[portmap] gave port " << g_port << " back to the router";
	}

	g_port = 0;
	g_state = State::Idle;
	g_public.clear();
	g_secondNat = false;

}

void update() {

	if(g_mapping < 0 || g_state == State::Open || g_state == State::Refused) {
		return;
	}

	plum_state_t got = PLUM_STATE_PENDING;
	plum_mapping_t mapping;
	std::memset(&mapping, 0, sizeof(mapping));
	if(plum_query_mapping(g_mapping, &got, &mapping) < 0) {
		return;
	}

	if(got == PLUM_STATE_SUCCESS) {

		char text[320];
		std::snprintf(text, sizeof(text), "%s:%u",
		              mapping.external_host, unsigned(mapping.external_port));
		g_public = text;
		g_secondNat = isPrivateAddress(mapping.external_host);
		g_state = State::Open;

		if(g_secondNat) {
			LogWarning << "[portmap] the router opened the port, but its own address ("
			           << mapping.external_host << ") is a private one - the internet "
			           << "provider has another router above it, so this machine still "
			           << "cannot be reached directly";
		} else {
			LogInfo << "[portmap] open, players can reach this machine at " << g_public;
		}

	} else if(got == PLUM_STATE_FAILURE) {
		g_state = State::Refused;
		LogInfo << "[portmap] no router here would open the port";
	}

}

State state() {
	return g_state;
}

const std::string & publicAddress() {
	return g_public;
}

bool behindSecondNat() {
	return g_secondNat;
}

std::string statusLine() {

	switch(g_state) {

		case State::Idle:
			return std::string();

		/*
		 * Short enough to fit the menu. The line sits under the status readout
		 * on a page whose widest text is "CO-OP: WAITING ON PORT 27100", so
		 * anything much beyond thirty characters runs off both edges of the
		 * screen and cannot be read at all.
		 */

		case State::Asking:
			return "OPENING PORT...";

		case State::Open:
			if(g_secondNat) {
				return "PORT OPEN - VPN STILL NEEDED";
			}
			return "JOIN AT " + g_public;

		case State::Refused:
			return "ROUTER REFUSED - USE A VPN";

	}

	return std::string();

}

} // namespace portmap
} // namespace coop

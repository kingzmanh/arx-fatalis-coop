/**
 * Copyright (c) 2020-2021 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "net.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <netioapi.h>
#elif defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#define NETLINK_BUFFER_SIZE 8192
#elif defined(__APPLE__)
#include <TargetConditionals.h>
# if TARGET_OS_OSX
# include <sys/sysctl.h>
# include <sys/socket.h>
# include <net/route.h>
# include <netinet/in.h>
# define HAVE_OSX_NET_ROUTE
# endif
#endif

int net_get_default_interface(int family, addr_record_t *record) {
	const char *host = NULL;
	switch (family) {
	case AF_INET:
		host = "192.0.2.1"; // dummy public unreachable address
		break;
	case AF_INET6:
		host = "2001:db8::1"; // dummy public unreachable address
		break;
	default:
		PLUM_LOG_WARN("Unknown address family %d", family);
		return -1;
	}

	addr_record_t dummy;
	addr_set(family, host, 9, &dummy);

	socket_t sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) {
		PLUM_LOG_WARN("UDP socket creation failed, errno=%d", sockerrno);
		return -1;
	}

	if (connect(sock, (const struct sockaddr *)&dummy.addr, dummy.len)) {
		PLUM_LOG_WARN("connect failed on UDP socket, errno=%d", sockerrno);
		goto error;
	}

	record->len = sizeof(record->addr);
	if (getsockname(sock, (struct sockaddr *)&record->addr, &record->len)) {
		PLUM_LOG_WARN("getsockname failed, errno=%d", sockerrno);
		goto error;
	}

	addr_unmap_inet6_v4mapped((struct sockaddr *)&record->addr, &record->len);
	if (record->addr.ss_family != family) {
		PLUM_LOG_WARN("getsockname returned unexpected address family");
		goto error;
	}

	addr_set_port((struct sockaddr *)&record->addr, 0);

	if (addr_is_local((const struct sockaddr *)&record->addr))
		goto error;

	closesocket(sock);
	return 0;

error:
	closesocket(sock);
	return -1;
}

int net_get_default_gateway(int family, addr_record_t *record) {
#if defined(_WIN32)
	/*
	 * Ask Windows which route it would really use, instead of taking the first
	 * default route in the table.
	 *
	 * A machine with Radmin VPN, Hamachi or ZeroTier installed has more than
	 * one default route. The VPN's is often listed first even though its metric
	 * marks it as a last resort, so picking the first one means asking the VPN
	 * to forward a port - which it will never do, and the real router is never
	 * asked at all. GetBestRoute2 applies the same selection the network stack
	 * applies to an outgoing packet, metrics and all.
	 */
	SOCKADDR_INET destination;
	memset(&destination, 0, sizeof(destination));
	destination.si_family = (ADDRESS_FAMILY)family;
	if (family == AF_INET) {
		// A landmark on the public internet. Nothing is sent to it; it only
		// says "a route that leaves this network", which is the one we want.
		destination.Ipv4.sin_family = AF_INET;
		destination.Ipv4.sin_addr.s_addr = htonl(0x08080808u); // 8.8.8.8
	} else if (family == AF_INET6) {
		destination.Ipv6.sin6_family = AF_INET6;
		// 2001:4860:4860::8888
		unsigned char *b = destination.Ipv6.sin6_addr.s6_addr;
		memset(b, 0, 16);
		b[0] = 0x20; b[1] = 0x01; b[2] = 0x48; b[3] = 0x60;
		b[4] = 0x48; b[5] = 0x60; b[15] = 0x88; b[14] = 0x88;
	}

	if (family == AF_INET || family == AF_INET6) {
		MIB_IPFORWARD_ROW2 best;
		SOCKADDR_INET bestSource;
		memset(&best, 0, sizeof(best));
		memset(&bestSource, 0, sizeof(bestSource));
		if (GetBestRoute2(NULL, 0, NULL, &destination, 0, &best, &bestSource) == NO_ERROR) {
			SOCKADDR_INET *nextHop = &best.NextHop;
			if (nextHop->si_family == AF_INET) {
				addr_set_binary(AF_INET, &nextHop->Ipv4.sin_addr, 0, record);
				return 0;
			}
			if (nextHop->si_family == AF_INET6) {
				addr_set_binary(AF_INET6, &nextHop->Ipv6.sin6_addr, 0, record);
				return 0;
			}
		}
		PLUM_LOG_DEBUG("GetBestRoute2 gave no usable gateway, falling back to the route table");
	}

	MIB_IPFORWARD_TABLE2 *table;
	if (GetIpForwardTable2(family, &table) != NO_ERROR) {
		PLUM_LOG_WARN("GetIpForwardTable2 failed");
		return -1;
	}

	// Lowest metric wins, rather than whichever happens to be listed first.
	MIB_IPFORWARD_ROW2 *chosen = NULL;
	for (ULONG n = 0; n < table->NumEntries; ++n) {
		MIB_IPFORWARD_ROW2 *row = table->Table + n;
		if (row->DestinationPrefix.PrefixLength != 0) {
			continue;
		}
		if (row->NextHop.si_family != AF_INET && row->NextHop.si_family != AF_INET6) {
			continue;
		}
		if (!chosen || row->Metric < chosen->Metric) {
			chosen = row;
		}
	}

	if (chosen) {
		SOCKADDR_INET *nextHop = &chosen->NextHop;
		int found = -1;
		if (nextHop->si_family == AF_INET) {
			addr_set_binary(AF_INET, &nextHop->Ipv4.sin_addr, 0, record);
			found = 0;
		} else if (nextHop->si_family == AF_INET6) {
			addr_set_binary(AF_INET6, &nextHop->Ipv6.sin6_addr, 0, record);
			found = 0;
		}
		// The table is the caller's to release on every path out, not just this one.
		FreeMibTable(table);
		return found;
	}

	PLUM_LOG_WARN("No default route found");
	FreeMibTable(table);
	return -1;

#elif defined(__linux__)
	int sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (sock == INVALID_SOCKET) {
		PLUM_LOG_WARN("Netlink socket creation failed, errno=%d", sockerrno);
		return -1;
	}

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 200000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv, sizeof(struct timeval));

	const int attempts = 3;
	for (int i = 0; i < attempts; ++i) {
		PLUM_LOG_VERBOSE("Requesting routing table via Netlink");
		char buffer[NETLINK_BUFFER_SIZE];
		memset(buffer, 0, sizeof(buffer));

		struct nlmsghdr *nlmsg = (struct nlmsghdr *)buffer;
		nlmsg->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
		nlmsg->nlmsg_type = RTM_GETROUTE;
		nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
		nlmsg->nlmsg_seq = i;
		nlmsg->nlmsg_pid = getpid();

		struct rtmsg *rtm = (struct rtmsg *)NLMSG_DATA(nlmsg);
		rtm->rtm_family = (unsigned char)family;
		rtm->rtm_dst_len = 0;
		rtm->rtm_src_len = 0;
		rtm->rtm_table = RT_TABLE_MAIN;
		rtm->rtm_type = RTN_UNICAST;
		rtm->rtm_flags = 0;

		struct sockaddr_nl snl;
		memset(&snl, 0, sizeof(snl));
		snl.nl_family = AF_NETLINK;
		snl.nl_pid = 0; // to kernel
		snl.nl_groups = 0;

		if (sendto(sock, (const char *)nlmsg, nlmsg->nlmsg_len, 0, (const struct sockaddr *)&snl,
		           sizeof(snl)) < 0) {
			PLUM_LOG_WARN("Netlink send failed, errno=%d", sockerrno);
			goto error;
		}

		socklen_t snl_len = sizeof(snl);
		int len = recvfrom(sock, buffer, NETLINK_BUFFER_SIZE, 0, (struct sockaddr *)&snl, &snl_len);
		if (len < 0) {
			if (sockerrno == SEAGAIN || sockerrno == SEWOULDBLOCK) {
				PLUM_LOG_DEBUG("Netlink recv timed out");
				continue;
			}

			PLUM_LOG_WARN("Netlink recv failed, errno=%d", sockerrno);
			goto error;
		}

		if (snl_len < (socklen_t)sizeof(snl) || snl.nl_pid != 0) {
			PLUM_LOG_WARN("Netlink received datagram not from kernel");
			continue;
		}

		PLUM_LOG_VERBOSE("Netlink received datagram, len=%d", len);

		for (nlmsg = (struct nlmsghdr *)buffer; NLMSG_OK(nlmsg, (uint32_t)len);
		     nlmsg = NLMSG_NEXT(nlmsg, len)) {

			if (nlmsg->nlmsg_type == NLMSG_DONE) {
				break;
			}

			if (nlmsg->nlmsg_type == NLMSG_ERROR) {
				PLUM_LOG_WARN("Netlink received error");
				goto error;
			}

			rtm = (struct rtmsg *)NLMSG_DATA(nlmsg);
			if ((rtm->rtm_table == RT_TABLE_MAIN || rtm->rtm_table == RT_TABLE_COMPAT) && rtm->rtm_dst_len == 0) {
				struct rtattr *rta;
				int payload_len = RTM_PAYLOAD(nlmsg);
				for (rta = (struct rtattr *)RTM_RTA(rtm); RTA_OK(rta, payload_len);
				     rta = RTA_NEXT(rta, payload_len)) {

					if (rta->rta_type == RTA_GATEWAY) {
						addr_set_binary(family, RTA_DATA(rta), 0, record);
						closesocket(sock);
						return 0;
					}
				}
			}
		}

		PLUM_LOG_WARN("No default route found");
		goto error;
	}

	PLUM_LOG_WARN("Netlink received no response after %d attempts", attempts);

error:
	closesocket(sock);
	return -1;

#elif defined(__APPLE__) && defined(HAVE_OSX_NET_ROUTE)

	// macOS always uses 4-byte alignment for sockaddrs following message header
	#define ROUNDUP(a) \
		((a) > 0 ? (1 + (((a) - 1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t))

	#define NET_MIB_INTS 6
	int mib[NET_MIB_INTS] = {CTL_NET, PF_ROUTE, 0, family, NET_RT_FLAGS, RTF_GATEWAY};

	size_t buf_len = 0;
	if (sysctl(mib, NET_MIB_INTS, NULL, &buf_len, NULL, 0) != 0) {
		PLUM_LOG_WARN("sysctl[1] failed");
		return -1;
	}
	if (buf_len == 0) {
		PLUM_LOG_WARN("sysctl[1] returned 0 buffer length");
		return -1;
	}
	char *buf = malloc(buf_len);
	if (!buf) {
		PLUM_LOG_WARN("Failed to allocate memory for buffer, size=%zu", buf_len);
		return -1;
	}
	if (sysctl(mib, NET_MIB_INTS, buf, &buf_len, NULL, 0) != 0) {
		PLUM_LOG_WARN("sysctl[2] failed");
		free(buf);
		return -1;
	}

	int ret = -1;
	struct rt_msghdr *rtm = NULL;
	char *buf_limit = (buf + buf_len);
	for (char *curr = buf; curr < buf_limit; curr += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)curr;

		if ( ((rtm->rtm_flags & (RTF_UP|RTF_GATEWAY)) != (RTF_UP|RTF_GATEWAY))
			|| ((rtm->rtm_addrs & (RTA_DST|RTA_GATEWAY)) != (RTA_DST|RTA_GATEWAY)) ) {
			continue;
		}

		struct sockaddr *sa = (struct sockaddr *)(rtm + 1);
		struct sockaddr *sa_dst = NULL;
		struct sockaddr *sa_gateway = NULL;
		for (int i = 0; i < RTAX_MAX; i++) {
			if (rtm->rtm_addrs & (1 << i)) {
				switch (i) {
				case RTAX_DST:
					sa_dst = sa;
					break;
				case RTAX_GATEWAY:
					sa_gateway = sa;
					break;
				}
				sa = (struct sockaddr *)((char *)sa + ROUNDUP(sa->sa_len));
			}
		}

		if (sa_dst->sa_family != family || sa_gateway->sa_family != family) {
			continue;
		}

		if (family == AF_INET) {
			if (((struct sockaddr_in *)sa_dst)->sin_addr.s_addr == INADDR_ANY) {
				addr_set_binary(AF_INET, &(((struct sockaddr_in *)(sa_gateway))->sin_addr), 0, record);
				ret = 0;
				break;
			}
		}
		else if (family == AF_INET6) {
			if (memcmp(&((struct sockaddr_in6 *)sa_dst)->sin6_addr, &in6addr_any, sizeof(struct in6_addr)) == 0
				&& !IN6_IS_ADDR_LINKLOCAL(&((struct sockaddr_in6 *)sa_gateway)->sin6_addr)) {
				addr_set_binary(AF_INET6, &(((struct sockaddr_in6 *)(sa_gateway))->sin6_addr), 0, record);
				ret = 0;
				break;
			}
		}
	}
	free(buf);
	if (ret == -1) {
		PLUM_LOG_WARN("No default route found");
	}
	return ret;

#else
	PLUM_LOG_WARN("Getting the default gateway is not implemented on this platform, falling back "
	              "to a wild guess");

	if (net_get_default_interface(family, record)) {
		PLUM_LOG_ERROR("Unable to get the default interface address");
		return -1;
	}

	switch (record->addr.ss_family) {
	case AF_INET: {
		// Assume a.b.c.0/24 with the gateway at a.b.c.1
		struct sockaddr_in *sin = (struct sockaddr_in *)&record->addr;
		uint8_t *b = (uint8_t *)&sin->sin_addr;
		b[3] = 1;
		return 0;
	}
	case AF_INET6: {
		// Assume xxxx::/64 with the gateway at xxxx::1
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&record->addr;
		uint8_t *b = (uint8_t *)&sin6->sin6_addr;
		for (int i = 8; i < 15; ++i)
			b[i] = 0;

		b[15] = 1;
		return 0;
	}
	default:
		PLUM_LOG_WARN("Unknown address family %d", (int)record->addr.ss_family);
		return -1;
	}
#endif
}

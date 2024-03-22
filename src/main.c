/*
	Bulwa CAN Simulator
	Copyright (C) 2024 Szymon Morawski

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "global.h"

#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

enum TimestampType
{
	TT_NONE,
	TT_TIMESTAMP,
	TT_TIMESTAMPING
};

// CAN socket
int s;

struct ScriptNode *nodes = NULL;
int nodes_num = 0;

static void nodes_init(int num);
static void nodes_deinit(void);

static void node_destroy(struct ScriptNode *node);

static int node_onenable(struct ScriptNode *node);
static int node_ondisable(struct ScriptNode *node);
static int node_onmessage(struct ScriptNode *node, struct canfd_frame *frame,
	int mtu, unsigned long long int timestamp);
static int node_ontimer(struct ScriptNode *node);

static void finalize(void);

int main(int argc, char *argv[])
{
	printf("Bulwa CAN Simulator " __DATE__ "\n"
		"Copyright (C) 2024 Szymon Morawski\n"
		"This is free software; see the source for copying conditions.  There is NO\n"
		"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");

	// parse JSON configuration file
	const char *config_path = "default.json";
	if (argc > 1)
		config_path = argv[1];
	if (RC_OK != config_load(config_path))
	{
		fprintf(stderr, "no valid json configuration found\n");
		return RC_CONFIGFILE;
	}

	// CAN socket setup
	struct ifreq ifr;
	struct sockaddr_can addr;

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0)
	{
		fprintf(stderr, "unable to create a socket\n");
		return RC_SOCKET;
	}
	const char *canif_name = config_get_canif_name();
	printf("interface %s found in %s\n\n", canif_name, config_path);
	strcpy(ifr.ifr_name, canif_name);
	ioctl(s, SIOCGIFINDEX, &ifr);

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		fprintf(stderr, "cannot bind a socket to the interface\n");
		return RC_BIND;
	}

	int recv_own_msgs = 1;
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own_msgs, sizeof(recv_own_msgs)) < 0)
	{
		fprintf(stderr, "warning: CAN_RAW_RECV_OWN_MSGS not supported\n");
	}

	can_err_mask_t err_mask = CAN_ERR_MASK;		// register for all error events
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask)) < 0)
	{
		fprintf(stderr, "warning: CAN_ERR_* not supported\n");
	}

	int enable_canfd = 1;
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd)) < 0)
	{
		if (ENOPROTOOPT == errno)
		{
			fprintf(stderr, "warning: CAN FD not supported\n");
		}
		else
		{
			fprintf(stderr, "weird thing happened: errno == %0x\n", errno);
		}
	}

	int timestamping_flags = SOF_TIMESTAMPING_SOFTWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	int timestamp_on = 1;
	enum TimestampType timestamp_type = TT_TIMESTAMPING;

	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING, &timestamping_flags, sizeof(timestamping_flags)) < 0)
	{
		fprintf(stderr, "warning: SO_TIMESTAMPING not supported\n");
		timestamp_type = TT_TIMESTAMP;
	}
	else if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMP,
		&timestamp_on, sizeof(timestamp_on)) < 0)
	{
		fprintf(stderr, "warning: SO_TIMESTAMP not supported\n");
		timestamp_type = TT_NONE;
	}

	// to do - count dropped frames (SO_RXQ_OVFL)

	// load node configuration
	int nodenum = config_get_node_num();
	nodes_init(nodenum);
	int err = RC_OK;
	for (int i = 0; i < nodenum; ++i)
	{
		err = config_load_node(i, &nodes[i]);
		if (err != RC_OK)
			return RC_INIT;
	}

	// enable nodes
	for (int i = 0; i < nodenum; ++i)
	{
		if (nodes[i].enabled)
			node_enable(&nodes[i]);
	}

	atexit(finalize);

	struct pollfd fds;
	fds.fd = s;
	fds.events = POLLIN;

	while (1)
	{
		if (poll(&fds, 1, 50) > 0)
		{
			if (fds.revents & POLLIN)
			{
				// there is data to read
				struct msghdr msg;
				struct iovec iov;
				char ctrlmsg[CMSG_SPACE(sizeof(struct timeval)) +
					CMSG_SPACE(3 * sizeof(struct timespec))];
				struct canfd_frame frame;

				memset(&msg, 0, sizeof(msg));
				iov.iov_base = &frame;
				iov.iov_len = sizeof(frame);
				msg.msg_iov = &iov;
				msg.msg_iovlen = 1;
				msg.msg_control = ctrlmsg;
				msg.msg_controllen = sizeof(ctrlmsg);

				int nbytes = recvmsg(fds.fd, &msg, 0);
				if (nbytes < 0)
				{
					fprintf(stderr, "recvmsg error\n");
					return RC_SOCKETREAD;
				}

				unsigned long long int timestamp = 0;
				if (msg.msg_control && msg.msg_controllen)
				{
					for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
						cmsg && (cmsg->cmsg_level == SOL_SOCKET);
						cmsg = CMSG_NXTHDR(&msg,cmsg))
					{
						if (timestamp_type == TT_TIMESTAMP && cmsg->cmsg_type == SO_TIMESTAMP)
						{
							struct timeval *tv = (struct timeval *)CMSG_DATA(cmsg);
							timestamp = tv->tv_usec * 1000 + tv->tv_sec * 1000000000;
						} else if (timestamp_type == TT_TIMESTAMPING && cmsg->cmsg_type == SO_TIMESTAMPING)
						{
							struct timespec *stamp = (struct timespec *)CMSG_DATA(cmsg);
							/*
							 * stamp[0] is the software timestamp
							 * stamp[1] is deprecated
							 * stamp[2] is the raw hardware timestamp
							 * See chapter 2.1.2 Receive timestamps in
							 * linux/Documentation/networking/timestamping.txt
							 */
							//if (stamp[2].tv_nsec || stamp[2].tv_sec)
							//	stamp += 2;		// read timestamp from stamp[2]
							timestamp = stamp->tv_nsec + stamp->tv_sec * 1000000000;
						}
					}
				}

				// on_message callback
				for (int i = 0; i < nodenum; ++i)
				{
					if (nodes[i].enabled)
						node_onmessage(&nodes[i], &frame, nbytes, timestamp);
				}
			}
			else if (fds.revents & POLLERR)
			{
				fprintf(stderr, "error reading from socket, have you forgot to set bitrate and set up %s?\n", canif_name);
				return RC_SOCKETREAD;
			}
			else
			{
				fprintf(stderr, "weird thing happened: revents == %0x\n", fds.revents);
			}
		}
		else
		{
			// timeout
		}

		// on_timer callback
		for (int i = 0; i < nodenum; ++i)
		{
			if (nodes[i].enabled && nodes[i].timer_interval)
			{
				struct itimerspec ts;
				timer_gettime(nodes[i].timerid, &ts);
				if (0 == ts.it_value.tv_sec && 0 == ts.it_value.tv_nsec)
				{
					node_ontimer(&nodes[i]);
				}
			}
		}

		// check if any of nodes is enabled
		bool all_dead = true;
		for (int i = 0; i < nodenum; ++i)
		{
			if (nodes[i].enabled)
			{
				all_dead = false;
				break;
			}
		}
		if (all_dead)
		{
			printf("All nodes are disabled. Graceful exit.\n");
			break;
		}
	}

	return 0;
}

static void finalize(void)
{
	/* needed to do as atexit callback as
	 * a Lua script can exit the process as well
	 */
	config_unload();
	close(s);

	for (int i = 0; i < nodes_num; ++i)
	{
		if (nodes[i].enabled)
			node_disable(&nodes[i]);
	}

	for (int i = 0; i < nodes_num; ++i)
	{
		node_destroy(&nodes[i]);
	}
}

static void nodes_init(int num)
{
	nodes = (struct ScriptNode *)malloc(num * sizeof(struct ScriptNode));
	memset(nodes, 0, num * sizeof(struct ScriptNode));
	nodes_num = num;
}

static void nodes_deinit(void)
{
	free(nodes);
	nodes = NULL;
}

static void node_destroy(struct ScriptNode *node)
{
	if (node->lua)
		lua_close(node->lua);
	node->lua = NULL;
	if (node->timer_interval)
		timer_delete(node->timerid);
	node->timer_interval = 0;
}

void node_enable(struct ScriptNode *node)
{
	node->enabled = true;
	node_onenable(node);
}

void node_disable(struct ScriptNode *node)
{
	node->enabled = false;
	node_ondisable(node);
	node_set_timer(node, 0);
}

void node_set_timer(struct ScriptNode *node, lua_Integer interval)
{
	if (0 == node->timer_interval && interval)
	{
		struct sigevent sev;
		memset(&sev, 0, sizeof(sev));
		sev.sigev_notify = SIGEV_NONE;
		int err = timer_create(CLOCK_MONOTONIC, &sev, &node->timerid);
		if (0 == err)
		{
			node->timer_interval = interval;
		}
		else
		{
			fprintf(stderr, "timer NOT created\n");
		}
	}

	if (node->timer_interval)
	{
		node->timer_interval = interval;

		if (0 == interval)
		{
			timer_delete(node->timerid);
		}
		else
		{
			struct itimerspec ts;
			memset(&ts, 0, sizeof(ts));
			ts.it_value.tv_sec = interval / 1000;
			ts.it_value.tv_nsec = (interval % 1000) * 1000000;
			int err = timer_settime(node->timerid, 0, &ts, NULL);
			if (err < 0)
			{
				fprintf(stderr, "timer NOT set\n");
			}
		}
	}
}

static int node_onenable(struct ScriptNode *node)
{
	int err = 0;
	int rettype = lua_getglobal(node->lua, "on_enable");
	if (LUA_TFUNCTION == rettype)
	{
		err = lua_pcall(node->lua, 0, 0, 0);
		if (err)
		{
			fprintf(stderr, "%s\n", lua_tostring(node->lua, -1));
			return RC_CALL;
		}
	}
	else
	{
		printf("warning: no valid on_enable function for node %s\n", node->name);
		lua_pop(node->lua, 1);
	}
	return RC_OK;
}

static int node_ondisable(struct ScriptNode *node)
{
	int err = 0;
	int rettype = lua_getglobal(node->lua, "on_disable");
	if (LUA_TFUNCTION == rettype)
	{
		err = lua_pcall(node->lua, 0, 0, 0);
		if (err)
		{
			fprintf(stderr, "%s\n", lua_tostring(node->lua, -1));
			return RC_CALL;
		}
	}
	else
	{
		printf("warning: no valid on_disable function for node %s\n", node->name);
		lua_pop(node->lua, 1);
	}
	return RC_OK;
}

static int node_onmessage(struct ScriptNode *node, struct canfd_frame *frame, int mtu, unsigned long long int timestamp)
{
	int err = 0;
	int rettype = lua_getglobal(node->lua, "on_message");
	if (LUA_TFUNCTION == rettype)
	{
		int dlc = 0;
		unsigned int canfd_flags = 0;
		if (mtu == CANFD_MTU)
		{
			canfd_flags = frame->flags;
		}
		else if (mtu == CAN_MTU)
		{
			dlc = ((struct can_frame *)frame)->len8_dlc;
		}

		lua_newtable(node->lua);
		// message type
		lua_pushstring(node->lua, "type");
		if (CAN_MTU == mtu)
			lua_pushstring(node->lua, "CAN");
		else if (CANFD_MTU == mtu)
			lua_pushstring(node->lua, "CANFD");
		else
			lua_pushstring(node->lua, "unknown");
		lua_settable(node->lua, -3);
		// timestamp in nanoseconds
		lua_pushstring(node->lua, "timestamp");
		lua_pushinteger(node->lua, timestamp);
		lua_settable(node->lua, -3);
		// frame format flag (0 = standard 11 bit, 1 = extended 29 bit)
		bool eff = frame->can_id & CAN_EFF_FLAG;
		lua_pushstring(node->lua, "eff");
		lua_pushboolean(node->lua, eff);
		lua_settable(node->lua, -3);
		// can id
		canid_t id = frame->can_id & (eff ? CAN_EFF_MASK : CAN_SFF_MASK);
		lua_pushstring(node->lua, "id");
		lua_pushinteger(node->lua, id);
		lua_settable(node->lua, -3);
		// remote transmission request flag (1 = rtr frame)
		lua_pushstring(node->lua, "rtr");
		lua_pushboolean(node->lua, frame->can_id & CAN_RTR_FLAG);
		lua_settable(node->lua, -3);
		// error message frame flag (0 = data frame, 1 = error message)
		lua_pushstring(node->lua, "err");
		lua_pushboolean(node->lua, frame->can_id & CAN_ERR_FLAG);
		lua_settable(node->lua, -3);
		// [CAN] optional DLC for 8 byte payload length (9..15)
		// legacy field, do not use it to check the frame length
		lua_pushstring(node->lua, "dlc");
		lua_pushinteger(node->lua, dlc);
		lua_settable(node->lua, -3);
		// [CANFD] bit rate switch flag (second bitrate for payload data)
		lua_pushstring(node->lua, "brs");
		lua_pushboolean(node->lua, canfd_flags & CANFD_BRS);
		lua_settable(node->lua, -3);
		// [CANFD] error state indicator of the transmitting node
		lua_pushstring(node->lua, "esi");
		lua_pushboolean(node->lua, canfd_flags & CANFD_ESI);
		lua_settable(node->lua, -3);
		// payload
		for (int i = 0; i < frame->len; ++i)
		{
			lua_pushinteger(node->lua, i + 1);
			lua_pushinteger(node->lua, frame->data[i]);
			lua_settable(node->lua, -3);
		}
		// callback function call
		err = lua_pcall(node->lua, 1, 0, 0);
		if (err)
		{
			fprintf(stderr, "%s\n", lua_tostring(node->lua, -1));
			return RC_CALL;
		}
	}
	else
	{
		printf("warning: no valid on_message function for node %s\n", node->name);
		lua_pop(node->lua, 1);
	}
	return RC_OK;
}

static int node_ontimer(struct ScriptNode *node)
{
	int err = 0;
	int rettype = lua_getglobal(node->lua, "on_timer");
	if (LUA_TFUNCTION == rettype)
	{
		lua_pushinteger(node->lua, node->timer_interval);
		node->timer_interval = -1;	// temporary marker
		err = lua_pcall(node->lua, 1, 1, 0);
		if (err)
		{
			fprintf(stderr, "%s\n", lua_tostring(node->lua, -1));
			return RC_CALL;
		}
		if (!lua_isnil(node->lua, -1))
		{
			// if we return anything, set it as interval
			// Number supports fractions of milliseconds
			lua_Integer interval = (lua_Integer)lua_tonumber(node->lua, -1);
			node_set_timer(node, interval);
		}
		else
		{
			if (node->timer_interval < 0)
			{
				// interval was not changed via set_timer in on_timer callback
				// so we can remove the timer
				node_set_timer(node, 0);
			}
		}
		// pop a function argument
		lua_pop(node->lua, 1);
	}
	else
	{
		printf("warning: no valid on_timer function for node %s\n", node->name);
		node_set_timer(node, 0);
		lua_pop(node->lua, 1);
	}
	return RC_OK;
}

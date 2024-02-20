#include "global.h"

#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/net_tstamp.h>

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
static int node_onmessage(struct ScriptNode *node, struct canfd_frame *frame, int mtu, unsigned long long int timestamp);

static void finalize(void);

int main(int argc, char *argv[])
{
	printf("Bulwa CAN Simulator " __DATE__ "\n"
		"Copyright (C) 2024 Szymon Morawski\n"
		"This is free software; see the source for copying conditions.  There is NO\n"
		"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");

	// parse JSON configuration file
	if (RC_OK != config_load("config.json"))
	{
		fprintf(stderr, "no valid json configuration found\n");
		return RC_CONFIGFILE;
	}

	// CAN socket setup
	struct ifreq ifr;
	struct sockaddr_can addr;

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0)
		return RC_SOCKET;
	const char *canif_name = config_get_canif_name();
	printf("interface %s found in config\n\n", canif_name);
	strcpy(ifr.ifr_name, canif_name);
	ioctl(s, SIOCGIFINDEX, &ifr);

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return RC_BIND;
	int enable_canfd = 1;
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd)) < 0)
	{
		if (ENOPROTOOPT == errno)
		{
			printf("warning: CAN FD not supported\n");
		}
		else
		{
			printf("weird thing happened: errno == %0x\n", errno);
		}
	}

	int timestamping_flags = SOF_TIMESTAMPING_SOFTWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	int timestamp_on = 1;
	enum TimestampType timestamp_type = TT_TIMESTAMPING;

	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING,
		&timestamping_flags, sizeof(timestamping_flags)) < 0)
	{
		printf("warning: SO_TIMESTAMPING not supported\n");
		timestamp_type = TT_TIMESTAMP;
	}
	else if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMP,
		&timestamp_on, sizeof(timestamp_on)) < 0)
	{
		printf("warning: SO_TIMESTAMP not supported\n");
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
		if (poll(&fds, 1, 100) > 0)
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
							if (stamp[2].tv_nsec || stamp[2].tv_sec)
								stamp += 2;		// read timestamp from stamp[2]
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
				printf("weird thing happened: revents == %0x\n", fds.revents);
			}
		}
		else
		{
			// timeout
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

void nodes_init(int num)
{
	nodes = (struct ScriptNode *)malloc(num * sizeof(struct ScriptNode));
	nodes_num = num;
}

void nodes_deinit(void)
{
	free(nodes);
	nodes = NULL;
}

void node_destroy(struct ScriptNode *node)
{
	lua_close(node->lua);
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
}

int node_onenable(struct ScriptNode *node)
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

int node_ondisable(struct ScriptNode *node)
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

int node_onmessage(struct ScriptNode *node, struct canfd_frame *frame, int mtu, unsigned long long int timestamp)
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
		// mtu - (16 - CAN, 72 - CAN FD)
		lua_pushstring(node->lua, "mtu");
		lua_pushinteger(node->lua, mtu);
		lua_settable(node->lua, -3);
		// timestamp in nanoseconds
		lua_pushstring(node->lua, "timestamp");
		lua_pushinteger(node->lua, timestamp);
		lua_settable(node->lua, -3);
		// frame format flag (0 = standard 11 bit, 1 = extended 29 bit)
		unsigned int eff = frame->can_id & CAN_EFF_FLAG;
		lua_pushstring(node->lua, "eff");
		lua_pushboolean(node->lua, eff);
		lua_settable(node->lua, -3);
		// can id
		unsigned int id = frame->can_id & (eff ? CAN_EFF_MASK : CAN_SFF_MASK);
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
		// frame length
		lua_pushstring(node->lua, "len");
		lua_pushinteger(node->lua, frame->len);
		lua_settable(node->lua, -3);
		// payload
		for (int i = 0; i < frame->len; ++i)
		{
			lua_pushinteger(node->lua, i);
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

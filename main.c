#include "global.h"

#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

struct ScriptNode *nodes = NULL;
int nodes_num = 0;

static void nodes_init(int num);
static void nodes_deinit(void);

static void node_destroy(struct ScriptNode *node);

static int node_onenable(struct ScriptNode *node);
static int node_ondisable(struct ScriptNode *node);
static int node_onmessage(struct ScriptNode *node, struct canfd_frame *frame, int mtu);

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
	int s;
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
				struct canfd_frame frame;
				int nbytes = read(fds.fd, &frame, CANFD_MTU);
				if (nbytes < 0)
					return RC_SOCKETREAD;
				// on_message callback
				for (int i = 0; i < nodenum; ++i)
				{
					if (nodes[i].enabled)
						node_onmessage(&nodes[i], &frame, nbytes);
				}
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

	config_unload();
	close(s);

	for (int i = 0; i < nodenum; ++i)
	{
		node_destroy(&nodes[i]);
	}

	return 0;
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

int node_onmessage(struct ScriptNode *node, struct canfd_frame *frame, int mtu)
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

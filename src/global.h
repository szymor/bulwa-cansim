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

#ifndef _H_GLOBAL
#define _H_GLOBAL

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/net_tstamp.h>
#include <linux/can/error.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

enum ReturnCode
{
	RC_OK,
	RC_CONFIGFILE,
	RC_LOADFILE,
	RC_CALL,
	RC_INIT,
	RC_SOCKET,
	RC_BIND,
	RC_SOCKETREAD,
	RC_END
};

struct ScriptNode
{
	char *name;
	lua_State *lua;
	bool enabled;
	lua_Integer timer_interval;
	timer_t timerid;
};

extern int s;
extern struct ScriptNode *nodes;
extern int nodes_num;

void luaenv_add_custom_api(lua_State *lua, int node_id);

void node_enable(struct ScriptNode *node);
void node_disable(struct ScriptNode *node);
void node_set_timer(struct ScriptNode *node, lua_Integer interval);

int config_load(const char *path);
int config_get_node_num(void);
int config_load_node(int idx, struct ScriptNode *node);
void config_unload(void);
const char *config_get_canif_name(void);

#endif

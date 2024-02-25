#include "global.h"

static int luaenv_enablenode(lua_State *lua);
static int luaenv_disablenode(lua_State *lua);
static int luaenv_settimer(lua_State *lua);
static int luaenv_emit(lua_State *lua);

void luaenv_add_custom_api(lua_State *lua, int node_id)
{
	lua_pushstring(lua, nodes[node_id].name);
	lua_setglobal(lua, "node_name");

	lua_pushinteger(lua, node_id);
	lua_setglobal(lua, "node_id");

	lua_pushcfunction(lua, luaenv_enablenode);
	lua_setglobal(lua, "enable_node");

	lua_pushcfunction(lua, luaenv_disablenode);
	lua_setglobal(lua, "disable_node");

	lua_pushcfunction(lua, luaenv_settimer);
	lua_setglobal(lua, "set_timer");

	lua_pushcfunction(lua, luaenv_emit);
	lua_setglobal(lua, "emit");
}

static int luaenv_enablenode(lua_State *lua)
{
	const char *node_name = luaL_checkstring(lua, 1);
	for (int i = 0; i < nodes_num; ++i)
	{
		if (!strcmp(nodes[i].name, node_name))
		{
			if (!nodes[i].enabled)
			{
				node_enable(&nodes[i]);
			}
		}
	}
	return 0;
}

static int luaenv_disablenode(lua_State *lua)
{
	const char *node_name = luaL_checkstring(lua, 1);
	for (int i = 0; i < nodes_num; ++i)
	{
		if (!strcmp(nodes[i].name, node_name))
		{
			if (nodes[i].enabled)
			{
				node_disable(&nodes[i]);
			}
		}
	}
	return 0;
}

static int luaenv_settimer(lua_State *lua)
{
	lua_Integer interval = (lua_Integer)luaL_checknumber(lua, 1);
	int rettype = lua_getglobal(lua, "node_id");
	if (LUA_TNUMBER == rettype)
	{
		int id = lua_tointeger(lua, -1);
		node_set_timer(&nodes[id], interval);
	}
	lua_pop(lua, 1);
	return 0;
}

static int luaenv_emit(lua_State *lua)
{
	struct canfd_frame frame;
	struct can_frame *fptr = (struct can_frame *)&frame;

	// discard any extra arguments passed in
	lua_settop(lua, 1);
	luaL_checktype(lua, 1, LUA_TTABLE);

	lua_getfield(lua, 1, "mtu");
	lua_getfield(lua, 1, "id");
	lua_getfield(lua, 1, "len");
	lua_getfield(lua, 1, "dlc");	// CAN only, do not use unless you know what you are doing
	lua_getfield(lua, 1, "eff");
	lua_getfield(lua, 1, "rtr");
	lua_getfield(lua, 1, "err");
	lua_getfield(lua, 1, "brs");	// CAN FD only
	lua_getfield(lua, 1, "esi");	// CAN FD only

	int mtu = luaL_optinteger(lua, -9, CAN_MTU);
	frame.can_id = luaL_checkinteger(lua, -8);
	frame.len = luaL_checkinteger(lua, -7);
	if (CAN_MTU == mtu)
		fptr->len8_dlc = luaL_optinteger(lua, -6, frame.len);
	bool eff_flag = lua_toboolean(lua, -5);
	bool rtr_flag = lua_toboolean(lua, -4);
	bool err_flag = lua_toboolean(lua, -3);
	bool brs_flag = lua_toboolean(lua, -2);
	bool esi_flag = lua_toboolean(lua, -1);

	// free the stack
	lua_pop(lua, 9);

	// if id is in the extended range, set eff_flag automatically
	if (frame.can_id & ~CAN_SFF_MASK)
		eff_flag = true;

	// add proper flags to id if needed
	if (eff_flag)
	{
		frame.can_id &= CAN_EFF_MASK;
		frame.can_id |= CAN_EFF_FLAG;
	}
	else
	{
		frame.can_id &= CAN_SFF_MASK;
	}

	if (rtr_flag)
		frame.can_id |= CAN_RTR_FLAG;
	if (err_flag)
		frame.can_id |= CAN_ERR_FLAG;

	// CAN FD flags
	frame.flags = 0;
	if (brs_flag)
		frame.flags |= CANFD_BRS;
	if (esi_flag)
		frame.flags |= CANFD_ESI;

	// fill in payload
	for (int i = 0; i < frame.len; ++i)
	{
		lua_pushinteger(lua, i);
		lua_gettable(lua, 1);
		frame.data[i] = luaL_checkinteger(lua, -1);
		lua_pop(lua, 1);
	}

	int nbytes = write(s, &frame, mtu);
	if (nbytes != CAN_MTU && nbytes != CANFD_MTU)
	{
		fprintf(stderr, "critical: cannot send a message\n");
	}
	return 0;
}

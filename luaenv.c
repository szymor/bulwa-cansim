#include "global.h"

static int luaenv_enablenode(lua_State *lua);
static int luaenv_disablenode(lua_State *lua);
static int luaenv_settimer(lua_State *lua);

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

#ifndef _H_GLOBAL
#define _H_GLOBAL

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
};

int config_load(const char *path);
int config_get_node_num(void);
int config_load_node(int idx, struct ScriptNode *node);
void config_unload(void);

#endif

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
	lua_Integer timer_interval;
	timer_t timerid;
};

extern struct ScriptNode *nodes;
extern int nodes_num;

void luaenv_add_custom_api(lua_State *lua, const char *node_name);

void node_enable(struct ScriptNode *node);
void node_disable(struct ScriptNode *node);
void node_set_timer(struct ScriptNode *node, lua_Integer interval);

int config_load(const char *path);
int config_get_node_num(void);
int config_load_node(int idx, struct ScriptNode *node);
void config_unload(void);
const char *config_get_canif_name(void);

#endif

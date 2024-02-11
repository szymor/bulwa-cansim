#include "global.h"

#include <cjson/cJSON.h>

static cJSON *config = NULL;

int config_load(const char *path)
{
	FILE *file = fopen(path, "r");
	if (!file)
		return RC_CONFIGFILE;
	fseek(file, 0L, SEEK_END);
	long int sz = ftell(file);
	rewind(file);

	char *config_string = (char *)malloc(sz);
	fread(config_string, sz, 1, file);

	config = cJSON_ParseWithLength(config_string, sz);

	free(config_string);
	fclose(file);

	return RC_OK;
}

void config_unload(void)
{
	if (config)
		free(config);
	config = NULL;
}

int config_get_node_num(void)
{
	cJSON *nodes_array = cJSON_GetObjectItem(config, "nodes");
	if (!nodes_array)
		return 0;
	return cJSON_GetArraySize(nodes_array);
}

int config_load_node(int idx, struct ScriptNode *node)
{
	cJSON *nodes_array = cJSON_GetObjectItem(config, "nodes");
	if (!nodes_array)
		return RC_CONFIGFILE;
	cJSON *node_item = cJSON_GetArrayItem(nodes_array, idx);
	if (!node_item)
		return RC_CONFIGFILE;

	// name string
	cJSON *name_string_item = cJSON_GetObjectItem(node_item, "name");
	node->name = cJSON_GetStringValue(name_string_item);

	// enabled flag (turned on by default)
	cJSON *enabled_flag_item = cJSON_GetObjectItem(node_item, "enabled");
	node->enabled = !cJSON_IsFalse(enabled_flag_item);

	// path string
	cJSON *path_string_item = cJSON_GetObjectItem(node_item, "path");
	char *script_path = cJSON_GetStringValue(path_string_item);

	// initialize Lua environment
	int err = RC_OK;
	node->lua = luaL_newstate();
	luaL_openlibs(node->lua);
	luaenv_add_custom_api(node->lua, node->name);
	err = luaL_loadfile(node->lua, script_path);
	if (err)
	{
		fprintf(stderr, "%s: loadfile error\n", node->name);
		return RC_LOADFILE;
	}
	err = lua_pcall(node->lua, 0, 0, 0);
	if (err)
	{
		fprintf(stderr, "%s: %s\n", node->name, lua_tostring(node->lua, -1));
		return RC_CALL;
	}
	return RC_OK;
}

const char *config_get_canif_name(void)
{
	cJSON *canif_item = cJSON_GetObjectItem(config, "canif");
	if (!canif_item)
		return NULL;
	cJSON *name_item = cJSON_GetObjectItem(canif_item, "name");
	if (!name_item)
		return NULL;
	return cJSON_GetStringValue(name_item);
}

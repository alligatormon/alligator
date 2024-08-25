#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include "lang/type.h"
#include "events/fs_read.h"
#include "common/logs.h"

char* lua_run_script(lang_options *lo, char *code, char *key, uint64_t log_level, char *method, char *arg, string* smetrics, string *conf, string *parser_data, string *response)
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	char *metrics = NULL;

	char *metrics_str = smetrics ? smetrics->s : "";
	char *conf_str = conf ? conf->s : "";
	char *parser_data_str = parser_data ? parser_data->s : "";

	if (code)
	{
		if (luaL_loadstring(L, code) == LUA_OK) {
			if (lua_pcall(L, 0, 0, 0) == LUA_OK) {

				lua_pop(L, lua_gettop(L));
			}
		}

		lua_getglobal(L, method);
		if (lua_type(L, -1) == LUA_TFUNCTION)
		{
			lua_pushstring(L, arg);
			lua_pushstring(L, metrics_str);
			lua_pushstring(L, conf_str);
			lua_pushstring(L, parser_data_str);
			//lua_pushstring(L, response_str);
			if (lua_pcall(L, 4, 2, 0)) // L, numArgs, numRets, errFuncIndex
			{
				langlog(lo, L_ERROR, "lua_pcall failed: %s\n", lua_tostring(L, -1));

				lua_pop(L, 1);
			}
		}
		if (lua_isstring(L, -1)) {
			char* response_from_lua = (char*)lua_tostring(L, -1);

			if (response_from_lua && response)
				string_cat(response, response_from_lua, strlen(response_from_lua));
			lua_pop(L, 1);

			langlog(lo, L_ERROR, "Lang lua exec key '%s' response result:\n'%s'\n", key, response_from_lua);
		}
		if (lua_isstring(L, -1)) {
			char* metrics_from_lua = (char*)lua_tostring(L, -1);

			if (metrics_from_lua)
				metrics = strdup(metrics_from_lua);
			lua_pop(L, 1);

			langlog(lo, L_ERROR, "Lang lua exec key '%s' metrics result:\n'%s'\n", key, metrics);
		}
	}

	lua_close(L);

	return metrics;
}

char* lua_run(lang_options *lo, char* script, char *file, char *arg, string* smetrics, string *conf, string *parser_data, string *response)
{
	char *metrics = NULL;
	if (script)
		metrics = lua_run_script(lo, script, lo->key, lo->log_level, lo->method, lo->arg, smetrics, conf, parser_data, response);
	else if (file)
		read_from_file(strdup(file), 0, lang_load_script, lo);
	else
	{
		langlog(lo, L_ERROR, "no file or script for lua lang: %s\n", lo->key);
	}

	return metrics;
}


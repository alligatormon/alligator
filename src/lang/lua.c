#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include "lang/lang.h"

char* lua_run_script(char *code, char *key, uint64_t log_level, char *method, char *arg, string* smetrics, string *conf)
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	char *metrics = NULL;

	char *metrics_str = smetrics ? smetrics->s : "";
	char *conf_str = conf ? conf->s : "";

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
			if (lua_pcall(L, 3, 1, 0)) // L, numArgs, numRets, errFuncIndex
			{
				if (log_level)
					printf("lua_pcall failed: %s\n", lua_tostring(L, -1));

				lua_pop(L, 1);
			}
		}
		if (lua_isstring(L, -1)) {
			char* metrics_from_lua = (char*)lua_tostring(L, -1);

			if (metrics_from_lua)
				metrics = strdup(metrics_from_lua);
			lua_pop(L, 1);

			if (log_level)
				printf("Lang lua exec key '%s' result:\n'%s'\n", key, metrics);
		}
	}

	lua_close(L);

	return metrics;
}

char* lua_run(lang_options *lo, char* script, char *file, char *arg, string* smetrics, string *conf)
{
	char *metrics = NULL;
	if (script)
		metrics = lua_run_script(script, lo->key, lo->log_level, lo->method, lo->arg, smetrics, conf);
	else if (file)
		read_from_file(strdup(file), 0, lang_load_script, lo);
	else
	{
		if (lo->log_level)
			printf("no file or script for lua lang: %s\n", lo->key);
	}

	return metrics;
}


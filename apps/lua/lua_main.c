#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <string.h>

static void open_liwus_libs(lua_State *L) {
  luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_IOLIBNAME, luaopen_io, 1);
  lua_pop(L, 1);
}

static int report(lua_State *L, int status) {
  if (status != LUA_OK) {
    const char *msg = lua_tostring(L, -1);
    printf("lua: %s\n", msg ? msg : "erro");
    lua_pop(L, 1);
  }
  return status;
}

int main(int argc, char **argv) {
  lua_State *L;
  int status;

  if (argc < 2 || !argv[1]) {
    printf("Usage: lua <arquivo.lua>\n");
    return 1;
  }

  L = luaL_newstate();
  if (!L) {
    printf("lua: falha ao criar estado\n");
    return 1;
  }

  open_liwus_libs(L);

  lua_createtable(L, argc - 1, 0);
  for (int i = 1; i < argc; i++) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, i - 1);
  }
  lua_setglobal(L, "arg");

  status = luaL_loadfile(L, argv[1]);
  if (report(L, status) == LUA_OK) {
    status = lua_pcall(L, 0, LUA_MULTRET, 0);
    report(L, status);
  }

  lua_close(L);
  return status == LUA_OK ? 0 : 1;
}

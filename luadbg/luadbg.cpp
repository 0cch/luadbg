// luadbg.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "lua.hpp"
#include "lauxlib.h"

class EXT_CLASS : public ExtExtension
{
public:
	EXT_COMMAND_METHOD(lua);
};

EXT_DECLARE_GLOBALS();

static int dbgprint(lua_State* L) 
{
	int nargs = lua_gettop(L);
	for (int i=1; i <= nargs; ++i) {
		g_ExtInstancePtr->Out(lua_tostring(L, i));
	}
	g_ExtInstancePtr->Out("\r\n");

	return 0;
}

static const struct luaL_Reg printlib[] = {
	{"print", dbgprint},
	{NULL, NULL} /* end of array */
};

void SetDefaultPrint(lua_State* L)
{
	lua_getglobal(L, "_G");
	luaL_setfuncs(L, printlib, 0);
	lua_pop(L, 1);
}

typedef struct _DBG_MODULE {
	ULONG64 ModuleAddr;
	CHAR ModuleName[1024];
} DBG_MODULE;

static int dbgmodule_new(lua_State *L)
{
	const char *module_name = luaL_checkstring(L, 1);
	ULONG index = 0;
	ULONG64 addr = 0;
	if (FAILED(g_ExtInstancePtr->m_Symbols->GetModuleByModuleName(module_name, 0, &index, &addr)) || addr == 0) {
		lua_pushnil(L);
		return 1;
	}

	DBG_MODULE *m = (DBG_MODULE *)lua_newuserdata(L, sizeof(DBG_MODULE));
	
	luaL_getmetatable(L, "dbgmodule.metatable");
	lua_setmetatable(L, -2);

	m->ModuleAddr = addr;
	strcpy_s(m->ModuleName, module_name);
	
	return 1;
}

static DBG_MODULE *dbgmodule_check (lua_State *L) 
{
	void *ud = luaL_checkudata(L, 1, "dbgmodule.metatable");
	luaL_argcheck(L, ud != NULL, 1, "`dbgmodule' expected");
	return (DBG_MODULE *)ud;
}

static int dbgmodule_getname (lua_State *L) {
	DBG_MODULE *m = dbgmodule_check(L);
	lua_pushstring(L, m->ModuleName);
	return 1;
}

static int dbgmodule_getaddr (lua_State *L) {
	DBG_MODULE *m = dbgmodule_check(L);
	lua_pushinteger(L, m->ModuleAddr);
	return 1;
}

static const struct luaL_Reg arraylib_f [] = {
	{"new", dbgmodule_new},
	{NULL, NULL}
};

static const struct luaL_Reg arraylib_m [] = {
	{"addr", dbgmodule_getaddr},
	{"name", dbgmodule_getname},
	{NULL, NULL}
};

static int dbgmodule___index(lua_State *L)
{
	const char *member_name = luaL_checkstring(L, -1);

	lua_getmetatable(L, -2);
	lua_pushstring(L, member_name);
	lua_rawget(L,-2);
	lua_remove(L,-2);
	if (lua_isfunction(L, -1))
	{
		lua_remove(L, -2);
		return 1;
	}
	DBG_MODULE *m = dbgmodule_check(L);
	const char* key = member_name;

	CStringA sym_name;
	sym_name.Format("%s!%s", m->ModuleName, key);

	ULONG64 off = 0;
	if (FAILED(g_ExtInstancePtr->m_Symbols->GetOffsetByName(sym_name.GetString(), &off))) {
		lua_pushstring(L, "cannot find symbol.");
		return 1;
	}

	lua_pushinteger(L, off);

	return 1;
}

int luaopen_dbgmodule(lua_State *L) 
{
	int p = luaL_newmetatable(L, "dbgmodule.metatable");
	
	lua_pushstring(L, "addr");
	lua_pushcfunction(L, dbgmodule_getaddr);
	lua_settable(L, -3);

	lua_pushstring(L, "name");
	lua_pushcfunction(L, dbgmodule_getname);
	lua_settable(L, -3);

	lua_pushstring(L, "__index");
	lua_pushcfunction(L, dbgmodule___index);
	lua_settable(L, -3);

	lua_newtable(L);
	luaL_setfuncs(L, arraylib_f, 0);
	lua_setglobal(L, "dbgmodule");
	return 1;
}

EXT_COMMAND(lua,
	"Execute lua file.",
	"{;x,r;path;Execute lua file path.}")
{
	LPCSTR path = GetUnnamedArgStr(0);
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	SetDefaultPrint(L);
	luaopen_dbgmodule(L);
	
	if (luaL_dofile(L, path) != 0) {
		Err("lua error: %s\r\n", lua_tostring(L, -1));
	}
	
	lua_close(L);
}
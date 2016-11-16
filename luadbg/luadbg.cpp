// luadbg.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "lua.hpp"
#include "lauxlib.h"

class EXT_CLASS : public ExtExtension
{
public:
	EXT_COMMAND_METHOD(lua);
	EXT_COMMAND_METHOD(luacmd);
};

EXT_DECLARE_GLOBALS();

static int dbgprint(lua_State* L) 
{
	int nargs = lua_gettop(L);
	for (int i=1; i <= nargs; ++i) {
		g_Ext->Out(lua_tostring(L, i));
	}
	g_Ext->Out("\r\n");

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


/************************************************************************/
/*                        dbgmodule                                     */
/************************************************************************/


typedef struct _DBG_MODULE {
	ULONG64 ModuleAddr;
	CHAR ModuleName[1024];
} DBG_MODULE;

static int dbgmodule_new(lua_State *L)
{
	const char *module_name = luaL_checkstring(L, 1);
	ULONG index = 0;
	ULONG64 addr = 0;
	if (FAILED(g_Ext->m_Symbols->GetModuleByModuleName(module_name, 0, &index, &addr)) || addr == 0) {
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

static DBG_MODULE *dbgmodule_check(lua_State *L) 
{
	void *ud = luaL_checkudata(L, 1, "dbgmodule.metatable");
	luaL_argcheck(L, ud != NULL, 1, "`dbgmodule' expected");
	return (DBG_MODULE *)ud;
}

static int dbgmodule_getname(lua_State *L)
{
	DBG_MODULE *m = dbgmodule_check(L);
	lua_pushstring(L, m->ModuleName);
	return 1;
}

static int dbgmodule_getaddr(lua_State *L)
{
	DBG_MODULE *m = dbgmodule_check(L);
	lua_pushinteger(L, m->ModuleAddr);
	return 1;
}

static const struct luaL_Reg dbgmodule_lib [] = {
	{"new", dbgmodule_new},
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
	if (FAILED(g_Ext->m_Symbols->GetOffsetByName(sym_name.GetString(), &off))) {
		lua_pushstring(L, "cannot find symbol.");
		return 1;
	}

	lua_pushinteger(L, off);

	return 1;
}

static const struct luaL_Reg dbgmodule_func[] = {
	{ "__index", dbgmodule___index },
	{ "addr", dbgmodule_getaddr },
	{ "name", dbgmodule_getname },
	{ NULL, NULL }
};

int luaopen_dbgmodule(lua_State *L) 
{
	int p = luaL_newmetatable(L, "dbgmodule.metatable");
	
	for (const luaL_Reg *l = dbgmodule_func; l->name != NULL; l++) {
		lua_pushstring(L, l->name);
		lua_pushcfunction(L, l->func);
		lua_settable(L, -3);
	}
	
	lua_newtable(L);
	luaL_setfuncs(L, dbgmodule_lib, 0);
	lua_setglobal(L, "dbgmodule");
	return 1;
}

/************************************************************************/
/*                        dbgtype                                       */
/************************************************************************/

enum SymTagEnum {   
	SymTagNull,  
	SymTagExe,  
	SymTagCompiland,  
	SymTagCompilandDetails,  
	SymTagCompilandEnv,  
	SymTagFunction,  
	SymTagBlock,  
	SymTagData,  
	SymTagAnnotation,  
	SymTagLabel,  
	SymTagPublicSymbol,  
	SymTagUDT,  
	SymTagEnum,  
	SymTagFunctionType,  
	SymTagPointerType,  
	SymTagArrayType,   
	SymTagBaseType,   
	SymTagTypedef,   
	SymTagBaseClass,  
	SymTagFriend,  
	SymTagFunctionArgType,   
	SymTagFuncDebugStart,   
	SymTagFuncDebugEnd,  
	SymTagUsingNamespace,   
	SymTagVTableShape,  
	SymTagVTable,  
	SymTagCustom,  
	SymTagThunk,  
	SymTagCustomType,  
	SymTagManagedType,  
	SymTagDimension,  
	SymTagCallSite,  
	SymTagInlineSite,  
	SymTagBaseInterface,  
	SymTagVectorType,  
	SymTagMatrixType,  
	SymTagHLSLType  
};  

typedef struct _DBG_TYPE {
	CHAR TypeName[1024];
	CHAR ModuleName[1024];
	ULONG64 ModuleAddr;
	ULONG TypeId;
	ULONG64 Address;
} DBG_TYPE;


static int dbgtype_new(lua_State *L)
{
	const char *type_name = luaL_checkstring(L, 1);
	ULONG64 addr = luaL_checkinteger(L, 2);
	ULONG type_id = 0;

	char type_name_str[2048];
	strcpy_s(type_name_str, type_name);

	char *pos = strchr(type_name_str, '!');
	luaL_argcheck(L, pos != NULL, 1, "`module!symbol' expected");
	if (pos == NULL) {
		lua_pushnil(L);
		return 1;
	}

	*pos = 0;
	pos++;

	if (FAILED(g_Ext->m_Symbols->GetTypeId(0, type_name, &type_id))) {
		lua_pushnil(L);
		return 1;
	}

	ULONG index = 0;
	ULONG64 module_addr = 0;
	if (FAILED(g_Ext->m_Symbols->GetModuleByModuleName(type_name_str, 0, &index, &module_addr)) || addr == 0) {
		lua_pushnil(L);
		return 1;
	}

	DBG_TYPE *m = (DBG_TYPE *)lua_newuserdata(L, sizeof(DBG_TYPE));

	luaL_getmetatable(L, "dbgtype.metatable");
	lua_setmetatable(L, -2);

	m->Address = addr;
	m->TypeId = type_id;
	m->ModuleAddr = module_addr;
	strcpy_s(m->TypeName, pos);
	strcpy_s(m->ModuleName, type_name_str);

	return 1;
}

static DBG_TYPE *dbgtype_check (lua_State *L) 
{
	void *ud = luaL_checkudata(L, 1, "dbgtype.metatable");
	luaL_argcheck(L, ud != NULL, 1, "`dbgtype' expected");
	return (DBG_TYPE *)ud;
}

static int dbgtype_getname(lua_State *L)
{
	DBG_TYPE *m = dbgtype_check(L);
	char module_type_name[2048];
	sprintf_s(module_type_name, "%s!%s", m->ModuleName, m->TypeName);
	lua_pushstring(L, module_type_name);
	return 1;
}

static int dbgtype_getaddr(lua_State *L)
{
	DBG_TYPE *m = dbgtype_check(L);
	lua_pushinteger(L, m->Address);
	return 1;
}

static int dbgtype_getsize(lua_State *L)
{
	DBG_TYPE *m = dbgtype_check(L);
	ULONG type_size = 0;
	if (FAILED(g_Ext->m_Symbols->GetTypeSize(m->ModuleAddr, m->TypeId, &type_size))) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, type_size);
	return 1;
}

static int dbgtype_getfieldoffset(lua_State *L)
{
	DBG_TYPE *m = dbgtype_check(L);
	const char *name = luaL_checkstring(L, 2);

	ULONG offset = 0;
	if (FAILED(g_Ext->m_Symbols->GetFieldOffset(m->ModuleAddr, m->TypeId, name, &offset))) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, offset);
	return 1;
}

static int dbgtype_getderef(lua_State *L)
{
	DBG_TYPE *m = dbgtype_check(L);

	ExtRemoteTyped type_obj;
	type_obj.Set(FALSE, m->ModuleAddr, m->TypeId, m->Address);
	char type_buffer[1024];
	ExtRemoteTyped field = *type_obj;
	if (FAILED(g_Ext->m_Symbols3->GetTypeName(m->ModuleAddr, field.m_Typed.TypeId, type_buffer, 1024, NULL))) {
		lua_pushnil(L);
		return 1;
	}

	DBG_TYPE *new_m = (DBG_TYPE *)lua_newuserdata(L, sizeof(DBG_TYPE));

	luaL_getmetatable(L, "dbgtype.metatable");
	lua_setmetatable(L, -2);

	ExtRemoteData data(m->Address, g_Ext->m_PtrSize);
	new_m->Address = data.GetPtr();
	new_m->TypeId = field.m_Typed.TypeId;
	new_m->ModuleAddr = m->ModuleAddr;
	strcpy_s(new_m->TypeName, type_buffer);
	strcpy_s(new_m->ModuleName, m->ModuleName);
	
	return 1;
}

static int dbgtype_getfieldtype(lua_State *L)
{

	DBG_TYPE *m = dbgtype_check(L);
	const char *name = luaL_checkstring(L, 2);

	ULONG type_id = 0;
	if (FAILED(g_Ext->m_Symbols3->GetFieldTypeAndOffset(m->ModuleAddr, m->TypeId, name, &type_id, NULL))) {
		lua_pushnil(L);
		return 1;
	}

	char type_buffer[1024];
	if (FAILED(g_Ext->m_Symbols3->GetTypeName(m->ModuleAddr, type_id, type_buffer, 1024, NULL))) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushstring(L, type_buffer);

	return 1;
}

static const struct luaL_Reg dbgtype_lib [] = {
	{"new", dbgtype_new},
	{NULL, NULL}
};

static int dbgtype___index(lua_State *L)
{
	const char *name = luaL_checkstring(L, -1);

	lua_getmetatable(L, -2);
	lua_pushstring(L, name);
	lua_rawget(L,-2);
	lua_remove(L,-2);
	if (lua_isfunction(L, -1))
	{
		lua_remove(L, -2);
		return 1;
	}
	
	DBG_TYPE *m = dbgtype_check(L);
	ExtRemoteTyped type_obj;
	type_obj.Set(FALSE, m->ModuleAddr, m->TypeId, m->Address);
	if (type_obj.m_Typed.Tag == SymTagArrayType) {
		ExtRemoteTyped array_item = type_obj.ArrayElement(atoi(name));

		char type_buffer[1024];
		if (FAILED(g_Ext->m_Symbols3->GetTypeName(m->ModuleAddr, array_item.m_Typed.TypeId, type_buffer, 1024, NULL))) {
			lua_pushnil(L);
			return 1;
		}

		if (array_item.m_Typed.Tag == SymTagBaseType) {
			lua_pushstring(L, array_item.GetSimpleValue());
			return 1;
		}
		
		DBG_TYPE *new_m = (DBG_TYPE *)lua_newuserdata(L, sizeof(DBG_TYPE));

		luaL_getmetatable(L, "dbgtype.metatable");
		lua_setmetatable(L, -2);

		new_m->Address = array_item.m_Offset;
		new_m->TypeId = array_item.m_Typed.TypeId;
		new_m->ModuleAddr = m->ModuleAddr;
		strcpy_s(new_m->TypeName, type_buffer);
		strcpy_s(new_m->ModuleName, m->ModuleName);

		return 1;
	}

	ULONG type_id = 0;
	if (FAILED(g_Ext->m_Symbols3->GetFieldTypeAndOffset(m->ModuleAddr, m->TypeId, name, &type_id, NULL))) {
		lua_pushnil(L);
		return 1;
	}

	char type_buffer[1024];
	if (FAILED(g_Ext->m_Symbols3->GetTypeName(m->ModuleAddr, type_id, type_buffer, 1024, NULL))) {
		lua_pushnil(L);
		return 1;
	}

	ExtRemoteTyped field = type_obj.Field(name);
	
	if (field.m_Typed.Tag == SymTagPointerType) {
		DBG_TYPE *new_m = (DBG_TYPE *)lua_newuserdata(L, sizeof(DBG_TYPE));

		luaL_getmetatable(L, "dbgtype.metatable");
		lua_setmetatable(L, -2);

		new_m->Address = field.m_Offset;
		new_m->TypeId = field.m_Typed.TypeId;
		new_m->ModuleAddr = m->ModuleAddr;
		strcpy_s(new_m->TypeName, type_buffer);
		strcpy_s(new_m->ModuleName, m->ModuleName);
		
	}
	else if (field.m_Typed.Tag == SymTagBaseType) {
		lua_pushstring(L, field.GetSimpleValue());
	}
	else if (field.m_Typed.Tag == SymTagUDT || field.m_Typed.Tag == SymTagArrayType) {
		DBG_TYPE *new_m = (DBG_TYPE *)lua_newuserdata(L, sizeof(DBG_TYPE));

		luaL_getmetatable(L, "dbgtype.metatable");
		lua_setmetatable(L, -2);

		new_m->Address = field.m_Offset;
		new_m->TypeId = field.m_Typed.TypeId;
		new_m->ModuleAddr = m->ModuleAddr;
		strcpy_s(new_m->TypeName, type_buffer);
		strcpy_s(new_m->ModuleName, m->ModuleName);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

static const struct luaL_Reg dbgtype_func[] = {
	{ "__index", dbgtype___index },
	{ "addr", dbgtype_getaddr },
	{ "name", dbgtype_getname },
	{ "size", dbgtype_getsize },
	{ "deref", dbgtype_getderef },
	{ "fieldoffset", dbgtype_getfieldoffset },
	{ "fieldtype", dbgtype_getfieldtype },
	{ NULL, NULL }
};

int luaopen_dbgtype(lua_State *L) 
{
	int p = luaL_newmetatable(L, "dbgtype.metatable");

	for (const luaL_Reg *l = dbgtype_func; l->name != NULL; l++) {
		lua_pushstring(L, l->name);
		lua_pushcfunction(L, l->func);
		lua_settable(L, -3);
	}

	lua_newtable(L);
	luaL_setfuncs(L, dbgtype_lib, 0);
	lua_setglobal(L, "dbgtype");
	return 1;
}


/************************************************************************/
/*                          dbgreg                                      */
/************************************************************************/

typedef struct _DBG_REG {
	ULONG RegIndex;
	CHAR RegName[1024];
} DBG_REG;

static int dbgreg_new(lua_State *L)
{
	const char *reg_name = luaL_checkstring(L, 1);
	ULONG index = 0;
	if (FAILED(g_Ext->m_Registers->GetIndexByName(reg_name, &index))) {
		lua_pushnil(L);
		return 1;
	}

	DBG_REG *m = (DBG_REG *)lua_newuserdata(L, sizeof(DBG_REG));

	luaL_getmetatable(L, "dbgreg.metatable");
	lua_setmetatable(L, -2);

	m->RegIndex = index;
	strcpy_s(m->RegName, reg_name);

	return 1;
}

static DBG_REG *dbgreg_check(lua_State *L)
{
	void *ud = luaL_checkudata(L, 1, "dbgreg.metatable");
	luaL_argcheck(L, ud != NULL, 1, "`dbgreg' expected");
	return (DBG_REG *)ud;
}

static int dbgreg_getname(lua_State *L)
{
	DBG_REG *m = dbgreg_check(L);
	lua_pushstring(L, m->RegName);
	return 1;
}

static int dbgreg_getindex(lua_State *L)
{
	DBG_REG *m = dbgreg_check(L);
	lua_pushinteger(L, m->RegIndex);
	return 1;
}

static const struct luaL_Reg dbgreg_lib[] = {
	{ "new", dbgreg_new },
	{ NULL, NULL }
};

static int dbgreg___index(lua_State *L)
{
	const char *member_name = luaL_checkstring(L, -1);

	lua_getmetatable(L, -2);
	lua_pushstring(L, member_name);
	lua_rawget(L, -2);
	lua_remove(L, -2);
	if (lua_isfunction(L, -1))
	{
		lua_remove(L, -2);
		return 1;
	}
	
	lua_pushnil(L);
	return 1;
}

static int dbgreg_get(lua_State *L)
{
	DBG_REG *m = dbgreg_check(L);
	DEBUG_VALUE value = {0};
	if (FAILED(g_Ext->m_Registers->GetValue(m->RegIndex, &value))) {
		lua_pushnil(L);
		return 1;
	}

	if (value.Type == DEBUG_VALUE_INT8) {
		lua_pushinteger(L, value.I8);
	}
	else if (value.Type == DEBUG_VALUE_INT16) {
		lua_pushinteger(L, value.I16);
	}
	else if (value.Type == DEBUG_VALUE_INT32) {
		lua_pushinteger(L, value.I32);
	}
	else if (value.Type == DEBUG_VALUE_INT64) {
		lua_pushinteger(L, value.I64);
	}
	else if (value.Type == DEBUG_VALUE_FLOAT32) {
		lua_pushnumber(L, value.F32);
	}
	else if (value.Type == DEBUG_VALUE_FLOAT64) {
		lua_pushnumber(L, value.F64);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

static int dbgreg_set(lua_State *L)
{
	DBG_REG *m = dbgreg_check(L);
	DEBUG_REGISTER_DESCRIPTION des= { 0 };
	if (FAILED(g_Ext->m_Registers->GetDescription(m->RegIndex, NULL, 0, NULL, &des))) {
		return 0;
	}

	DEBUG_VALUE value = { 0 };
	value.Type = des.Type;
	if (value.Type == DEBUG_VALUE_INT8) {
		ULONG64 reg_value = luaL_checkinteger(L, 2);
		value.I8 = (UCHAR)reg_value;
		g_Ext->m_Registers->SetValue(m->RegIndex, &value);
	}
	else if (value.Type == DEBUG_VALUE_INT16) {
		ULONG64 reg_value = luaL_checkinteger(L, 2);
		value.I16 = (USHORT)reg_value;
		g_Ext->m_Registers->SetValue(m->RegIndex, &value);
	}
	else if (value.Type == DEBUG_VALUE_INT32) {
		ULONG64 reg_value = luaL_checkinteger(L, 2);
		value.I32 = (ULONG)reg_value;
		g_Ext->m_Registers->SetValue(m->RegIndex, &value);
	}
	else if (value.Type == DEBUG_VALUE_INT64) {
		ULONG64 reg_value = luaL_checkinteger(L, 2);
		value.I64 = reg_value;
		g_Ext->m_Registers->SetValue(m->RegIndex, &value);
	}
	else if (value.Type == DEBUG_VALUE_FLOAT32) {
		double reg_value = luaL_checknumber(L, 2);
		value.F32 = (float)reg_value;
		g_Ext->m_Registers->SetValue(m->RegIndex, &value);
	}
	else if (value.Type == DEBUG_VALUE_FLOAT64) {
		double reg_value = luaL_checknumber(L, 2);
		value.F64 = reg_value;
		g_Ext->m_Registers->SetValue(m->RegIndex, &value);
	}

	return 0;
}

static const struct luaL_Reg dbgreg_func[] = {
	{ "__index", dbgreg___index },
	{ "get", dbgreg_get },
	{ "set", dbgreg_set },
	{ "index", dbgreg_getindex },
	{ "name", dbgreg_getname },
	{ NULL, NULL }
};

int luaopen_dbgreg(lua_State *L)
{
	int p = luaL_newmetatable(L, "dbgreg.metatable");

	for (const luaL_Reg *l = dbgreg_func; l->name != NULL; l++) {
		lua_pushstring(L, l->name);
		lua_pushcfunction(L, l->func);
		lua_settable(L, -3);
	}

	lua_newtable(L);
	luaL_setfuncs(L, dbgreg_lib, 0);
	lua_setglobal(L, "dbgreg");
	return 1;
}


/************************************************************************/
/*                        dbgmem                                        */
/************************************************************************/


typedef struct _DBG_MEM {
	ULONG64 Offset;
	ULONG Size;
	ULONG CachedSize;
	UCHAR Buffer[1];
} DBG_MEM;

static int dbgmem_new(lua_State *L)
{
	ULONG64 offset = luaL_checkinteger(L, 1);
	ULONG mem_size = (ULONG)luaL_checkinteger(L, 2);
	
	UCHAR *buffer = (UCHAR *)malloc(mem_size);
	if (buffer == NULL) {
		lua_pushnil(L);
		return 1;
	}

	ULONG cached_size = 0;
	if (FAILED(g_Ext->m_Data->ReadVirtual(offset, buffer, mem_size, &cached_size))) {
		free(buffer);
		lua_pushnil(L);
		return 1;
	}

	DBG_MEM *m = (DBG_MEM *)lua_newuserdata(L, sizeof(DBG_MEM) + cached_size - 1);

	luaL_getmetatable(L, "dbgmem.metatable");
	lua_setmetatable(L, -2);

	m->Offset = offset;
	m->Size = mem_size;
	m->CachedSize = cached_size;
	CopyMemory(m->Buffer, buffer, cached_size);

	free(buffer);

	return 1;
}

static DBG_MEM *dbgmem_check(lua_State *L) 
{
	void *ud = luaL_checkudata(L, 1, "dbgmem.metatable");
	luaL_argcheck(L, ud != NULL, 1, "`dbgmem' expected");
	return (DBG_MEM *)ud;
}

static int dbgmem_getoffset(lua_State *L)
{
	DBG_MEM *m = dbgmem_check(L);
	lua_pushinteger(L, m->Offset);
	return 1;
}

static int dbgmem_getsize(lua_State *L)
{
	DBG_MEM *m = dbgmem_check(L);
	lua_pushinteger(L, m->Size);
	return 1;
}

static int dbgmem_getcachedsize(lua_State *L)
{
	DBG_MEM *m = dbgmem_check(L);
	lua_pushinteger(L, m->Size);
	return 1;
}

static int dbgmem_getstring(lua_State *L)
{
	DBG_MEM *m = dbgmem_check(L);
	lua_pushlstring(L, (char *)m->Buffer, m->CachedSize);
	return 1;
}

static int dbgmem_tohexstring(lua_State *L)
{
	DBG_MEM *m = dbgmem_check(L);
	
	int hex_str_len = AtlHexEncodeGetRequiredLength(m->CachedSize);
	CStringA hex_str;
	
	int encode_length = hex_str_len;
	AtlHexEncode(m->Buffer, m->CachedSize, hex_str.GetBufferSetLength(hex_str_len), &encode_length);
	hex_str.ReleaseBuffer(encode_length);

	lua_pushstring(L, hex_str.GetString());

	return 1;
}

static int dbgmem_fromhexstring(lua_State *L)
{
	DBG_MEM *m = dbgmem_check(L);
	const char* hex_str = luaL_checkstring(L, 2);

	int hex_buffer_len = AtlHexDecodeGetRequiredLength(strlen(hex_str));
	if ((ULONG)hex_buffer_len > m->CachedSize) {
		return 0;
	}

	int decode_length = hex_buffer_len;
	AtlHexDecode(hex_str, strlen(hex_str), m->Buffer, &decode_length);
	g_Ext->m_Data->WriteVirtual(m->Offset, (PVOID)m->Buffer, m->CachedSize, NULL);

	return 0;
}

static int dbgmem_setstring(lua_State *L)
{
	DBG_MEM *m = dbgmem_check(L);
	const char* buffer = luaL_checkstring(L, 2);
	ULONG mem_size = (ULONG)lua_rawlen(L, 2);

	if (mem_size > m->CachedSize) {
		return 0;
	}

	ULONG cached_size = 0;
	g_Ext->m_Data->WriteVirtual(m->Offset, (PVOID)buffer, mem_size, &cached_size);
	CopyMemory(m->Buffer, buffer, cached_size);
	return 0;
}

static const struct luaL_Reg dbgmem_lib [] = {
	{"new", dbgmem_new},
	{NULL, NULL}
};

static int dbgmem___index(lua_State *L)
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
	DBG_MEM *m = dbgmem_check(L);
	
	ULONG index = atoi(member_name);
	if (index >= m->CachedSize) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, m->Buffer[index]);
	return 1;
}

static int dbgmem___newindex(lua_State *L)
{
	const char *member_name = luaL_checkstring(L, -2);
	UCHAR data = (UCHAR)luaL_checkinteger(L, -1);
	DBG_MEM *m = dbgmem_check(L);

	ULONG index = atoi(member_name);
	if (index >= m->CachedSize) {
		return 0;
	}
	m->Buffer[index] = data;
	g_Ext->m_Data->WriteVirtual(m->Offset, m->Buffer, m->CachedSize, NULL);
	return 0;
}

static int dbgmem_update(lua_State *L)
{
	DBG_MEM *m = dbgmem_check(L);
	g_Ext->m_Data->ReadVirtual(m->Offset, m->Buffer, m->CachedSize, NULL);
	return 0;
} 

static const struct luaL_Reg dbgmem_func[] = {
	{ "__index", dbgmem___index },
	{ "__newindex", dbgmem___newindex },
	{ "offset", dbgmem_getoffset },
	{ "size", dbgmem_getsize },
	{ "cachedsize", dbgmem_getcachedsize },
	{ "getstring", dbgmem_getstring },
	{ "setstring", dbgmem_setstring },
	{ "update", dbgmem_update },
	{ "tohexstring", dbgmem_tohexstring },
	{ "fromhexstring", dbgmem_fromhexstring },
	{ NULL, NULL }
};

int luaopen_dbgmem(lua_State *L) 
{
	int p = luaL_newmetatable(L, "dbgmem.metatable");

	for (const luaL_Reg *l = dbgmem_func; l->name != NULL; l++) {
		lua_pushstring(L, l->name);
		lua_pushcfunction(L, l->func);
		lua_settable(L, -3);
	}

	lua_newtable(L);
	luaL_setfuncs(L, dbgmem_lib, 0);
	lua_setglobal(L, "dbgmem");
	return 1;
}


/************************************************************************/
/*                        dbg                                           */
/************************************************************************/

static int exec(lua_State* L)
{
	const char* cmd = luaL_checkstring(L, 1);
	g_Ext->m_Control->Execute(DEBUG_OUTCTL_ALL_CLIENTS |
		DEBUG_OUTCTL_OVERRIDE_MASK |
		DEBUG_OUTCTL_NOT_LOGGED,
		cmd,
		DEBUG_EXECUTE_DEFAULT);

	return 0;
}

static int readbyte(lua_State* L)
{
	BOOL ret = FALSE;
	ULONG data = 0;
	ULONG return_length = 0;
	ULONG64 offset = luaL_checkinteger(L, 1);
	HRESULT hr = g_Ext->m_Data->ReadVirtual((ULONG64)offset, &data, 1, &return_length);
	if (SUCCEEDED(hr)) {
		ret = TRUE;
	}

	if (ret) {
		lua_pushinteger(L, data);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

static int readword(lua_State* L)
{
	BOOL ret = FALSE;
	ULONG data = 0;
	ULONG return_length = 0;
	ULONG64 offset = luaL_checkinteger(L, 1);
	HRESULT hr = g_Ext->m_Data->ReadVirtual((ULONG64)offset, &data, 2, &return_length);
	if (SUCCEEDED(hr)) {
		ret = TRUE;
	}

	if (ret) {
		lua_pushinteger(L, data);
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}

static int readdword(lua_State* L)
{
	BOOL ret = FALSE;
	ULONG data = 0;
	ULONG return_length = 0;
	ULONG64 offset = luaL_checkinteger(L, 1);
	HRESULT hr = g_Ext->m_Data->ReadVirtual((ULONG64)offset, &data, 4, &return_length);
	if (SUCCEEDED(hr)) {
		ret = TRUE;
	}

	if (ret) {
		lua_pushinteger(L, data);
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}

static int readqword(lua_State* L)
{
	BOOL ret = FALSE;
	ULONG64 data = 0;
	ULONG return_length = 0;
	ULONG64 offset = luaL_checkinteger(L, 1);
	HRESULT hr = g_Ext->m_Data->ReadVirtual((ULONG64)offset, &data, 8, &return_length);
	if (SUCCEEDED(hr)) {
		ret = TRUE;
	}

	if (ret) {
		lua_pushinteger(L, data);
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}

static int writebyte(lua_State* L)
{
	ULONG return_length = 0;
	ULONG64 offset = luaL_checkinteger(L, 1);
	ULONG64 data = luaL_checkinteger(L, 2);
	g_Ext->m_Data->WriteVirtual((ULONG64)offset, &data, 1, &return_length);
	return 0;
}

static int writeword(lua_State* L)
{
	ULONG return_length = 0;
	ULONG64 offset = luaL_checkinteger(L, 1);
	ULONG64 data = luaL_checkinteger(L, 2);
	g_Ext->m_Data->WriteVirtual((ULONG64)offset, &data, 2, &return_length);
	return 0;
}

static int writedword(lua_State* L)
{
	ULONG return_length = 0;
	ULONG64 offset = luaL_checkinteger(L, 1);
	ULONG64 data = luaL_checkinteger(L, 2);
	g_Ext->m_Data->WriteVirtual((ULONG64)offset, &data, 4, &return_length);
	return 0;
}

static int writeqword(lua_State* L)
{
	ULONG return_length = 0;
	ULONG64 offset = luaL_checkinteger(L, 1);
	ULONG64 data = luaL_checkinteger(L, 2);
	g_Ext->m_Data->WriteVirtual((ULONG64)offset, &data, 8, &return_length);
	return 0;
}

static int readunicode(lua_State* L)
{
	CStringW unicode_string;
	WCHAR temp_char;
	ULONG return_length;
	ULONG64 offset = luaL_checkinteger(L, 1);
	for (INT i = 0; i < 0x1000; i++) {
		HRESULT hr = g_Ext->m_Data->ReadVirtual((ULONG64)(offset + i * sizeof(WCHAR)),
			&temp_char, sizeof(WCHAR), &return_length);
		if (temp_char == L'\0' || FAILED(hr)) {
			break;
		}
		unicode_string += temp_char;
	}

	lua_pushstring(L, CW2A(unicode_string));
	return 1;
}

static int readascii(lua_State* L)
{
	CStringA ascii_string;
	CHAR temp_char;
	ULONG return_length;
	ULONG64 offset = luaL_checkinteger(L, 1);
	for (INT i = 0; i < 1024; i++) {
		HRESULT hr = g_Ext->m_Data->ReadVirtual((ULONG64)(offset + i * sizeof(CHAR)),
			&temp_char, sizeof(CHAR), &return_length);
		if (temp_char == '\0' || FAILED(hr)) {
			break;
		}
		ascii_string += temp_char;
	}

	lua_pushstring(L, ascii_string);
	return 1;
}

static int wait(lua_State* L)
{
	g_Ext->m_Control->WaitForEvent(0, INFINITE);
	return 0;
}

static int evalmasm(lua_State* L)
{
	BOOL ret = FALSE;
	ULONG64 eval_ret = 0;
	const char* string = luaL_checkstring(L, 1);
	DEBUG_VALUE debug_value = { 0 };
	ULONG old_type;
	ULONG return_index;
	g_Ext->m_Control3->GetExpressionSyntax(&old_type);
	g_Ext->m_Control3->SetExpressionSyntax(DEBUG_EXPR_MASM);

	HRESULT hr = g_Ext->m_Control3->Evaluate(string, DEBUG_VALUE_INT32, &debug_value, &return_index);
	if (SUCCEEDED(hr)) {
		if (debug_value.Type == DEBUG_VALUE_INT8) {
			eval_ret = debug_value.I8;
			ret = TRUE;
		}
		else if (debug_value.Type == DEBUG_VALUE_INT16) {
			eval_ret = debug_value.I16;
			ret = TRUE;
		}
		else if (debug_value.Type == DEBUG_VALUE_INT32) {
			eval_ret = debug_value.I32;
			ret = TRUE;
		}
		else if (debug_value.Type == DEBUG_VALUE_INT64) {
			eval_ret = debug_value.I64;
			ret = TRUE;
		}
		else {
			g_Ext->Err("Lua dose not support this value.\n");
		}
	}
	else {
		g_Ext->Err("Cannot evaluate value.\n");
	}

	g_Ext->m_Control3->SetExpressionSyntax(old_type);

	if (ret) {
		lua_pushinteger(L, eval_ret);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

static int evalcpp(lua_State* L)
{
	BOOL ret = FALSE;
	ULONG64 eval_ret = 0;
	const char* string = luaL_checkstring(L, 1);
	DEBUG_VALUE debug_value = { 0 };
	ULONG old_type;
	ULONG return_index;
	g_Ext->m_Control3->GetExpressionSyntax(&old_type);
	g_Ext->m_Control3->SetExpressionSyntax(DEBUG_EXPR_CPLUSPLUS);

	HRESULT hr = g_Ext->m_Control3->Evaluate(string, DEBUG_VALUE_INT32, &debug_value, &return_index);
	if (SUCCEEDED(hr)) {
		if (debug_value.Type == DEBUG_VALUE_INT8) {
			eval_ret = debug_value.I8;
			ret = TRUE;
		}
		else if (debug_value.Type == DEBUG_VALUE_INT16) {
			eval_ret = debug_value.I16;
			ret = TRUE;
		}
		else if (debug_value.Type == DEBUG_VALUE_INT32) {
			eval_ret = debug_value.I32;
			ret = TRUE;
		}
		else if (debug_value.Type == DEBUG_VALUE_INT64) {
			eval_ret = debug_value.I64;
			ret = TRUE;
		}
		else {
			g_Ext->Err("Lua dose not support this value.\n");
		}
	}
	else {
		g_Ext->Err("Cannot evaluate value.\n");
	}

	g_Ext->m_Control3->SetExpressionSyntax(old_type);
	if (ret) {
		lua_pushinteger(L, eval_ret);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

static UCHAR CharToDigital(CHAR c)
{
	UCHAR ret;
	if (c >= '0' && c <= '9') {
		ret = c - '0';
	}
	else if (c >= 'a' && c <= 'f') {
		ret = c - 'a' + 10;
	}
	else if (c >= 'A' && c <= 'F') {
		ret = c - 'A' + 10;
	}
	
	return ret;
}

static int StringToBytes(LPCSTR hex_str, CAutoVectorPtr<UCHAR> &hex_buffer)
{
	CStringA pure_hex_str;
	int hex_str_length = strlen(hex_str);
	CHAR current_char;
	int i;
	for (i = 0; i < hex_str_length; i++) {
		current_char = hex_str[i];
		if ((current_char >= '0' && current_char <= '9') ||
			(current_char >= 'a' && current_char <= 'f') ||
			(current_char >= 'A' && current_char <= 'F')) {
			pure_hex_str += current_char;
		}
		else if (current_char == ' ' ||
			current_char == '\t' ||
			current_char == '\r' ||
			current_char == '\n') {
			continue;
		}
		else {
			return 0;
		}
	}

	if (pure_hex_str.GetLength() % 2 != 0) {
		return 0;
	}

	int buffer_length = pure_hex_str.GetLength() / 2;
	if (!hex_buffer.Allocate(buffer_length)) {
		return 0;
	}

	for (i = 0; i < pure_hex_str.GetLength(); i += 2) {
		hex_buffer[i / 2] = CharToDigital(pure_hex_str[i]);
		hex_buffer[i / 2] <<= 4;
		hex_buffer[i / 2] |= CharToDigital(pure_hex_str[i + 1]);
	}

	return buffer_length;
}

static int search(lua_State* L)
{
	BOOL ret = FALSE;
	ULONG64 match_offset = 0;
	ULONG64 search_offset = luaL_checkinteger(L, 1);
	ULONG64 search_length = luaL_checkinteger(L, 2);
	const char* string = luaL_checkstring(L, 3);

	CAutoVectorPtr<UCHAR> hex_buffer;
	int hex_buffer_length = StringToBytes(string, hex_buffer);
	if (hex_buffer_length != 0) {
		HRESULT hr = g_Ext->m_Data->SearchVirtual((ULONG64)search_offset,
			(ULONG64)search_length, hex_buffer.m_p, hex_buffer_length, 1, &match_offset);
		if (hr == S_OK) {
			ret = TRUE;
		}
	}
	else {
		g_Ext->Err("Search bytes error.\n");
	}

	if (ret) {
		lua_pushinteger(L, (int)match_offset);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

static int get_symbolnamebyoffset(lua_State* L)
{
	ULONG64 offset = luaL_checkinteger(L, 1);
	char name[1024] = {0};
	ULONG name_length = sizeof(name);
	ULONG64 disp = 0;
	if (FAILED(g_Ext->m_Symbols->GetNameByOffset(offset, name, sizeof(name), &name_length, &disp))) {
		lua_pushnil(L);
		return 1;
	}

	CStringA sym_str;
	sym_str.Format("%s+0x%I64x", name, disp);
	lua_pushstring(L, sym_str.GetString());
	return 1;
}

static int get_symboloffsetbyname(lua_State* L)
{
	const char *name = luaL_checkstring(L, 1);
	ULONG64 offset = 0;
	if (FAILED(g_Ext->m_Symbols->GetOffsetByName(name, &offset))) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, offset);
	return 1;
}

static const struct luaL_Reg dbgbasic_func[] =
{
	{ "exec", exec },
	{ "readbyte", readbyte },
	{ "readword", readword },
	{ "readdword", readdword },
	{ "readqword", readqword },
	{ "writebyte", writebyte },
	{ "writeword", writeword },
	{ "writedword", writedword },
	{ "writeqword", writeqword },
	{ "readunicode", readunicode },
	{ "readascii", readascii },
	{ "wait", wait },
	{ "evalmasm", evalmasm },
	{ "evalcpp", evalcpp },
	{ "search", search },
	{ "get_symbolnamebyoffset", get_symbolnamebyoffset },
	{ "get_symboloffsetbyname", get_symboloffsetbyname },
	{ NULL, NULL }
};


EXT_COMMAND(lua,
	"Execute lua file.",
	"{;x,r;path;Execute lua file path.}")
{
	LPCSTR path = GetUnnamedArgStr(0);
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	SetDefaultPrint(L);
	luaopen_dbgmodule(L);
	luaopen_dbgtype(L);
	luaopen_dbgreg(L);
	luaopen_dbgmem(L);

	lua_pushglobaltable(L);
	luaL_setfuncs(L, dbgbasic_func, 0);
	lua_pop(L, 1);

	if (luaL_dofile(L, path) != 0) {
		Err("lua error: %s\r\n", lua_tostring(L, -1));
	}
	
	lua_close(L);
}

EXT_COMMAND(luacmd,
	"Input lua command.",
	"")
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	SetDefaultPrint(L);
	luaopen_dbgmodule(L);
	luaopen_dbgtype(L);
	luaopen_dbgreg(L);
	luaopen_dbgmem(L);

	lua_pushglobaltable(L);
	luaL_setfuncs(L, dbgbasic_func, 0);
	lua_pop(L, 1);

	CHAR buffer[0x1000] = {0};
	ULONG input_length = 0;
	while (SUCCEEDED(m_Control4->Input(buffer, 0x1000, &input_length))) {
		CStringA backspace;
		for (ULONG i = 0; i < input_length; i++) {
			backspace.Append("\b");
		}
		Out(backspace.GetString());
		
		if (strcmp(buffer, "quit()") == 0) {
			buffer[0] = 0;
			input_length = 0;
			break;
		}
		luaL_dostring(L, buffer);
		buffer[0] = 0;
		input_length = 0;
	}

	lua_close(L);
}
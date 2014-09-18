#include <lua.h>
#include <lauxlib.h>
#include "lum.h"

static void mark_object(lua_State *L, lua_State *dL, const void * parent, const char * desc);

//#if LUA_VERSION_NUM == 501

static void
luaL_checkversion(lua_State *L) {
	if (lua_pushthread(L) == 0) {
		luaL_error(L, "Must require in main thread");
	}
	lua_setfield(L, LUA_REGISTRYINDEX, "mainthread");
}

static void
lua_rawsetp(lua_State *L, int idx, const void *p) {
	if (idx < 0) {
		idx += lua_gettop(L) + 1;
	}
	lua_pushlightuserdata(L, (void *)p);
	lua_insert(L, -2);
	lua_rawset(L, idx);
}

static void
lua_rawgetp(lua_State *L, int idx, const void *p) {
	if (idx < 0) {
		idx += lua_gettop(L) + 1;
	}
	lua_pushlightuserdata(L, (void *)p);
	lua_rawget(L, idx);
}

static void
lua_getuservalue(lua_State *L, int idx) {
	lua_getfenv(L, idx);
}

static void
mark_function_env(lua_State *L, lua_State *dL, const void * t) {
	lua_getfenv(L,-1);
	if (lua_istable(L,-1)) {
		mark_object(L, dL, t, "[environment]");
	} else {
		lua_pop(L,1);
	}
}

//#else

//#define mark_function_env(L,dL,t)

//#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TABLE 1
#define FUNCTION 2
#define SOURCE 3
#define THREAD 4
#define USERDATA 5
#define MARK 6

static bool
ismarked(lua_State *L, const void *p) {
	lua_rawgetp(L, MARK, p);
	if (lua_isnil(L,-1)) {
		lua_pop(L,1);
		lua_pushboolean(L,1);
		lua_rawsetp(L, MARK, p);
		return false;
	}
	lua_pop(L,1);
	return true;
}

static const void *
readobject(lua_State *L, lua_State *dL, const void *parent, const char *desc) {
	lua_State * nL = dL ? dL : L;
	int t = lua_type(L, -1);
	int tidx = 0;
	switch (t) {
	case LUA_TTABLE:
		tidx = TABLE;
		break;
	case LUA_TFUNCTION:
		tidx = FUNCTION;
		break;
	case LUA_TTHREAD:
		tidx = THREAD;
		break;
	case LUA_TUSERDATA:
		tidx = USERDATA;
		break;
	default:
		return NULL;
	}

	const void * p = lua_topointer(L, -1);
	if (ismarked(nL, p)) {
		lua_rawgetp(nL, tidx, p);
		if (!lua_isnil(nL,-1)) {
			lua_pushstring(nL,desc);
			lua_rawsetp(nL, -2, parent);
		}
		lua_pop(nL, 1);
		lua_pop(L, 1);
		return NULL;
	}

	lua_newtable(nL);
	lua_pushstring(nL,desc); 
	lua_rawsetp(nL, -2, parent);
	lua_rawsetp(nL, tidx, p);

	return p;
}

static const char *
keystring(lua_State *L, int index, char * buffer) {
	int t = lua_type(L,index);
	switch (t) {
	case LUA_TSTRING:
		return lua_tostring(L,index);
	case LUA_TNUMBER:
		sprintf(buffer,"[%lg]",lua_tonumber(L,index));
		break;
	case LUA_TBOOLEAN:
		sprintf(buffer,"[%s]",lua_toboolean(L,index) ? "true" : "false");
		break;
	case LUA_TNIL:
		sprintf(buffer,"[nil]");
		break;
	default:
		sprintf(buffer,"[%s:%p]",lua_typename(L,t),lua_topointer(L,index));
		break;
	}
	return buffer;
}

static void
mark_table(lua_State *L, lua_State *dL, const void * parent, const char * desc) {
	
	const void * t = readobject(L, dL, parent, desc);
	if (t == NULL)
		return;
	
	bool weakk = false;
	bool weakv = false;
	if (lua_getmetatable(L, -1)) {
		lua_pushliteral(L, "__mode");
		lua_rawget(L, -2);
		if (lua_isstring(L,-1)) {
			const char *mode = lua_tostring(L, -1);
			if (strchr(mode, 'k')) {
				weakk = true;
			}
			if (strchr(mode, 'v')) {
				weakv = true;
			}
		}
		lua_pop(L,1);
		mark_table(L, dL, t, "[metatable]");
	}
	

	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		if (weakv) {
			lua_pop(L,1);
		} else {
			char temp[256];
			const char * desc = keystring(L, -2, temp);
			mark_object(L, dL, t , desc);
		}
		if (!weakk) {
			lua_pushvalue(L,-1);
			mark_object(L, dL, t , "[key]");
		}
	}

	lua_pop(L,1);
}

static void
mark_userdata(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	if (t == NULL)
		return;
	if (lua_getmetatable(L, -1)) {
		mark_table(L, dL, t, "[metatable]");
	}

	lua_getuservalue(L,-1);
	if (lua_isnil(L,-1)) {
		lua_pop(L,2);
	} else {
		mark_table(L, dL, t, "[uservalue]");
		lua_pop(L,1);
	}
}

static void
mark_function(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	if (t == NULL)
		return;

	mark_function_env(L,dL,t);
	
	int i;
	for (i=1;;i++) {
		const char *name = lua_getupvalue(L,-1,i);
		if (name == NULL)
			break;
		//lua_pop(L, 1);
		mark_object(L, dL, t, name[0] ? name : "[upvalue]");
	}


	if (lua_iscfunction(L,-1)) {
		if (i==1) {
			// light c function
			lua_pushnil(L);
			lua_rawsetp(L, FUNCTION, t);
		}
		lua_pop(L,1);
	} else {
		lua_Debug ar;
		lua_getinfo(L, ">S", &ar);   // pop function
		static char b[1024];
		sprintf(b, "%s:%d", ar.short_src, ar.linedefined);
		lua_pushstring(L, b);
		lua_rawsetp(L, SOURCE, t);
	}
}

static void
mark_thread(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	if (t == NULL)
		return;
	int level = 0;
	lua_State *cL = lua_tothread(L,-1);
	if (cL == L) {
		level = 1;
	}
	lua_Debug ar;
	static char b[1024*1024*4];
	static int boffset = 0;
	boffset = 0;
	while (lua_getstack(cL, level, &ar)) {
		char tmp[128];
		lua_getinfo(cL, "Sl", &ar);
		boffset += sprintf(b+boffset, "%s", ar.short_src);
		if (ar.currentline >=0) {
			boffset += sprintf(b+boffset, ":%d", ar.currentline);
		}

		int i,j;
		for (j=1;j>-1;j-=2) {
			for (i=j;;i+=j) {
				const char * name = lua_getlocal(cL, &ar, i);
				if (name == NULL)
					break;

				snprintf(tmp, sizeof(tmp), "%s : %s:%d",name,ar.short_src,ar.currentline);
				mark_object(cL, L, t, tmp);
			}
		}

		++level;
	}
	lua_pushstring(L, b);
	lua_rawsetp(L, SOURCE, t);
	lua_pop(L, 1);
}

static void 
mark_object(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	int t = lua_type(L, -1);
	switch (t) {
	case LUA_TTABLE:
		mark_table(L, dL, parent, desc);
		break;
	case LUA_TUSERDATA:
		mark_userdata(L, dL, parent, desc);
		break;
	case LUA_TFUNCTION:
		mark_function(L, dL, parent, desc);
		//lua_pop(L, 1);
		break;
	case LUA_TTHREAD:
		mark_thread(L, dL, parent, desc);
		break;
	default:
		lua_pop(L,1);
		break;
	}
}

static int
count_table(lua_State *L, int idx) {
	int n = 0;
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		++n;
		lua_pop(L,1);
	}
	return n;
}

static void
gen_table_desc(lua_State *dL, char *b, const void * parent, const char *desc, int *boffset) {
	*boffset += sprintf(b+(*boffset), "%p : %s\n", parent, desc);
}

static void
pdesc(lua_State *L, lua_State *dL, int idx, const char * typename) {
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		static char b[1024*1024*4];
		static int boffset = 0;
		boffset = 0;
		const void * key = lua_touserdata(L, -2);
		if (idx == FUNCTION) {
			lua_rawgetp(L, SOURCE, key);
			if (lua_isnil(L, -1)) {
				boffset += sprintf(b+boffset, "cfunction\n");
			} else {
				size_t l = 0;
				const char * s = lua_tolstring(L, -1, &l);
				boffset += sprintf(b+boffset, "%s\n", s);
			}
			lua_pop(L, 1);
		} else if (idx == THREAD) {
			lua_rawgetp(L, SOURCE, key);
			size_t l = 0;
			const char * s = lua_tolstring(L, -1, &l);
			boffset += sprintf(b+boffset, "%s\n", s);
			lua_pop(L, 1);
		} else {
			boffset += sprintf(b+boffset, "%s\n", typename);
		}

		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			const void * parent = lua_touserdata(L,-2);
			const char * desc = luaL_checkstring(L,-1);
			gen_table_desc(L, b, parent, desc, &boffset);
			lua_pop(L,1);
		}

		lua_pushstring(L, b);
		lua_rawsetp(L, -4, key);
		lua_pop(L,1);
	}
}

static void
gen_result(lua_State *L, lua_State *dL) {
	int count = 0;
	count += count_table(L, TABLE);
	count += count_table(L, FUNCTION);
	count += count_table(L, USERDATA);
	count += count_table(L, THREAD);
	lua_createtable(L, 0, count);
	pdesc(L, dL, TABLE, "table");
	pdesc(L, dL, USERDATA, "userdata");
	pdesc(L, dL, FUNCTION, "function");
	pdesc(L, dL, THREAD, "thread");
}

static int
snapshot(lua_State *L) {
	
	int i;
	for (i=0;i<MARK;i++) {
		lua_newtable(L);
		lua_insert(L, 1);
	}
	
	lua_pushvalue(L, LUA_REGISTRYINDEX);
	mark_table(L, NULL, NULL, "[registry]");
	gen_result(L, NULL);
	
	return 1;
}

int
luaopen_snapshot(lua_State *L) {
	luaL_checkversion(L);
	lua_pushcfunction(L, snapshot);
	return 1;
}

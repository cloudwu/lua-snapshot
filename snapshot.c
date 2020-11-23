#include <stdlib.h>
#include <assert.h>
#include <lua.h>
#include <lauxlib.h>

/*
lua.h
#define LUA_TTABLE		5
#define LUA_TFUNCTION		6
#define LUA_TUSERDATA		7
#define LUA_TTHREAD		8
*/

#define MAX_NAME_SIZE 128

#define REF_TYPE_TABLE_VALUE 1 		/* REF_TYPE_GLOBAL is _G[key] refrence */
#define REF_TYPE_TABLE_KEY 2
#define REF_TYPE_LOCAL 3
#define REF_TYPE_UPVALUE 4
#define REF_TYPE_META 5				/* metatable/uservalue/registry/environment/args[n] */

static void mark_object(lua_State *L, lua_State *dL, const void * refobj, int reftype,const char * refname,int depth);

#if LUA_VERSION_NUM == 501
#define LUA_OK 0

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
mark_function_env(lua_State *L, lua_State *dL, const void * t,int depth) {
	lua_getfenv(L,-1);
	if (lua_istable(L,-1)) {
		mark_object(L, dL, t, REF_TYPE_META,"[environment]",depth);
	} else {
		lua_pop(L,1);
	}
}

#else

#define mark_function_env(L,dL,t,depth)

#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define OBJECT 1
#define MARK 2

typedef struct ref_t {
	void *obj;
	int type;
	char name[MAX_NAME_SIZE];
	struct ref_t *next;
} ref_t;

typedef struct object_t {
	void *id;
	int type;
	int depth;
	char source[MAX_NAME_SIZE];
	ref_t *refindex;		/* shortest path ref */
	int refcount;
	ref_t reflist;
	int tablecount;
} object_t;

static object_t*
get_object(lua_State *dL,const void *id) {
	lua_rawgetp(dL,OBJECT,id);
	if (lua_isnil(dL,-1)) {
		lua_pop(dL,1);
		return NULL;
	}
	object_t *obj = (object_t*)lua_touserdata(dL,-1);
	lua_pop(dL,1);
	return obj;
}

static object_t*
add_object(lua_State *dL,const void *id,int type,int depth,const char *source) {
	object_t *object = (object_t*)lua_newuserdata(dL,sizeof(*object));
	object->id = (void*)id;
	object->type = type;
	object->depth = depth;
	object->source[0] = '\0';
	if (source != NULL) {
		strcpy(object->source,source);
	}
	object->refindex = NULL;
	object->refcount = 0;
	object->reflist.next = NULL;
	object->tablecount = 0;
	lua_rawsetp(dL, OBJECT, id);
	return object;
}

static void
destroy_objects(lua_State *dL) {
	lua_pushnil(dL);
	while(lua_next(dL,OBJECT) != 0) {
		object_t *object = lua_touserdata(dL,-1);
		lua_pop(dL,1);
		ref_t *ref = object->reflist.next;
		while(ref) {
			ref_t *temp = ref->next;
			free(ref);
			ref = temp;
		}
	}
}

static int
refindex(object_t *object) {
	int index = 0;
	ref_t *ref = object->reflist.next;
	while(ref) {
		index++;
		ref_t *temp = ref->next;
		if (ref == object->refindex) {
			return index;
		}
		ref = temp;
	}
	assert(false);
}

static ref_t*
add_ref(lua_State *dL,object_t *object,const void *refobj,int reftype,const char* refname) {
	ref_t *ref = (ref_t*)calloc(1,sizeof(*ref));
	ref->obj = (void*)refobj;
	ref->type = reftype;
	strcpy(ref->name,refname);
	object->refcount++;
	ref->next = object->reflist.next;
	object->reflist.next = ref;
	return ref;
}

static bool
ignore(lua_State *L) {
	lua_getfield(L,LUA_REGISTRYINDEX,"ignore_handle");
	if (lua_isnil(L,-1)) {
		lua_pop(L,1);
		return false;
	}
	int handle = (int)lua_tointeger(L,-1);
	lua_pop(L,1);
	lua_rawgeti(L,LUA_REGISTRYINDEX,handle);
	if (lua_isnil(L,-1)) {
		lua_pop(L,1);
		return false;
	}

	lua_pushvalue(L,-2);
	if (lua_pcall(L,1,1,0) != LUA_OK) {
		lua_pop(L,1);
		return false;
	}
	bool ok = lua_toboolean(L,-1);
	lua_pop(L,1);
	return ok;
}

static bool
ismarked(lua_State *dL, const void *p) {
	lua_rawgetp(dL, MARK, p);
	if (lua_isnil(dL,-1)) {
		lua_pop(dL,1);
		lua_pushboolean(dL,1);
		lua_rawsetp(dL, MARK, p);
		return false;
	}
	lua_pop(dL,1);
	return true;
}

static bool
is_object_table(lua_State *L) {
	lua_Integer current = (lua_Integer)lua_topointer(L,-1);
	lua_getfield(L,LUA_REGISTRYINDEX,"__SNAPSHOT");
	lua_Integer history = lua_tointeger(L,-1);
	lua_pop(L,1);
	return current == history;
}

static object_t *
readobject(lua_State *L, lua_State *dL, const void *refobj, int reftype,const char *refname,int depth) {
	int type = lua_type(L, -1);
	switch (type) {
	case LUA_TTABLE:
		if (is_object_table(L)) {
			lua_pop(L,1);
			return NULL;
		}
		break;
	case LUA_TFUNCTION:
		break;
	case LUA_TTHREAD:
		break;
	case LUA_TUSERDATA:
		break;
	default:
		return NULL;
	}
	object_t *object = NULL;
	ref_t *ref = NULL;
	const void * p = lua_topointer(L, -1);
	if (ismarked(dL, p)) {
		object = get_object(dL,p);
		if (object) {
			ref = add_ref(dL,object,refobj,reftype,refname);
			if (depth < object->depth) {
				object->depth = depth;
				object->refindex = ref;
			}
		}
		lua_pop(L,1);
		return NULL;
	}
	if (ignore(L)) {
		lua_pop(L,1);
		return NULL;
	}
	assert(get_object(dL,p) == NULL);
	object = add_object(dL,p,type,depth,NULL);
	ref = add_ref(dL,object,refobj,reftype,refname);
	object->refindex = ref;
	return object;
}

static const char *
keystring(lua_State *L, int index, char * buffer) {
	lua_Integer inum;
	lua_Number num;
	int t = lua_type(L,index);
	switch (t) {
	case LUA_TSTRING:
		return lua_tostring(L,index);
	case LUA_TNUMBER:
		inum = lua_tointeger(L,index);
		num = lua_tonumber(L,index);
		if (inum == num && LUA_VERSION_NUM >= 503) {
			#if defined(LUA_USE_WINDOWS)
			sprintf(buffer,"[%I64]",inum);
			#else
			sprintf(buffer,"[%lld]",(long long)inum);
			#endif
		} else {
			sprintf(buffer,"[%lg]",num);
		}
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
mark_table(lua_State *L, lua_State *dL, const void * refobj, int reftype,const char *refname,int depth) {
	object_t *object = readobject(L, dL, refobj, reftype,refname,depth);
	if (object == NULL)
		return;

	const void * t = object->id;
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

		luaL_checkstack(L, LUA_MINSTACK, NULL);
		mark_table(L,dL,t,REF_TYPE_META,"[metatable]",depth+1);
	}

	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		object->tablecount++;
		if (weakv) {
			lua_pop(L,1);
		} else {
			char temp[32];
			const char * key = keystring(L, -2, temp);
			mark_object(L,dL,t,REF_TYPE_TABLE_VALUE,key,depth+1);
		}
		if (!weakk) {
			lua_pushvalue(L,-1);
			mark_object(L,dL,t,REF_TYPE_TABLE_KEY,"[tablekey]",depth+1);
		}
	}

	lua_pop(L,1);
}

static void
mark_userdata(lua_State *L, lua_State *dL, const void * refobj, int reftype,const char *refname,int depth) {
	object_t *object = readobject(L, dL, refobj, reftype,refname,depth);
	if (object == NULL)
		return;
	const void * t = object->id;
	if (lua_getmetatable(L, -1)) {
		mark_table(L, dL, t, REF_TYPE_META,"[metatable]",depth+1);
	}

	lua_getuservalue(L,-1);
	if (lua_isnil(L,-1)) {
		lua_pop(L,2);
	} else {
		mark_table(L, dL, t, REF_TYPE_META,"[uservalue]",depth+1);
		lua_pop(L,1);
	}
}

static void
mark_function(lua_State *L, lua_State *dL, const void * refobj, int reftype,const char *refname,int depth) {
	object_t *object = readobject(L, dL, refobj, reftype,refname,depth);
	if (object == NULL)
		return;
	const void * t = object->id;
	mark_function_env(L,dL,t,depth+1);
	int i;
	for (i=1;;i++) {
		const char *name = lua_getupvalue(L,-1,i);
		if (name == NULL)
			break;
		mark_object(L, dL, t, REF_TYPE_UPVALUE,name[0] ? name : "[upvalue]",depth+1);
	}
	if (lua_iscfunction(L,-1)) {
		strcpy(object->source,"cfunction");
		if (i==1) {
			// light c function
			if (LUA_VERSION_NUM >= 502) {
				lua_pushnil(dL);
				lua_rawsetp(dL,OBJECT,t);
			} else {
				// lua5.1 function reference a environment
			}
		}
		lua_pop(L,1);
	} else {
		lua_Debug ar;
		lua_getinfo(L, ">S", &ar);
		snprintf(object->source,MAX_NAME_SIZE,"%s:%d",ar.short_src,ar.linedefined);
	}
}

static void
mark_thread(lua_State *L, lua_State *dL, const void * refobj, int reftype,const char *refname,int depth) {
	object_t *object = readobject(L, dL, refobj, reftype,refname,depth);
	if (object == NULL)
		return;
	const void * t = object->id;
	int level = 0;
	lua_State *cL = lua_tothread(L,-1);
	if (cL == L) {
		level = 1;
	} else {
		// mark stack
		int top = lua_gettop(cL);
		luaL_checkstack(cL, 1, NULL);
		int i;
		char tmp[16];
		for (i=0;i<top;i++) {
			lua_pushvalue(cL, i+1);
			sprintf(tmp, "args[%d]", i+1);
			mark_object(cL, dL, cL, REF_TYPE_META,tmp,depth+1);
		}
	}
	lua_Debug ar;
	luaL_Buffer b;
	luaL_buffinit(dL, &b);
	while (lua_getstack(cL, level, &ar)) {
		char tmp[128];
		lua_getinfo(cL, "Sl", &ar);
		luaL_addstring(&b, ar.short_src);
		if (ar.currentline >=0) {
			char tmp[16];
			sprintf(tmp,":%d ",ar.currentline);
			luaL_addstring(&b, tmp);
		}

		int i,j;
		for (j=1;j>-1;j-=2) {
			for (i=j;;i+=j) {
				const char * name = lua_getlocal(cL, &ar, i);
				if (name == NULL)
					break;
				if (level == 1) {
					snprintf(tmp, sizeof(tmp), "%s",name);
				} else {
					snprintf(tmp, sizeof(tmp), "%s@%s:%d",name,ar.short_src,ar.currentline);
				}
				mark_object(cL,dL,t,REF_TYPE_LOCAL,tmp,depth+1);
			}
		}

		++level;
	}
	luaL_pushresult(&b);
	const char* source = lua_tostring(dL,-1);
	strncpy(object->source,source,MAX_NAME_SIZE);
	object->source[MAX_NAME_SIZE-1] = '\0';
	lua_pop(dL,1);
	lua_pop(L,1);
}

static void
mark_object(lua_State *L, lua_State *dL, const void * refobj, int reftype,const char *refname,int depth) {
	luaL_checkstack(L, LUA_MINSTACK, NULL);
	int t = lua_type(L, -1);
	switch (t) {
	case LUA_TTABLE:
		mark_table(L, dL, refobj, reftype,refname,depth);
		break;
	case LUA_TUSERDATA:
		mark_userdata(L, dL, refobj, reftype,refname,depth);
		break;
	case LUA_TFUNCTION:
		mark_function(L, dL, refobj, reftype,refname,depth);
		break;
	case LUA_TTHREAD:
		mark_thread(L, dL, refobj, reftype,refname,depth);
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
pdesc(lua_State *L, lua_State *dL, int idx) {
	int result_tidx = lua_gettop(L);
	char temp[16];
	lua_pushnil(dL);
	while (lua_next(dL, idx) != 0) {
		object_t *object = (object_t*)lua_touserdata(dL,-1);
		lua_pop(dL,1);
		sprintf(temp,"%p",object->id);
		lua_pushstring(L,temp);
		lua_createtable(L,0,8);
		int object_tidx = lua_gettop(L);
		lua_pushstring(L,temp);
		lua_setfield(L,object_tidx,"id");
		lua_pushinteger(L,object->type);
		lua_setfield(L,object_tidx,"type");
		lua_pushinteger(L,object->depth);
		lua_setfield(L,object_tidx,"depth");
		lua_pushstring(L,object->source);
		lua_setfield(L,object_tidx,"source");
		lua_pushinteger(L,object->tablecount);
		lua_setfield(L,object_tidx,"tablecount");
		lua_pushinteger(L,object->refcount);
		lua_setfield(L,object_tidx,"refcount");
		lua_pushinteger(L,refindex(object));
		lua_setfield(L,object_tidx,"refindex");
		lua_createtable(L,object->refcount,0);
		int reflist_tidx = lua_gettop(L);
		ref_t *ref = object->reflist.next;
		int i = 0;
		while(ref) {
			i++;
			lua_createtable(L,0,3);
			int ref_tidx = lua_gettop(L);
			sprintf(temp,"%p",ref->obj);
			lua_pushstring(L,temp);
			lua_setfield(L,ref_tidx,"obj");
			lua_pushinteger(L,ref->type);
			lua_setfield(L,ref_tidx,"type");
			lua_pushstring(L,ref->name);
			lua_setfield(L,ref_tidx,"name");
			lua_rawseti(L,reflist_tidx,i);
			ref = ref->next;
		}
		lua_setfield(L,object_tidx,"reflist");
		lua_rawset(L,result_tidx);
	}
}

static void
gen_result(lua_State *L, lua_State *dL) {
	int count = count_table(dL, OBJECT);
	lua_createtable(L, 0, count+1);
	lua_Integer address = (lua_Integer)lua_topointer(L,-1);
	lua_pushinteger(L,(lua_Integer)address);
	lua_setfield(L,LUA_REGISTRYINDEX,"__SNAPSHOT");
	pdesc(L, dL, OBJECT);
}

static int
snapshot(lua_State *L) {
	int handle = LUA_NOREF;
	int n = lua_gettop(L);
	if (n == 1) {
		lua_getfield(L,LUA_REGISTRYINDEX,"ignore_handle");
		if (!lua_isnil(L,-1)) {
			handle = (int)lua_tointeger(L,-1);
		}
		luaL_unref(L,LUA_REGISTRYINDEX,handle);
		lua_pop(L,1);
		if (lua_isfunction(L,1)) {
			handle = luaL_ref(L, LUA_REGISTRYINDEX);
			lua_pushinteger(L,handle);
			lua_setfield(L,LUA_REGISTRYINDEX,"ignore_handle");
		} else {
			lua_pop(L,1);
		}
	}
	int i;
	lua_State *dL = luaL_newstate();
	for (i=0;i<MARK;i++) {
		lua_newtable(dL);
	}
	lua_pushvalue(L, LUA_REGISTRYINDEX);
	mark_table(L, dL, NULL, REF_TYPE_META,"[registry]",0);
	gen_result(L, dL);
	destroy_objects(dL);
	lua_close(dL);
	luaL_unref(L,LUA_REGISTRYINDEX,handle);
	return 1;
}

int
luaopen_snapshot(lua_State *L) {
	luaL_checkversion(L);
	lua_pushcfunction(L, snapshot);
	return 1;
}
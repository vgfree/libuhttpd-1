/*
 * Copyright (C) 2017 Jianhui Zhao <jianhuizhao329@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <string.h>

#include "uhttpd.h"
#include "uhttpd-lua.h"

static void *uh_create_userdata(lua_State *L, size_t size, const luaL_Reg *reg, lua_CFunction gc)
{
	void *ret = lua_newuserdata(L, size);

	memset(ret, 0, size);
	lua_createtable(L, 0, 2);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, gc);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -3);
	lua_pushvalue(L, -2);
	luaI_openlib(L, NULL, reg, 1);
	lua_pushvalue(L, -2);

	return ret;
}

static inline void add_all_var(struct uh_client *cl, lua_State *L)
{
    const char *name, *value;

    lua_newtable(L);

    kvlist_for_each(&cl->request.var, name, value) {
        lua_pushstring(L, value);
        lua_setfield(L, -2, name);
    }

    lua_setfield(L, -2, "vars");
}

static inline void add_body(struct uh_client *cl, lua_State *L)
{
    int len;
    const char *body;

    body = cl->get_body(cl, &len);
    
    lua_pushlstring(L, body, len);
    lua_setfield(L, -2, "body");
}

static void lua_uh_action(struct uh_client *cl)
{
	struct uh_server *srv = cl->srv;
	lua_State *L = srv->L;

    lua_getglobal(L, "__uh_action_cb");
    lua_getfield(L, -1, cl->get_path(cl));

    lua_pushlightuserdata(L, cl);

    lua_newtable(L);

    lua_pushstring(L, cl->get_peer_addr(cl));
    lua_setfield(L, -2, "peer_addr");

    lua_pushstring(L, cl->get_url(cl));
    lua_setfield(L, -2, "url");

    lua_pushstring(L, cl->get_query(cl));
    lua_setfield(L, -2, "query");

    add_body(cl, L);

    add_all_var(cl, L);

    printf("ac:%p\n", cl);

    lua_call(L, 2, 0);
}

static int lua_uh_add_action(lua_State *L)
{
	struct lua_uh_server *lsrv = lua_touserdata(L, 1);
    struct uh_server *srv = lsrv->srv;
	const char *path = lua_tostring(L, -2);

    if (!srv) {
        lua_pushstring(L, "Not initialized");
        lua_error(L);
        return 0;
    }

	if (!path || !path[0] || !lua_isfunction(L, -1)) {
		lua_pushstring(L, "invalid arg list");
		lua_error(L);
		return 0;
	}

	srv->add_action(srv, path, lua_uh_action);

	lua_getglobal(L, "__uh_action_cb");
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, path);

	return 0;
}

static int lua_uh_server_free(lua_State *L)
{
	struct lua_uh_server *lsrv = lua_touserdata(L, 1);

	if (lsrv->srv) {
		lsrv->srv->free(lsrv->srv);
		lsrv->srv = NULL;
	}

	return 0;
}

static const luaL_Reg server_mt[] = {
	{ "add_action", lua_uh_add_action },
	{ "free", lua_uh_server_free },
	{ NULL, NULL }
};

static int lua_uh_new(lua_State *L)
{
	int port = lua_tointeger(L, -1);
	const char *address = lua_tostring(L, -2);
	struct lua_uh_server *lsrv;
	struct uh_server *srv;
    
	srv = uh_server_new(address, port);
    if (!srv) {
        lua_pushnil(L);
        lua_pushstring(L, "Bind failed");
        return 2;
    }

    lsrv = uh_create_userdata(L, sizeof(struct lua_uh_server), server_mt, lua_uh_server_free);
    lsrv->srv = srv;
    lsrv->srv->L = L;

	return 1;
}

static int lua_uh_send_header(lua_State *L)
{
    struct uh_client *cl = lua_touserdata(L, 1);
    int code = lua_tointeger(L, 2);
    const char *summary = lua_tostring(L, 3);
    int len = lua_tointeger(L, 4);

    cl->send_header(cl, code, summary, len);

    return 0;
}

static int lua_uh_append_header(lua_State *L)
{
    struct uh_client *cl = lua_touserdata(L, 1);
    const char *name = lua_tostring(L, 2);
    const char *value = lua_tostring(L, 2);

    cl->append_header(cl, name, value);

    return 0;
}

static int lua_uh_header_end(lua_State *L)
{
    struct uh_client *cl = lua_touserdata(L, 1);

    cl->header_end(cl);

    return 0;
}

static int lua_uh_send(lua_State *L)
{
    struct uh_client *cl = lua_touserdata(L, 1);
    const char *data;
    size_t len;

    data = lua_tolstring(L, 2, &len);
    cl->send(cl, data, len);

    return 0;

}

static int lua_uh_chunk_send(lua_State *L)
{
    struct uh_client *cl = lua_touserdata(L, 1);
    const char *data;
    size_t len;

    data = lua_tolstring(L, 2, &len);
    cl->chunk_send(cl, data, len);

    return 0;

}

static int lua_uh_request_done(lua_State *L)
{
    struct uh_client *cl = lua_touserdata(L, 1);

    cl->request_done(cl);

    return 0;
}

static const luaL_Reg uhttpd_fun[] = {
	{"new", lua_uh_new},
	{"send_header", lua_uh_send_header},
	{"append_header", lua_uh_append_header},
	{"header_end", lua_uh_header_end},
	{"send", lua_uh_send},
	{"chunk_send", lua_uh_chunk_send},
	{"request_done", lua_uh_request_done},
	{NULL, NULL}
};

int luaopen_uhttpd(lua_State *L)
{
	lua_createtable(L, 1, 0);
	lua_setglobal(L, "__uh_action_cb");

	lua_newtable(L);
	luaL_setfuncs(L, uhttpd_fun, 0);

    lua_pushstring(L, UHTTPD_VERSION_STRING);
    lua_setfield(L, -2, "VERSION");

    return 1;
}

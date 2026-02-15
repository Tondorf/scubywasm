#include "scubywasm_agent.h"

#include <stdint.h>
#include <stdlib.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#ifndef LUA_AGENT_FILE
#    error LUA_AGENT_FILE must be defined via the build system.
#endif

static const unsigned char lua_agent_source[] = {
#embed LUA_AGENT_FILE
};

struct Context
{
    lua_State *L;
    int trapped;

    int ref_init;
    int ref_set_config_parameter;
    int ref_clear_world_state;
    int ref_update_ship;
    int ref_update_shot;
    int ref_update_score;
    int ref_make_action;
};

__attribute__((import_module("debug"), import_name("debug_log"))) void
host_debug_log(uint32_t ptr, uint32_t len);

static int print(lua_State *L)
{
    const int n = lua_gettop(L);

    luaL_Buffer b;
    luaL_buffinit(L, &b);

    for (int i = 0; i < n; i++)
    {
        if (i > 0)
        {
            luaL_addchar(&b, '\t');
        }

        size_t len = 0;
        const char *s = luaL_tolstring(L, i + 1, &len);
        luaL_addlstring(&b, s, len);
        lua_pop(L, 1);
    }

    luaL_addchar(&b, '\n');
    luaL_pushresult(&b);

    size_t len = 0;
    const char *out = lua_tolstring(L, -1, &len);

    if (out)
    {
        const uint32_t uint32_max = 0xFFFFFFFF;
        host_debug_log((uint32_t)(uintptr_t)out,
                       (len <= uint32_max) ? (uint32_t)len : uint32_max);
    }

    lua_pop(L, 1);

    return 0;
}

static void ctx_trap(struct Context *ctx)
{
    if (ctx)
    {
        ctx->trapped = 1;
        if (ctx->L)
        {
            lua_settop(ctx->L, 0);
        }
    }
}

static int ctx_pcall(struct Context *ctx, int n_args, int n_results)
{
    if (!ctx || ctx->trapped)
    {
        return -1;
    }

    if (lua_pcall(ctx->L, n_args, n_results, 0) != LUA_OK)
    {
        lua_pop(ctx->L, 1);
        ctx_trap(ctx);

        return -1;
    }

    return 0;
}

static int require_global_function_ref(lua_State *L, const char *name)
{
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);

        return LUA_REFNIL;
    }

    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static void open_minimal_libs(lua_State *L)
{
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);

    luaL_requiref(L, "math", luaopen_math, 1);
    lua_pop(L, 1);

    luaL_requiref(L, "table", luaopen_table, 1);
    lua_pop(L, 1);

    lua_pushcfunction(L, print);
    lua_setglobal(L, "print");
}

static void export_constants(lua_State *L)
{
    lua_newtable(L);

    lua_pushinteger(L, (lua_Integer)(unsigned int)ACTION_NONE);
    lua_setfield(L, -2, "ACTION_NONE");
    lua_pushinteger(L, (lua_Integer)(unsigned int)ACTION_THRUST);
    lua_setfield(L, -2, "ACTION_THRUST");
    lua_pushinteger(L, (lua_Integer)(unsigned int)ACTION_TURN_LEFT);
    lua_setfield(L, -2, "ACTION_TURN_LEFT");
    lua_pushinteger(L, (lua_Integer)(unsigned int)ACTION_TURN_RIGHT);
    lua_setfield(L, -2, "ACTION_TURN_RIGHT");
    lua_pushinteger(L, (lua_Integer)(unsigned int)ACTION_FIRE);
    lua_setfield(L, -2, "ACTION_FIRE");

    lua_pushinteger(L, (lua_Integer)(unsigned int)CFG_SHIP_MAX_TURN_RATE);
    lua_setfield(L, -2, "CFG_SHIP_MAX_TURN_RATE");
    lua_pushinteger(L, (lua_Integer)(unsigned int)CFG_SHIP_MAX_VELOCITY);
    lua_setfield(L, -2, "CFG_SHIP_MAX_VELOCITY");
    lua_pushinteger(L, (lua_Integer)(unsigned int)CFG_SHIP_HIT_RADIUS);
    lua_setfield(L, -2, "CFG_SHIP_HIT_RADIUS");
    lua_pushinteger(L, (lua_Integer)(unsigned int)CFG_SHOT_VELOCITY);
    lua_setfield(L, -2, "CFG_SHOT_VELOCITY");
    lua_pushinteger(L, (lua_Integer)(unsigned int)CFG_SHOT_LIFETIME);
    lua_setfield(L, -2, "CFG_SHOT_LIFETIME");

    lua_setglobal(L, "scubywasm");
}

struct Context *
init_agent(uint32_t n_agents, uint32_t agent_multiplicity, uint32_t seed)
{
    struct Context *ctx = (struct Context *)calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        return NULL;
    }

    ctx->L = luaL_newstate();
    if (!ctx->L)
    {
        free(ctx);

        return NULL;
    }

    open_minimal_libs(ctx->L);
    export_constants(ctx->L);

    if (luaL_loadbufferx(ctx->L,
                         (const char *)lua_agent_source,
                         sizeof(lua_agent_source),
                         LUA_AGENT_FILE,
                         NULL)
        != LUA_OK)
    {
        lua_pop(ctx->L, 1);
        lua_close(ctx->L);
        free(ctx);

        return NULL;
    }

    if (lua_pcall(ctx->L, 0, 0, 0) != LUA_OK)
    {
        lua_pop(ctx->L, 1);
        lua_close(ctx->L);
        free(ctx);

        return NULL;
    }

    ctx->ref_init = require_global_function_ref(ctx->L, "init");
    ctx->ref_set_config_parameter =
        require_global_function_ref(ctx->L, "set_config_parameter");
    ctx->ref_clear_world_state =
        require_global_function_ref(ctx->L, "clear_world_state");
    ctx->ref_update_ship = require_global_function_ref(ctx->L, "update_ship");
    ctx->ref_update_shot = require_global_function_ref(ctx->L, "update_shot");
    ctx->ref_update_score = require_global_function_ref(ctx->L, "update_score");
    ctx->ref_make_action = require_global_function_ref(ctx->L, "make_action");

    if (ctx->ref_init == LUA_REFNIL
        || ctx->ref_set_config_parameter == LUA_REFNIL
        || ctx->ref_clear_world_state == LUA_REFNIL
        || ctx->ref_update_ship == LUA_REFNIL
        || ctx->ref_update_shot == LUA_REFNIL
        || ctx->ref_update_score == LUA_REFNIL
        || ctx->ref_make_action == LUA_REFNIL)
    {
        lua_close(ctx->L);
        free(ctx);

        return NULL;
    }

    lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_init);
    lua_pushinteger(ctx->L, (lua_Integer)n_agents);
    lua_pushinteger(ctx->L, (lua_Integer)agent_multiplicity);
    lua_pushinteger(ctx->L, (lua_Integer)seed);
    if (ctx_pcall(ctx, 3, 0) != 0)
    {
        lua_close(ctx->L);
        free(ctx);

        return NULL;
    }

    return ctx;
}

void free_context(struct Context *ctx)
{
    if (ctx && ctx->L)
    {
        luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref_init);
        luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref_set_config_parameter);
        luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref_clear_world_state);
        luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref_update_ship);
        luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref_update_shot);
        luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref_update_score);
        luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->ref_make_action);

        lua_close(ctx->L);
    }

    free(ctx);
}

void set_config_parameter(struct Context *ctx,
                          enum ConfigParameter param,
                          float value)
{
    if (ctx && !ctx->trapped)
    {
        lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_set_config_parameter);
        lua_pushinteger(ctx->L, (lua_Integer)(uint32_t)param);
        lua_pushnumber(ctx->L, (lua_Number)value);
        ctx_pcall(ctx, 2, 0);
    }
}

void clear_world_state(struct Context *ctx)
{
    if (ctx && !ctx->trapped)
    {
        lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_clear_world_state);
        ctx_pcall(ctx, 0, 0);
    }
}

void update_ship(struct Context *ctx,
                 uint32_t agent_id,
                 int32_t hp,
                 float x,
                 float y,
                 float heading)
{
    if (ctx && !ctx->trapped)
    {
        lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_update_ship);
        lua_pushinteger(ctx->L, (lua_Integer)agent_id);
        lua_pushinteger(ctx->L, (lua_Integer)hp);
        lua_pushnumber(ctx->L, (lua_Number)x);
        lua_pushnumber(ctx->L, (lua_Number)y);
        lua_pushnumber(ctx->L, (lua_Number)heading);
        ctx_pcall(ctx, 5, 0);
    }
}

void update_shot(struct Context *ctx,
                 uint32_t agent_id,
                 int32_t lifetime,
                 float x,
                 float y,
                 float heading)
{
    if (ctx && !ctx->trapped)
    {
        lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_update_shot);
        lua_pushinteger(ctx->L, (lua_Integer)agent_id);
        lua_pushinteger(ctx->L, (lua_Integer)lifetime);
        lua_pushnumber(ctx->L, (lua_Number)x);
        lua_pushnumber(ctx->L, (lua_Number)y);
        lua_pushnumber(ctx->L, (lua_Number)heading);
        ctx_pcall(ctx, 5, 0);
    }
}

void update_score(struct Context *ctx, uint32_t agent_id, int32_t score)
{
    if (ctx && !ctx->trapped)
    {
        lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_update_score);
        lua_pushinteger(ctx->L, (lua_Integer)agent_id);
        lua_pushinteger(ctx->L, (lua_Integer)score);
        ctx_pcall(ctx, 2, 0);
    }
}

uint32_t make_action(struct Context *ctx, uint32_t agent_id, uint32_t tick)
{
    if (!ctx || ctx->trapped)
    {
        return ACTION_NONE;
    }

    lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_make_action);
    lua_pushinteger(ctx->L, (lua_Integer)agent_id);
    lua_pushinteger(ctx->L, (lua_Integer)tick);

    if (ctx_pcall(ctx, 2, 1) != 0)
    {
        return ACTION_NONE;
    }

    int is_int = 0;
    lua_Integer v = lua_tointegerx(ctx->L, -1, &is_int);
    lua_pop(ctx->L, 1);

    if (!is_int)
    {
        ctx_trap(ctx);

        return ACTION_NONE;
    }

    return (uint32_t)v;
}

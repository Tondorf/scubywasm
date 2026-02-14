#include "scubywasm_agent.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

__attribute__((import_module("debug"), import_name("debug_log"))) void
_dbg(uint32_t ptr, uint32_t len);

static void log(const char *s)
{
    uint32_t n = 0;
    while (s[n] != '\0')
    {
        n++;
    }

    _dbg((uint32_t)(uintptr_t)s, n);
}

struct Context *init_agent(uint32_t /*n_agents*/,
                           uint32_t /*agent_multiplicity*/,
                           uint32_t /*seed*/)
{
    log("hello from freestanding agent");

    return NULL;
}

void free_context(struct Context * /*ctx*/)
{
}

void set_config_parameter(struct Context * /*ctx*/,
                          enum ConfigParameter /*p*/,
                          float /*value*/)
{
}

void clear_world_state(struct Context * /*ctx*/)
{
}

void update_ship(struct Context * /*ctx*/,
                 uint32_t /*agent_id*/,
                 int32_t /*hp*/,
                 float /*x*/,
                 float /*y*/,
                 float /*heading*/)
{
}

void update_shot(struct Context * /*ctx*/,
                 uint32_t /*agent_id*/,
                 int32_t /*lifetime*/,
                 float /*x*/,
                 float /*y*/,
                 float /*heading*/)
{
}

void update_score(struct Context * /*ctx*/,
                  uint32_t /*agent_id*/,
                  int32_t /*score*/)
{
}

uint32_t
make_action(struct Context * /*ctx*/, uint32_t /*agent_id*/, uint32_t /*tick*/)
{
    return ACTION_THRUST | ACTION_TURN_LEFT | ACTION_FIRE;
}

#ifdef __cplusplus
}
#endif

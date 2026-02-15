function set_config_parameter(param, value) end
function clear_world_state() end
function update_ship(agent_id, hp, x, y, heading) end
function update_shot(agent_id, lifetime, x, y, heading) end
function update_score(agent_id, score) end

local world_seed = 0
local rng_state = {}
local is_moving = {}

local function next_rnd(state)
    state = 1664525 * state + 1013904223

    -- drop sign bit... u is now in [0, 2^31]
    local u = state & 0x7fffffff

    return state, u / (2^31)
end

function init(n_agents, agent_multiplicity, seed)
    print("hello from Lua agent");
    world_seed = seed
end

function make_action(agent_id, tick)
    if rng_state[agent_id] == nil then
        rng_state[agent_id] = world_seed ~ agent_id
    end


    local action = 0
    local s = rng_state[agent_id]
    local r

    s, r = next_rnd(s)
    if r >= 0.9 then
        is_moving[agent_id] = not is_moving[agent_id]
    end
    if is_moving[agent_id] then
        action = action | scubywasm.ACTION_THRUST
    end

    s, r = next_rnd(s)
    if r < 0.25 then
        action = action | scubywasm.ACTION_TURN_LEFT
    elseif r < 0.50 then
        action = action | scubywasm.ACTION_TURN_RIGHT
    end

    s, r = next_rnd(s)
    if r < 0.5 then
        action = action | scubywasm.ACTION_FIRE
    end

    rng_state[agent_id] = s

    return action
end

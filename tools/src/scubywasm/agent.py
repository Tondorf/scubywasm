import functools
import logging

from .wasmmodule import WASMModule

logger = logging.getLogger(__name__)


def fuel_guard(fn):
    @functools.wraps(fn)
    def wrapper(self, *args, **kwargs):
        if self._trapped:
            return None

        try:
            return fn(self, *args, **kwargs)
        except Exception as e:
            logger.info("Filthy agent misbehaved and is now trapped: %s", e)
            self._trapped = True
            return None

    return wrapper


class Agent:
    def __init__(
        self,
        wasm,
        *,
        store,
        n_agents_total,
        agent_multiplicity,
        seed,
        engine_cfg,
        init_fuel_level=None,
    ):
        self._module = WASMModule(wasm, store=store, wasi=True)

        self._trapped = False
        self._fuel_limited = init_fuel_level is not None
        try:
            if self._fuel_limited:
                self._module.store.set_fuel(init_fuel_level)

            self._ctx = self._module.init_agent(
                n_agents_total, agent_multiplicity, seed
            )

            for i, value in enumerate(
                [
                    engine_cfg.ship_max_turn_rate,
                    engine_cfg.ship_max_velocity,
                    engine_cfg.ship_hit_radius,
                    engine_cfg.shot_velocity,
                    float(engine_cfg.shot_lifetime),
                ]
            ):
                self._module.set_config_parameter(self._ctx, i, value)
        except Exception as e:
            logger.info("Agent failed to initialize: %s", e)
            self._trapped = True

    def refuel(self, *, level):
        if not self._trapped and level is not None:
            self._module.store.set_fuel(level)

    @property
    def fuel_level(self):
        return self._module.store.get_fuel() if self._fuel_limited else None

    @property
    def trapped(self):
        return self._trapped

    @fuel_guard
    def clear_world_state(self):
        self._module.clear_world_state(self._ctx)

    @fuel_guard
    def update_ship(self, agent_id, *, is_alive, pose):
        self._module.update_ship(
            self._ctx, agent_id, 1 if is_alive else 0, pose.x, pose.y, pose.heading
        )

    @fuel_guard
    def update_shot(self, agent_id, *, lifetime, pose):
        self._module.update_shot(
            self._ctx, agent_id, lifetime, pose.x, pose.y, pose.heading
        )

    @fuel_guard
    def update_score(self, agent_id, score):
        self._module.update_score(self._ctx, agent_id, score)

    @fuel_guard
    def make_action(self, agent_id, ticks):
        return self._module.make_action(self._ctx, agent_id, ticks)

import argparse
import datetime
import errno
import json
import os
import pathlib
from random import random
import signal
import tempfile
import concurrent.futures as cf
from time import sleep
from .server import Logger
from .game import Game

RESULTS_DIR = None
ENGINE_WASM = None

class Scenario:
    def __init__(self, name, multiplicity=1, max_ticks=1000, fuel_limit=1000, max_rounds=100):
        self.name = name
        self.multiplicity = multiplicity
        self.max_ticks = max_ticks
        self.fuel_limit = fuel_limit
        self.max_rounds = max_rounds
        self.agents = {}
        self.round = 0
        self.seed = random.randint(0, 2**32 - 1)
        self.notified = False
        self.result_dir = None

    def gather_agents(self):
        agents = {}
        for user_dir in pathlib.Path("/home").glob("*"):
            if user_dir.is_dir():   
                user = user_dir.stem
                for agent_file in user_dir.glob("agents/*/*.wasm"):
                    agent_name = agent_file.parent.stem
                    if agent_name not in agents or agent_file.stat().st_mtime > agents[agent_name][1]:
                        agents[agent_name] =  (agent_file, agent_file.stat().st_mtime, agent_file.stat().st_size, user)
        return agents
    
    def need_restart(self, agents):
        if set(agents.keys()) != set(self.agents.keys()):
            return True
        for agent_name, (_, mtime, size, _) in agents.items():
            if agent_name not in self.agents:
                return True
            old_mtime, old_size = self.agents[agent_name][1], self.agents[agent_name][2]
            if mtime != old_mtime or size != old_size:
                return True
        return False
    
    def run_game(self):
        agent_files = {name: (agent_file, user) for name, (agent_file, _, _, user) in self.agents.items()}
        agents=[]
        for agent_file, user in agent_files.values():
            dest_file = self.result_dir / f"{user}-{agent_file.parent.name}-{agent_file.stem}.wasm"
            if not dest_file.exists():
                dest_file.write_bytes(agent_file.read_bytes())
            agents.append(dest_file)
        agent_wasms = [file_name.read_bytes() for file_name in agents]

        logger = Logger(self.result_dir, verbose=True)

        game = Game(
            ENGINE_WASM.read_bytes(), agent_wasms, seed=self.seed, agent_multiplicity=self.multiplicity, agent_fuel_limit=self.fuel_limit
        )

        for _ in range(self.max_ticks):
            n_teams_alive = game.tick()
            if n_teams_alive <= 1:
                break

        log = game.log["history"]
        final_scores = [team["scores"][-1] for team in log]
        teams = [f"{file.stem}" for file in agents]
        logger.save_log(dict(teams=teams, final_scores=final_scores) | game.log)

    def run(self):
        agents = self.gather_agents()
        if not agents or len(agents) == 0:
            self.round = 0
            self.agents = {}
            if not self.notified:
                print(f"Warning: no agents found for scenario '{self.name}', sleeping...")
                self.notified = True
            sleep(5)
            return self
        if self.need_restart(agents) or self.result_dir is None:
            self.round = 0
            self.agents = agents
            self.result_dir = RESULTS_DIR / self.name / datetime.datetime.now().strftime("%Y%m%d-%H%M%S.%f")[:-3]
            self.result_dir.mkdir(parents=True, exist_ok=True)

        elif self.round >= self.max_rounds:
            if not self.notified:
                print(f"Reached max rounds for scenario '{self.name}', sleeping...")
                self.notified = True
            sleep(5)
            return self
        self.notified = False
        self.run_game()
        self.round += 1
        return self
    
SCENARIO_KEYS = [("name", str),
                 ("multiplicity", int), 
                 ("max_ticks", int),
                 ("fuel_limit", int)]

def _validate_scenario(scenario, index):
    if not isinstance(scenario, dict):
        raise ValueError(f"scenario at index {index} must be an object")
    missing = [key for key, _ in SCENARIO_KEYS if key not in scenario]
    if missing:
        raise ValueError(
            f"scenario at index {index} is missing required properties: {', '.join(missing)}"
        )
    if not isinstance(scenario["name"], str):
        raise ValueError(f"scenario at index {index} must have string 'name'")
    for key, expected_type in SCENARIO_KEYS[1:]:
        value = scenario[key]
        if not isinstance(value, expected_type):
            raise ValueError(
                f"scenario '{scenario['name']}' must have integer '{key}'"
            )

def _read_scenarios(scenario_file):
    if not os.path.exists(scenario_file):
        raise FileNotFoundError(f"scenario file {scenario_file!s} does not exist")
    s = json.load(scenario_file.open())
    if not isinstance(s, list):
        raise ValueError("scenario file must contain a JSON array")
    scenarios = {}

    for index, scenario in enumerate(s):
        _validate_scenario(scenario, index)
        scenarios[scenario["name"]] = Scenario(
            scenario["name"],
            scenario["multiplicity"],
            scenario["max_ticks"],
            scenario["fuel_limit"],
            scenario["max_rounds"]
        )
    
    return scenarios

def main():
    parser = argparse.ArgumentParser(
        prog="scubywasm-service-runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=(
            "run continuous Scubywasm matches on a server in parallel and persist each finished "
            "game as a JSON log.\n\n"
            "what it does\n"
            "  Starts continuous scubywasm games running user-provided agents in a number of defined scenarios in parallel.\n"
            "  Each scenario is run in a separate process. A scenario is run until any of the agents is changed or updated,\n"
            "  in which case it is restarted with the new agents. Each finished game is written to disk as a JSON log\n"
            "  compatible with scubywasm-display alongside the used agent wasm files in a results folder. This is intended\n"
            "  to be downloaded via a HTTP-server.\n\n"
            "agents\n"
            "  Agents are taken from a subfolder 'agents/<agent-name>' inside each user's home directory.\n"
            "  For each subfolder, the server selects exactly one wasm file: the latest version present (by date).\n"
            "  Internally, the server keeps track of the used agent versions and restarts scenarios using an agent if a new \n"
            "  version is detected or a change in the number of the agents is detected. The agents will be renamed to\n"
            " '<user>-<agent-name>.wasm' in the results folder.\n\n"
            
        ),
    )
    parser.add_argument(
        "engine_wasmfile",
        type=pathlib.Path,
        metavar="ENGINE_WASM",
        help="path to the engine wasm module",
    )
    parser.add_argument(
        "results_dir",
        type=pathlib.Path,
        metavar="RESULTS_DIR",
        help=(
            "path to results directory; finished game logs and used agent wasm files will be written here"
        ),
    )
    parser.add_argument(
        "scenario_file",
        type=pathlib.Path,
        metavar="SCENARIO_FILE",
        default=None,
        help=(
            "list of scenarios to run; each scenario is run in a separate process and restarted when any of the used"
            "agents is updated"
        ),
    )
    args = parser.parse_args()

    scenarios = _read_scenarios(args.scenario_file)

    if not args.results_dir.exists():
        args.results_dir.mkdir(parents=True)
    if not args.results_dir.exists() or not args.results_dir.is_dir():
        raise NotADirectoryError(f"results path {args.results_dir!s} does not exist or is not a directory")
    try:
        testfile = tempfile.TemporaryFile(dir = args.results_dir)
        testfile.close()
    except OSError as e:
       raise OSError(f"results path {args.results_dir!s} is not writable")
    
    if not args.engine_wasmfile.exists() or not args.engine_wasmfile.is_file():
        raise FileNotFoundError(f"engine wasm file {args.engine_wasmfile!s} does not exist or is not a file")
    
    global RESULTS_DIR, ENGINE_WASM
    RESULTS_DIR = args.results_dir
    ENGINE_WASM = args.engine_wasmfile

    stopping = False
    
    def _ignore_sigint():
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)

    def _on_sigint(signum, frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, _on_sigint)
    signal.signal(signal.SIGTERM, _on_sigint)

    with cf.ProcessPoolExecutor(
        max_workers=len(scenarios), initializer=_ignore_sigint
    ) as ex:
        futures = set()
        for scenario in scenarios.values():
            futures.add(ex.submit(scenario.run))

        while futures:
            done, pending = cf.wait(
                futures,
                return_when=cf.FIRST_COMPLETED,
            )

            futures = pending
            for future in done:
                try:
                    scenario = future.result()
                except Exception as e:
                    print(f"Worker failed: {e!r}")

                if not stopping:
                    futures.add(ex.submit(scenario.run))

if __name__ == "__main__":
    main()

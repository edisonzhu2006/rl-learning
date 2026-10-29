# RL Learning

Reviewing reinforcement learning by building things.

- **[RL_Curriculum.md](RL_Curriculum.md)**: an 11-week review plan. Stage 1 goes from tabular methods through DQN, PPO, SAC and world models on a 2D racer (Gymnasium CarRacing). Stage 2 moves to 3D sims (Donkey Car, MetaDrive) for imitation and offline RL, generalization, multi-agent traffic and learned rewards.
- **[islanders/](islanders/)**: a fast, deterministic, cloneable C simulator of the city builder [ISLANDERS](https://store.steampowered.com/app/1046030/ISLANDERS/), built as an RL environment for PPO and AlphaZero-style agents.

## Islanders

| Path | What it is |
|---|---|
| [DESIGN.md](islanders/DESIGN.md) | Environment design: rules, modeling decisions, observation, action and reward |
| [REAL_GAME_SPEC.md](islanders/REAL_GAME_SPEC.md) | Exact rules extracted from the installed game. Overrides DESIGN.md where they disagree |
| [c/](islanders/c/) | The simulator (C11): `src/sim.c`, `include/islanders.h`, generated rule tables, tests, a greedy baseline, and Python bindings with a Gymnasium env (`python/islanders_sim.py`) |
| [mod/](islanders/mod/) | BepInEx mod (`IslandExporter`) that exports real islands from the running game and accepts commands. `client.py` drives it from Python |
| [data/](islanders/data/) | Extracted rule tables, real island exports, islands converted to `.isl` binaries, and game session logs |

### Build and run

```sh
cd islanders/c
make                 # library, tests and greedy example
make test
./build/greedy ../data/islands_bin/s0_*.isl
```

When the JSON in `data/extracted/` changes, `make` regenerates `src/rules_data.c` with `gen/gen_rules.py`. To add new islands exported by the mod:

```sh
python3 islanders/c/tools/islands_to_bin.py <export.json or folder> --out islanders/data/islands_bin
```

**Note:** the data in `islanders/data/extracted/` and everything derived from it was extracted from a personal copy of the game for research. ISLANDERS is by Grizzly Games; this project isn't affiliated with them.

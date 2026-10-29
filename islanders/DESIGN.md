# Islanders in C++: Game Model and RL Environment Design

*Created: 2026-10-28*

**Goal:** a fast, deterministic, cloneable C++ model of [ISLANDERS](https://store.steampowered.com/app/1046030/ISLANDERS/) (Grizzly Games, 2019), exposed to Python as a Gymnasium environment for PPO and AlphaZero-style agents.

---

## 1. What the game actually is (rules we need to model)

ISLANDERS is a minimalist city builder with no resources, traffic or economy. You place buildings from an inventory onto a procedurally generated island, and each placement scores points based on what's nearby ([Wikipedia](https://en.wikipedia.org/wiki/Islanders_(video_game))). That makes it unusually clean to model: a turn-based, single-player game with an exact score.

### 1.1 Scoring
- **Each building has a base value plus proximity bonuses and penalties.** When you hold a building, a translucent sphere shows its radius. Anything inside that radius adds to or subtracts from its score, and different building types have different radii ([awkwardmixture review](http://awkwardmixture.blogspot.com/2024/05/islanders-challenging-casual-city.html)).
- **The score is computed once, at placement, for the placed building only.** Buildings that are already down are not rescored. A community guide puts it this way: "once a score is gained - a high scoring structure can have as many negative scoring structures placed around and it doesn't matter" ([Score & Strategy Bible](https://steamcommunity.com/sharedfiles/filedetails/?id=1704459130)).
- **Interactions are asymmetric, so placement order matters.** "A Sawmill scores 7 for a Lumberjack" but a Lumberjack only scores 5 for a Sawmill, so you place Lumberjacks first. Where the values happen to be symmetric, order doesn't matter: "The order in which you place Fountains, Houses, and Mansions does not affect your point gain" ([Become an Efficient Islander](https://steamcommunity.com/sharedfiles/filedetails/?id=3300334936)).
- **Radii differ per building, so "in range" is asymmetric too.** A House can see a City Center that can't see the House. The model has to handle both.
- **Natural features count as neighbors.** Trees, Flowers and Gold Veins score for some buildings (Lumberjack +Tree, Shaman +Flower, Gold Mine +GoldVein). Building over trees removes them ("Don't feel bad about building over the trees" ([Bible](https://steamcommunity.com/sharedfiles/filedetails/?id=1704459130))).

So the score for placing building `b` at position `p` is:

```
score(b, p) = base[b] + Σ_{e ∈ entities, dist(p, e.pos) ≤ radius[b]} W[b][type(e)]
```

`W[b][t]` is the like/dislike value of type `t` *as seen by* `b`, and `W[b][b]` is the "SELF" penalty. The total score is the sum over all placements.

### 1.2 Terrain ("land affinity")
Each building can only be placed on certain terrain: Grass, Sand, Rock, Cliff, Shallows or Water. For example, Mason goes on Rock or Cliff, Fisher on Water or Shallows, Sandpit only on Sand, and Field only on Grass ([Building Quick Reference](https://steamcommunity.com/sharedfiles/filedetails/?id=2087011580)). Some buildings have extra conditions:
- **Gold Mine** has to be placed near a Gold Vein, and its pack only appears if the island has one ([Efficient Islander](https://steamcommunity.com/sharedfiles/filedetails/?id=3300334936)).
- **Plateau, Water Plateau and Wall Plateau** create new buildable land, on land, water and cliffs respectively.

### 1.3 Progression
- **Packs.** Reaching a points milestone lets you choose one of **two** building packs. The pack refills your inventory and can unlock new building types ([Wikipedia](https://en.wikipedia.org/wiki/Islanders_(video_game))). A pack type you never pick never spawns, and eventually the game forces certain packs ([Efficient Islander](https://steamcommunity.com/sharedfiles/filedetails/?id=3300334936)). Example contents: the City Pack is 3 Mansions, 2 Houses and 1 City Center ([Gametrodon](https://gametrodon.com/islanders-quick-revision/)).
- **Milestone thresholds.** Counted in points earned on the current island, they are 20, 70, 125, 185, 250, 320, 395, 475, and so on. After the first, each gap is 5 more than the previous one. They reset on each new island ([Islanders Wiki, Scoring](https://islanders.fandom.com/wiki/Scoring); [Steam discussion](https://steamcommunity.com/app/1046030/discussions/0/3570700856124684734/)).
- **Islands.** A separate island gauge fills with points. Once it's full you can move to a new island, and your total score carries over. The points needed for each new island go up every time ([Wikipedia](https://en.wikipedia.org/wiki/Islanders_(video_game))).
- **Game over.** The session ends when you run out of buildings, or space to place them, before unlocking the next island.
- **Undo.** Original mode has an undo for the last placement. The RL environment doesn't need it.

### 1.4 What isn't public (must be calibrated or made configurable)

> **Update 2026-10-28:** most of these are now answered from the game files. See [REAL_GAME_SPEC.md](REAL_GAME_SPEC.md), which overrides this section and Appendix A. Still open: exact terrain generation, and whether inventory carries over between islands.
| Unknown | Plan |
|---|---|
| Exact radius per building | Community estimates give the radius as a multiple of the footprint ("Lumberjack … 10x : Very Small", "Sawmill … 4x : Medium" ([Bible](https://steamcommunity.com/sharedfiles/filedetails/?id=1704459130))). Mansions have a much smaller radius than Houses ([Efficient Islander](https://steamcommunity.com/sharedfiles/filedetails/?id=3300334936)). Start from these estimates, then calibrate against in-game score previews. |
| Footprint shapes and sizes | Use rectangles in grid cells, sized roughly from the descriptions (Hut smallest, Temple and Market large) |
| What counts as "in range" (center point vs. any part of the footprint) | Config flag. Default: the neighbor's center is within the radius |
| Island-gauge thresholds | Config curve |
| Exact pack contents, which packs are offered when, forced packs | Config. City Pack contents are known; fill in the rest by playing |
| Whether the inventory carries over to a new island | Config flag |
| How steep a building's footprint can be, and whether height counts toward the scoring radius | Config: `maxSlope` per building and the vertical weight `k`. Calibrate by placing buildings next to plateaus and hills in the real game |
| Conflicting values between sources | e.g. Lumberjack per Tree is +1 in one guide and +3 in another; one player measured +1 in game ([awkwardmixture](http://awkwardmixture.blogspot.com/2024/05/islanders-challenging-casual-city.html)). Keep all values in JSON and treat them as tunable |

The model doesn't need to be a pixel-perfect clone to be a good RL environment. It needs the same structure: asymmetric one-time scoring, radius interactions, terrain constraints, limited space, and stochastic pack choices.

---

## 2. Modeling decisions

| Real game | Model | Why |
|---|---|---|
| Continuous 3D placement, sliding/clipping tricks | **2D grid**, e.g. 64×64 cells, with **elevation stored per cell** (ground height, surface height, plateau id; see §3.6) | Discrete action space, fast collision checks, and the per-cell structure that convolutional policies need. Elevation is a 2.5D heightmap, not voxels |
| Spheres in 3D | 3D distance over the heightmap: `dx² + dy² + (k·dz)²`, where `k` is configurable and `k = 0` gives flat 2D discs | Keeps the "sphere" behavior across hills and plateaus. Calibrate `k` against the game |
| Arbitrary footprint shapes, free rotation | Axis-aligned rectangles `w×h`, 2 orientations | A summed-area table then makes legality checks O(1) per cell |
| Building ids and natural features | **One shared type-id space** (buildings + Tree, Flower, GoldVein) | One interaction matrix `W[placed][neighbor]` covers everything |
| Score recomputation | None. A placement's score is frozen when it's placed | Matches the game, and makes the reward exact and dense |
| Plateaus | A building whose footprint becomes new terrain at a raised `surfH` (Plateau → Grass top, Water Plateau → Sand top, Wall Plateau → Rock top; tunable) | Buildings on top follow the normal rules. "Near a Plateau" bonuses use the plateau entity as a neighbor |

---

## 3. C++ architecture

```
islanders/
├── DESIGN.md
├── CMakeLists.txt                 # C++20, builds core lib + tests + Python module
├── data/
│   ├── buildings.json             # base, W row, terrain mask, radius, footprint (from Appendix A)
│   └── packs.json                 # pack contents, offer rules, milestone curves
├── include/islanders/
│   ├── types.hpp                  # Terrain, TypeId, Pos, constants
│   ├── rules.hpp                  # BuildingDef, Rules (loaded from JSON), interaction matrix
│   ├── island.hpp                 # grid layers: terrain, height, level, occupancy
│   ├── generator.hpp              # procedural island generation
│   ├── state.hpp                  # GameState (trivially copyable where possible, for MCTS)
│   ├── game.hpp                   # legal actions, step, score preview
│   └── render.hpp                 # ASCII + PPM/PNG dump
├── src/                           # implementations
├── tests/                         # Catch2/GoogleTest: scoring, legality, progression
└── python/
    ├── bindings.cpp               # pybind11 or nanobind → numpy without copies
    └── islanders_env.py           # Gymnasium env + vector env
```

### 3.1 Core types (sketch)

```cpp
enum class Terrain : uint8_t { DeepWater, Water, Shallows, Sand, Grass, Rock, Cliff };
using TypeId = uint8_t;               // buildings first, then Tree, Flower, GoldVein
constexpr int MAX_T = 48;

struct BuildingDef {
    std::string name;
    int16_t  base;
    float    radius;                  // in cells
    uint8_t  w, h;                    // footprint
    uint8_t  terrainMask;             // bit per Terrain
    bool     needsGoldVein = false;
    bool     createsLand   = false;   // plateaus
    Terrain  landTop       = Terrain::Grass;
};

struct Rules {
    std::vector<BuildingDef> defs;
    int16_t W[MAX_T][MAX_T] = {};     // W[placed][neighbor], includes SELF on the diagonal
    std::vector<Pack> packs;          // contents + offer constraints
    MilestoneCurve packCurve;         // 20, +50, +55, ... (reset per island)
    MilestoneCurve islandCurve;       // config
    bool inventoryCarriesOver = true;
    static Rules fromJson(const std::filesystem::path&);
};

struct Entity { TypeId type; int16_t x, y; uint8_t w, h; uint8_t z; };   // z = surface height it sits on

struct Island {
    int W, H;
    std::vector<Terrain>  terrain;    // top-surface terrain (plateaus overwrite)
    std::vector<uint8_t>  groundH;    // natural elevation, 0..255; sea level = rules.seaLevel
    std::vector<uint8_t>  surfH;      // height you build on: groundH, or a plateau's top
    std::vector<int16_t>  plateau;    // entity index of the plateau under this cell, or -1
    std::vector<int16_t>  occupant;   // entity index or -1
};

struct GameState {
    Island                 island;
    std::vector<Entity>    entities;          // buildings + nature
    std::array<uint8_t, MAX_T> inventory{};
    std::optional<std::array<uint8_t, 2>> packOffer;   // pending choice
    int32_t scoreTotal = 0, scoreIsland = 0;
    int32_t nextPackAt = 20, packIndex = 0, nextIslandAt;
    bool    islandUnlocked = false, done = false;
    uint64_t rngState;                         // part of the state, so clone() is exact
};
```

### 3.2 Actions

```cpp
struct ChoosePack { uint8_t option; };                       // 0 or 1, only when packOffer is set
struct Place      { TypeId type; int16_t x, y; uint8_t rot; };
struct NextIsland {};                                         // only when islandUnlocked
using Action = std::variant<ChoosePack, Place, NextIsland>;
```

At each step, the state is in exactly one phase. Either a pack choice is pending, or the agent places a building or moves to the next island. This keeps the action space factorized.

### 3.3 The key fast paths

**1. Legality mask for one building type, over all cells: O(cells).**
Build a binary grid `ok[c] = (terrain[c] ∈ mask) && occupant[c] == -1`. Take its summed-area table. A `w×h` footprint at `(x, y)` passes if the rectangle sum equals `w*h`. Two elevation checks follow (§3.6): the footprint must be flat enough (`max(surfH) − min(surfH) ≤ maxSlope[b]`), and it must not straddle a plateau edge (every cell has the same `plateau` id). Gold Mine also needs a Gold Vein within range. Footprints are tiny, so the flatness check is a direct loop; a sliding min/max is only needed for large maps.

**2. Score preview for one building type, over all cells: O(entities × disc area).**
Distance is symmetric, so instead of querying every cell's neighborhood, **stamp** each existing entity's contribution:
```
preview[:] = base[b]
for e in entities:
    v = W[b][e.type]
    if v != 0: add v to every cell within radius[b] of e.pos   // precomputed disc offsets
```
With elevation on (`k > 0`), each stamped cell also checks the vertical term: it adds `v` only if `dx² + dy² + (k·(surfH[c] − e.z))² ≤ r²`. That's one extra multiply per cell. With about 300 entities and a disc of about 700 cells, that's around 2×10⁵ adds per building type. It's cheap enough to recompute every step for every type in the inventory. The same map is:
- the input to the greedy baseline (take the argmax),
- an optional observation channel ("what would I score here?"),
- a check in the tests (the preview should equal the score `step()` returns).

**3. `step(Place)`**: check legality, compute the score (a single disc query), freeze it, add the entity, mark occupancy, remove trees under the footprint, apply land creation for plateaus, then check milestones (set `packOffer` and island unlock) and the terminal condition.

**4. `clone()`**: `GameState` is a few flat vectors, so a copy is a few memcpys. This is what makes MCTS practical, and it's the main reason to write the core in C++.

### 3.4 Randomness
All randomness (island generation, pack offers, statue placement) comes from a seeded `rngState` stored inside `GameState`. Same seed and same actions give the same game. For MCTS you can either keep the RNG in the state (a determinized tree) or treat pack offers as explicit chance nodes.

### 3.5 Island generator
1. fBm/simplex-noise heightmap × a radial falloff gives an island shape. **Keep it** as `groundH` (quantized to 0..255) and copy it to `surfH`.
2. Height thresholds relative to `seaLevel` give DeepWater / Water / Shallows / Sand / Grass / Rock. Cells whose slope (largest height difference to a 4-neighbor) exceeds `cliffSlope` become **Cliff**, whatever their height.
3. Scatter Trees and Flowers using a second noise field (forests and meadows) on Grass.
4. Place 0–1 Gold Veins on Rock and one starting Statue (until the Statue pack is unlocked, the island comes with one ([Bible](https://steamcommunity.com/sharedfiles/filedetails/?id=1704459130))).
5. Parameters: size and terrain mix (beach-heavy, cliff-heavy, archipelago). This gives you **train/test island distributions** for measuring generalization.

### 3.6 Elevation

The island is a **2.5D heightmap**: every cell has one height, with no caves or overhangs. Three per-cell arrays carry it:

| Array | Meaning | Changes when |
|---|---|---|
| `groundH[c]` | Natural ground height, 0..255. Below `seaLevel` it's water, and the depth (`seaLevel − groundH`) splits DeepWater from Shallows | Never (set by the generator) |
| `surfH[c]` | The height you build on: `groundH`, or the top of a plateau | A plateau is placed |
| `plateau[c]` | Entity id of the plateau under the cell, or −1 | A plateau is placed |

Elevation does four jobs:

1. **Terrain class.** Height relative to sea level gives water, shallows, sand, grass and rock. Steepness gives cliff. So cliffs are where the ground changes height fast, which is where the game puts Masons and Wall Plateaus.
2. **Where you can build.** A footprint must be flat enough: `max(surfH) − min(surfH) ≤ maxSlope[b]` over its cells. Buildings that belong on cliffs (Mason, Wall Plateau) get a large `maxSlope`, and most others get a small one. A footprint can't straddle a plateau edge.
3. **Plateaus as new ground.** Placing a plateau sets `surfH` over its footprint to `max(surfH under footprint) + plateauHeight`, sets `terrain` to the plateau's top type (Grass for Plateau, Sand for Water Plateau, Rock for Wall Plateau) and writes its id into `plateau[c]`. A Water Plateau over water turns unbuildable cells into land. Plateaus can stack.
4. **Scoring distance.** Each entity stores the height `z` it sits on. The range test is `dx² + dy² + (k·dz)² ≤ r²`. With `k = 0` this is a flat 2D disc. With `k = 1` the radius is a true sphere, so a building on a tall plateau can lose neighbors that sit far below it.

**Observation planes for elevation:** `groundH / 255`, `surfH / 255`, slope (or a cliff flag), and plateau occupancy. This lets the policy see hills, cliffs and plateau tops directly.

**Undo for MCTS:** a plateau placement changes `surfH`, `terrain` and `plateau` over its footprint. The state copy covers this automatically. An incremental undo would have to save those cells first.

### 3.7 Python bindings
- pybind11 or nanobind module exposing `Game`, `Rules` and a batched `VecGame` that steps N states in C++ (optionally with OpenMP) and writes observations into preallocated numpy buffers.
- `islanders_env.py`: a Gymnasium `Env` that returns observations plus an `action_mask`, and passes `gymnasium.utils.env_checker`.

---

## 4. The RL environment

### Observation
- **Spatial tensor** `[C, H, W]`:
  - terrain one-hot (7 channels)
  - elevation: `groundH`, `surfH`, slope, plateau occupancy (§3.6)
  - Tree / Flower / GoldVein maps
  - one occupancy channel per building type (center points and/or footprints)
  - *optional:* score-preview maps for each building in the inventory
- **Vector features:** inventory counts, `scoreIsland`, points to the next pack, points to the next island, the pending pack-offer ids, phase flags.

### Action
Factorized and autoregressive, with masks at every stage:
1. **Phase head:** choose pack 0 or 1 / place / next island.
2. **Type head:** which inventory building to place (masked to types you hold that have at least one legal cell).
3. **Location head:** a fully convolutional per-cell logit map, `× rotation`, masked by the legality mask.

A flat action space would be about 30 types × 4096 cells × 2 rotations ≈ 250k actions, which is too many. Factorizing keeps each head small.

### Reward and episodes
- **Reward = the placement score.** It's exact, dense and can be negative. Negative rewards matter because good play includes "investment" placements that lose points now to gain more later.
- **Episode options:** (a) a single island that ends when no legal move is left, which is the simplest; (b) a full session with island hops.
- **Simpler variant (v0):** the environment hands you the next building in a fixed order, Tetris-style, so the agent only chooses where to put it.

### Baselines (the agent has to beat these)
1. **Random legal.**
2. **Greedy:** the argmax of the preview over all inventory types and cells. Because the reward is immediate, this is a **strong** baseline.
3. **Greedy + ordering heuristic:** place "Resource Gatherers" before "Resource Hubs" ([Efficient Islander](https://steamcommunity.com/sharedfiles/filedetails/?id=3300334936)).
4. **Beam search / rollouts** using `clone()`.

The interesting RL signal is exactly where the agent beats greedy: placement order, saving space for hubs like the Temple and Market, and choosing packs to fit the island's terrain.

### Agents (connecting to the RL curriculum)
- **PPO with invalid-action masking** and a convolutional actor-critic. This is a direct use of the PPO you're about to study.
- **Gumbel AlphaZero / MuZero** using the C++ `clone()`. It's a single-player, mostly deterministic game, which suits search well.

---

## 5. Build plan

| Milestone | Deliverable | Done when |
|---|---|---|
| M0 Data | `buildings.json` from Appendix A, `packs.json` stub | Loads; matrix printed and spot-checked |
| M1 Rules core | Scoring, legality, milestones on a hand-written ASCII island | Unit tests pass (see below) |
| M2 Generator + render | Procedural islands, ASCII and PNG dumps | Islands look plausible (checked by viewing the images) |
| M3 Python | Bindings, Gymnasium env, env checker, random + greedy baselines | Env checker passes; greedy ≫ random |
| M4 PPO | Masked factorized PPO | Beats greedy on held-out islands |
| M5 Search | `VecGame`, MCTS / Gumbel AlphaZero | Beats PPO |
| M6 Calibration | Play the real game and record preview scores for about 30 placements, then fit radii and footprints | Model previews match the game within a small error |

**Unit tests to write first:**
- *Order dependence:* with Lumberjack and Sawmill in each other's range, Lumberjack then Sawmill gives 0 + (3+7) = 10, while Sawmill then Lumberjack gives 3 + (0+5) = 8 (using the Appendix A values).
- *Order independence:* any placement order of Fountain, House and Mansion within mutual range gives the same total.
- *Asymmetric range:* building A with a large radius scores from B, while B with a small radius doesn't score from A.
- *Preview consistency:* for random states, the preview at the chosen cell equals the score `step()` returns.
- *Trees:* building over trees removes them, and a later Lumberjack no longer counts them.
- *Milestones:* scoring 20 on an island sets `packOffer`. The next offer comes at 70, then 125. A new island resets the curve to 20.

---

## 6. C version

The same model works in plain C (C11). The rules, the algorithms (§3.3) and the RL interface (§4) don't change. What changes is how state, actions, data loading and bindings are expressed.

### 6.1 What changes from C++

| Concern | C++ design | C design |
|---|---|---|
| State storage | `std::vector` fields | **One fixed-size struct** with compile-time maximums. No `malloc` in the game loop |
| Clone for MCTS | Copy a few vectors | `memcpy(dst, src, sizeof *src)`. A 64×64 state is about 45 KB |
| Actions | `std::variant<ChoosePack, Place, NextIsland>` | Tagged struct: a `kind` field plus the fields each kind uses |
| Rules data | Load `buildings.json` at runtime (e.g. nlohmann/json) | **Generate a C header** from `buildings.json` at build time (`static const` tables). Optionally vendor cJSON to reload values at runtime while calibrating |
| Errors | Exceptions or `std::optional` | Integer return codes (`ISL_OK`, `ISL_ERR_ILLEGAL`, ...) |
| Python bindings | pybind11 / nanobind | **cffi** (or ctypes) over a shared library, passing numpy buffers as raw pointers. A CPython extension module is the faster but wordier alternative |
| Batch stepping | `VecGame` class | `isl_step_batch(states, n, actions, rewards)` with OpenMP |
| Tests | Catch2 / GoogleTest | A single-header test library (e.g. greatest, Unity) or plain `assert` |

### 6.2 Header sketch (`islanders.h`)

```c
#ifndef ISLANDERS_H
#define ISLANDERS_H
#include <stdint.h>
#include <string.h>

#define ISL_MAX_W     64
#define ISL_MAX_H     64
#define ISL_MAX_CELLS (ISL_MAX_W * ISL_MAX_H)
#define ISL_MAX_T     48     /* building types + Tree, Flower, GoldVein */
#define ISL_MAX_ENT   1024

typedef enum { ISL_DEEP, ISL_WATER, ISL_SHALLOWS, ISL_SAND,
               ISL_GRASS, ISL_ROCK, ISL_CLIFF, ISL_NTERRAIN } isl_terrain;

enum { ISL_NEEDS_GOLD_VEIN = 1u << 0, ISL_CREATES_LAND = 1u << 1 };
enum { ISL_OK = 0, ISL_ERR_ILLEGAL = -1, ISL_ERR_PHASE = -2, ISL_ERR_EMPTY = -3 };

typedef struct {
    const char *name;
    int16_t     base;
    float       radius;          /* cells */
    uint8_t     w, h;            /* footprint */
    uint8_t     terrain_mask;    /* bit per isl_terrain */
    uint8_t     max_slope;       /* flatness tolerance over the footprint */
    uint8_t     flags;           /* ISL_NEEDS_GOLD_VEIN | ISL_CREATES_LAND */
    uint8_t     land_top;        /* isl_terrain, for plateaus */
    uint8_t     plateau_height;
} isl_def;

typedef struct {
    int      n_types;
    isl_def  defs[ISL_MAX_T];
    int16_t  W[ISL_MAX_T][ISL_MAX_T];   /* W[placed][neighbor]; SELF on the diagonal */
    uint8_t  sea_level, cliff_slope;
    float    vertical_k;                /* 0 = flat discs, 1 = true spheres */
    /* packs, milestone curves, inventory_carries_over ... */
} isl_rules;

typedef struct { uint8_t type, w, h, z; int16_t x, y; int16_t score; } isl_entity;

typedef enum { ISL_PHASE_PLACE, ISL_PHASE_CHOOSE_PACK, ISL_PHASE_DONE } isl_phase;

typedef struct {
    int16_t    w, h;
    uint8_t    terrain[ISL_MAX_CELLS];   /* top-surface terrain */
    uint8_t    ground_h[ISL_MAX_CELLS];  /* natural elevation */
    uint8_t    surf_h[ISL_MAX_CELLS];    /* build height: ground or plateau top */
    int16_t    plateau[ISL_MAX_CELLS];   /* plateau entity under cell, -1 */
    int16_t    occ[ISL_MAX_CELLS];       /* entity id, -1 */
    uint8_t    nature[ISL_MAX_CELLS];    /* bits: tree, flower, gold vein */
    int32_t    n_ent;
    isl_entity ent[ISL_MAX_ENT];
    uint8_t    inv[ISL_MAX_T];
    uint8_t    offer[2];
    uint8_t    phase;                    /* isl_phase */
    uint8_t    island_unlocked;
    int32_t    score_total, score_island;
    int32_t    next_pack_at, pack_index, next_island_at;
    uint64_t   rng[4];                   /* xoshiro256**, part of the state */
} isl_state;

typedef enum { ISL_ACT_CHOOSE_PACK, ISL_ACT_PLACE, ISL_ACT_NEXT_ISLAND } isl_act_kind;
typedef struct { uint8_t kind, option, type, rot; int16_t x, y; } isl_action;

/* setup */
void isl_rules_default(isl_rules *r);                       /* tables generated from buildings.json */
void isl_new_game(isl_state *s, const isl_rules *r, uint64_t seed, int w, int h);

/* queries: out arrays hold s->w * s->h entries */
int  isl_legal_mask(const isl_state *s, const isl_rules *r, int type, int rot, uint8_t *out);
void isl_preview(const isl_state *s, const isl_rules *r, int type, int rot, int16_t *out);
int  isl_score_at(const isl_state *s, const isl_rules *r, int type, int rot, int x, int y);

/* transition: returns ISL_OK or an error code; *reward = placement score */
int  isl_step(isl_state *s, const isl_rules *r, isl_action a, int32_t *reward);
void isl_step_batch(isl_state *s, int n, const isl_rules *r,
                    const isl_action *a, int32_t *rewards, int32_t *codes);

/* RL view */
int  isl_obs_planes(void);                                   /* number of channels */
void isl_observe(const isl_state *s, const isl_rules *r, float *planes, float *vec);

static inline void isl_clone(isl_state *dst, const isl_state *src) { memcpy(dst, src, sizeof *dst); }

#endif
```

### 6.3 Layout for the C version

```
islanders/c/
├── CMakeLists.txt          # builds libislanders (static + shared), tests, OpenMP optional
├── include/islanders.h
├── src/
│   ├── rules.c             # isl_rules_default, using the generated tables
│   ├── generator.c         # heightmap, terrain, nature
│   ├── game.c              # legality, preview, step, milestones
│   └── observe.c           # planes + vector features
├── gen/
│   ├── gen_rules.py        # buildings.json → rules_data.h (run by CMake)
│   └── rules_data.h        # generated, not edited by hand
├── tests/test_rules.c      # same unit tests as §5
└── python/
    ├── build_cffi.py       # cffi builder over islanders.h
    └── islanders_env.py    # Gymnasium env (same interface as the C++ one)
```

### 6.4 Which one to build
- **C** keeps the state a single flat struct, which suits MCTS (memcpy clones, no allocator in the loop). The binding layer is simple, and the code is easy to port to the GPU or WASM later.
- **C++** is quicker to develop in (containers, `std::variant`, JSON, pybind11), and the tests are nicer to write.
- **If you want both,** write the core in C and add a thin C++ wrapper (RAII, pybind11) on top. That's the usual way to support both without duplicating the game logic.

---

## Appendix A: Building interaction table

From the community [Building Quick Reference](https://steamcommunity.com/sharedfiles/filedetails/?id=2087011580). Values may differ slightly from the current game version. Keep them in JSON and treat them as tunable.

| Building | Base | Likes (+) | Dislikes (−) | Terrain |
|---|---|---|---|---|
| Statue | 15 | — | Self −25 | Grass, Sand, Rock |
| CityCenter | 15 | Fisher 7, Statue 5, Mason 4, Mill 2, SeaweedFarm 2 | Shaman −5, ResortOasis −15, Self −40 | Grass, Sand, Rock |
| HopField | 5 | Statue 4, Brewery 4, ResortOasis 3, Self 2 | — | Grass, Sand |
| Brewery | 3 | HopField 5, Statue 4, Warehouse 2, SeaweedFarm 2 | Self −11 | Grass, Sand |
| Mill | 8 | Statue 5, Warehouse 5, Field 4, CityCenter 3, Mason 2 | Self −20 | Grass |
| Field | 3 | Mill 5, Statue 5, Plateau 3, ResortOasis 3, Self 1 | — | Grass |
| Sawmill | 3 | Lumberjack 7, Warehouse 6, Statue 4 | Self −12 | Grass, Sand, Rock |
| Lumberjack | 0 | Sawmill 5, Statue 4, Tree 1 | Self −6 | Grass, Sand, Rock |
| Mason | 9 | Brickyard 4, Statue 3, Warehouse 3, Tower 2, Market 2, CityCenter 2, Plateau 2, Temple 2, Wall 2 | Self −4, Shaman −5 | Rock, Cliff |
| Brickyard | 6 | Mason 5, Warehouse 4, Sandpit 4, Statue 3 | Self −8 | Grass, Sand, Rock |
| Sandpit | 9 | Brickyard 3, Statue 3 | Self −3 | Sand |
| GoldMine | −20 | GoldVein 35, Warehouse 10, Jewelry 10, Statue 5 | ResortOasis −10 | Grass, Sand, Rock (near GoldVein) |
| Jewelry | −35 | GoldMine 30, Statue 6, Mansion 5 | Wall −3, House −4, Self −30 | Grass, Sand, Rock |
| Shaman | 0 | Statue 5, Temple 5, Flower 3, SeaweedField 2 | Mason −6, CityCenter −10, Self −13 | Grass, Sand, Rock |
| House | 1 | CityCenter 6, Shaman 5, Statue 4, Plateau 3, WallPlateau 3, Circus 3, Fountain 2, Market 2, Temple 2, Tavern 2, Tower 2, Mason 2, Park 1, Self 1 | Wall −2, Jewelry −2 | Grass, Sand, Rock |
| Mansion | 1 | CityCenter 8, Shaman 6, Plateau 6, WallPlateau 6, Statue 5, Fountain 3, Park 2, Market 2, Tower 2, Temple 2, Jewelry 2, Self 1 | Circus −3, Wall −3 | Grass, Sand, Rock |
| Fisher | 4 | CityCenter 7, Warehouse 2, Statue 2, Hut 1, SeaweedField 1 | SeaweedFarm −4, Self −11 | Water, Shallows |
| SeaweedFarm | 10 | SeaweedField 4, Statue 3 | Self −24 | Grass, Sand, Rock, Shallows, Cliff |
| SeaweedField | −1 | SeaweedFarm 9, Plateau 4, Statue 3, Self 1 | — | Water |
| Fountain | 0 | CityCenter 7, Statue 3, Park 3, Mansion 3, House 2 | Self −15 | Grass, Sand, Rock |
| Park | 2 | ResortOasis 5, Fountain 4, Temple 4, Tower 3, HopField 3, Field 2, Plateau 2, Statue 2, CityCenter 2, Tree 1, House 1, Mansion 1 | Wall −2, Warehouse −2, Brickyard −3, Lumberjack −4, Mill −4, Sawmill −5 | Grass, Sand |
| Plateau | 0 | CityCenter 10, Mason 8, Self 4, Statue 2, SeaweedField 1 | — | Grass, Sand, Rock, Shallows, Water |
| ResortOasis | 0 | HopField 7, Plateau 6, WaterPlateau 5, Park 5, Fountain 5, Statue 4, Field 3, Fisher 2, Brewery 2, Tree 1 | Mill −3, Lumberjack −5, Brickyard −5, House −5, SeaweedField −5, Mansion −7, Mason −10, Warehouse −10, CityCenter −10, Sawmill −10, GoldMine −10 | Grass, Sand, Rock |
| Hut | 4 | CityCenter 6, Statue 2, Shaman 2, Fisher 1, SeaweedFarm 1 | Sawmill −1, Lumberjack −1, Warehouse −1, Brickyard −1, Mill −1, Mason −1 | Grass, Sand, Rock |
| Tower | 0 | Temple 8, Mason 5, Balloon 5, Statue 4, Wall 1, House 1, Park 1, Mansion 1 | Self −30 | Grass, Sand, Rock, Cliff |
| Warehouse | 0 | Market 7, GoldMine 7, Mill 7, Fisher 6, Brickyard 6, Mason 5, Brewery 5, Statue 4, Sawmill 4, SeaweedFarm 4 | ResortOasis −5, Self −15 | Grass, Sand, Rock |
| Market | −20 | Warehouse 20, CityCenter 5, Mason 4, Statue 4, Fisher 3, House 2, Mansion 2 | Self −35 | Grass, Sand, Rock |
| Circus | 0 | Statue 10, House 4 | Mansion −5, Self −10, Temple −25 | (not listed in source) |
| Tavern | −15 | Brewery 18, CityCenter 8, Statue 4, House 2 | Self −15 | Grass, Sand, Rock |
| Temple | −75 | Statue 40, Plateau 25, Mason 25, Shaman 15, CityCenter 10, Tower 10, Mansion 10, Wall 10, House 8, Park 5 | Market −20, Sawmill −20, Brickyard −20, Mill −20, GoldMine −20, Tavern −20, Circus −20, Warehouse −20 | Grass, Sand, Rock |
| Wall | 2 | Temple 10, Market 7, Jewelry 7, CityCenter 6, Warehouse 6, Statue 4, Plateau 4, Park 3, Mason 3, House 3, Mansion 3, Hut 1 | Self −12 | Grass, Sand, Rock, Shallows, Water |
| Monument | −15 | Temple 20, CityCenter 15, Park 8, Statue 4 | Field −5, HopField −7, Self −100 | Grass, Sand, Rock |
| WaterPlateau | 13 | Sawmill 3, Statue 2, Self 1, Tree 1 | — | Sand, Shallows, Water |
| WallPlateau | 10 | CityCenter 4, Statue 2, Self 1 | — | Cliff |
| Balloon | −15 | ResortOasis 15, Market 15, Tower 15, Circus 15, Statue 10 | Temple −25, Self −45 | Grass, Sand, Rock, Cliff, Shallows, Water |

Natural entity types used as neighbors: **Tree**, **Flower**, **GoldVein**.

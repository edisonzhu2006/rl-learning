"""Python bindings for the ISLANDERS C simulator, plus a Gymnasium environment.

    from islanders_sim import Sim, IslandersEnv
    sim = Sim("islanders/data/islands_bin/s0_IG_01_Start_Island_812126.isl", seed=0)
    sim.offers(); sim.choose_pack(0)
    scores = sim.score_map(type_id, rot)       # int16 array (nz, nx), ILLEGAL where you can't build
    sim.place(type_id, rot, px, pz)

Build the library first: make -C islanders/c
"""
import ctypes as C
import glob
import pathlib
import random

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
LIB = C.CDLL(str(HERE.parent / "build" / "libislanders.dylib"))
ILLEGAL = -32768
MAX_NX = 160

LIB.isl_island_load.restype = C.c_void_p
LIB.isl_island_load.argtypes = [C.c_char_p]
LIB.isl_island_free.argtypes = [C.c_void_p]
LIB.isl_state_size.restype = C.c_size_t
LIB.isl_reset.argtypes = [C.c_void_p, C.c_void_p, C.c_uint64]
LIB.isl_legal.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int]
LIB.isl_score_at.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int]
LIB.isl_score_map.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_void_p]
LIB.isl_place.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int, C.POINTER(C.c_int)]
LIB.isl_offers.argtypes = [C.c_void_p, C.c_void_p]
LIB.isl_choose_pack.argtypes = [C.c_void_p, C.c_int]
LIB.isl_travel.argtypes = [C.c_void_p, C.c_void_p]
LIB.isl_end.argtypes = [C.c_void_p]
LIB.isl_info.argtypes = [C.c_void_p, C.c_void_p]
LIB.isl_inventory.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
LIB.isl_observe.argtypes = [C.c_void_p, C.c_void_p]
LIB.isl_type_name.restype = C.c_char_p
LIB.isl_pack_name.restype = C.c_char_p
LIB.isl_placement_world.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_void_p]

N_BUILDINGS = LIB.isl_n_buildings()
N_TYPES = LIB.isl_n_types()
N_PLANES = LIB.isl_n_planes()
TYPE_NAMES = [LIB.isl_type_name(t).decode() for t in range(N_TYPES)]
TYPE_ID = {n: i for i, n in enumerate(TYPE_NAMES)}
ERRORS = {-1: "illegal", -2: "not in inventory", -3: "no pack waiting", -4: "island not unlocked", -5: "game over"}


class Island:
    def __init__(self, path):
        self.path = str(path)
        self.ptr = LIB.isl_island_load(self.path.encode())
        if not self.ptr:
            raise IOError(f"could not load {path}")

    def __del__(self):
        if getattr(self, "ptr", None):
            LIB.isl_island_free(self.ptr)


class Sim:
    """One game. Keeps a reference to every island it has been on so their memory stays valid."""

    def __init__(self, island, seed=0):
        self.islands = [island if isinstance(island, Island) else Island(island)]
        self.buf = C.create_string_buffer(LIB.isl_state_size())
        self.ptr = C.cast(self.buf, C.c_void_p)
        LIB.isl_reset(self.ptr, self.islands[0].ptr, seed)

    def clone(self):
        other = Sim.__new__(Sim)
        other.islands = list(self.islands)
        other.buf = C.create_string_buffer(self.buf.raw, len(self.buf))
        other.ptr = C.cast(other.buf, C.c_void_p)
        return other

    # ---- state ----
    def info(self):
        a = (C.c_int32 * 16)()
        LIB.isl_info(self.ptr, a)
        keys = ["score", "next_pack_at", "last_pack_at", "pending", "n_offer", "offer0", "offer1", "island_unlocked",
                "done", "island_index", "next_island_at", "nx", "nz", "n_entities", "packs_k", "island_start_score"]
        return dict(zip(keys, list(a)))

    def inventory(self):
        inv, var = (C.c_uint8 * N_BUILDINGS)(), (C.c_uint8 * N_BUILDINGS)()
        LIB.isl_inventory(self.ptr, inv, var)
        return {TYPE_NAMES[t]: inv[t] for t in range(N_BUILDINGS) if inv[t]}, list(var)

    def inventory_array(self):
        inv, var = np.zeros(N_BUILDINGS, np.uint8), np.zeros(N_BUILDINGS, np.uint8)
        LIB.isl_inventory(self.ptr, inv.ctypes.data, var.ctypes.data)
        return inv, var

    def observe(self):
        i = self.info()
        out = np.zeros((N_PLANES, i["nz"], i["nx"]), np.float32)
        LIB.isl_observe(self.ptr, out.ctypes.data)
        return out

    # ---- queries ----
    def score_map(self, t, rot):
        i = self.info()
        out = np.empty((i["nz"], i["nx"]), np.int16)
        LIB.isl_score_map(self.ptr, t, rot, out.ctypes.data)
        return out

    def legal(self, t, rot, px, pz, variant=-1):
        return bool(LIB.isl_legal(self.ptr, t, variant, rot, px, pz))

    def score_at(self, t, rot, px, pz, variant=-1):
        return LIB.isl_score_at(self.ptr, t, variant, rot, px, pz)

    def world(self, t, rot, px, pz, variant=-1):
        out = (C.c_float * 3)()
        LIB.isl_placement_world(self.ptr, t, variant, rot, px, pz, out)
        return tuple(out)

    # ---- actions ----
    def offers(self):
        a = (C.c_int8 * 2)()
        n = LIB.isl_offers(self.ptr, a)
        if n < 0:
            return []
        return [LIB.isl_pack_name(a[k]).decode() for k in range(n)]

    def choose_pack(self, option):
        return self._ok(LIB.isl_choose_pack(self.ptr, option))

    def place(self, t, rot, px, pz):
        r = C.c_int(0)
        self._ok(LIB.isl_place(self.ptr, t, rot, px, pz, C.byref(r)))
        return r.value

    def travel(self, island):
        isl = island if isinstance(island, Island) else Island(island)
        self._ok(LIB.isl_travel(self.ptr, isl.ptr))
        self.islands.append(isl)

    def end(self):
        LIB.isl_end(self.ptr)

    @staticmethod
    def _ok(code):
        if code < 0:
            raise ValueError(ERRORS.get(code, code))
        return code


try:
    import gymnasium as gym
    from gymnasium import spaces
except ImportError:          # the simulator works without Gymnasium
    gym = None

if gym is not None:
    class IslandersEnv(gym.Env):
        """A whole session: place buildings, open packs, travel between islands.

        Actions (one flat Discrete space, use action_masks()):
            0, 1          choose pack offer 0 or 1 (0 also opens a random pack when there is no offer)
            2             travel to the next island (when unlocked)
            3             end the session
            4 + (t * 4 + rot) * 160 * 160 + pz * 160 + px      place building t at rotation rot on cell (px, pz)
        Reward: the placement score (and the +100 travel bonus).
        Observation: "planes" (N_PLANES, 160, 160) zero-padded, "inventory" and "next_variant" (N_BUILDINGS,),
        "info" (16,) as in Sim.info().
        """
        metadata = {"render_modes": []}

        def __init__(self, island_glob, seed=0, max_islands=21):
            self.paths = sorted(glob.glob(str(island_glob)))
            if not self.paths:
                raise ValueError(f"no islands match {island_glob}")
            self.rng = random.Random(seed)
            self.max_islands = max_islands
            self.action_space = spaces.Discrete(4 + N_BUILDINGS * 4 * MAX_NX * MAX_NX)
            self.observation_space = spaces.Dict({
                "planes": spaces.Box(-10, 10, (N_PLANES, MAX_NX, MAX_NX), np.float32),
                "inventory": spaces.Box(0, 255, (N_BUILDINGS,), np.uint8),
                "next_variant": spaces.Box(0, 8, (N_BUILDINGS,), np.uint8),
                "info": spaces.Box(-1e9, 1e9, (16,), np.int32),
            })
            self.sim = None

        def _island(self):
            return Island(self.rng.choice(self.paths))

        def reset(self, seed=None, options=None):
            super().reset(seed=seed)
            if seed is not None:
                self.rng.seed(seed)
            self.sim = Sim(self._island(), seed=self.rng.randrange(1 << 62))
            return self._obs(), {}

        def _obs(self):
            p = self.sim.observe()
            planes = np.zeros((N_PLANES, MAX_NX, MAX_NX), np.float32)
            planes[:, :p.shape[1], :p.shape[2]] = p
            inv, var = self.sim.inventory_array()
            i = self.sim.info()
            return {"planes": planes, "inventory": inv, "next_variant": var,
                    "info": np.array(list(i.values()), np.int32)}

        def action_masks(self):
            m = np.zeros(self.action_space.n, bool)
            i = self.sim.info()
            if i["done"]:
                return m
            if i["pending"] > 0:
                n = len(self.sim.offers())
                m[0] = True
                m[1] = n > 1
            m[2] = bool(i["island_unlocked"]) and i["island_index"] + 1 < self.max_islands
            m[3] = True
            inv, _ = self.sim.inventory_array()
            nx, nz = i["nx"], i["nz"]
            for t in np.nonzero(inv)[0]:
                for rot in range(4):
                    legal = self.sim.score_map(int(t), rot) != ILLEGAL
                    block = np.zeros((MAX_NX, MAX_NX), bool)
                    block[:nz, :nx] = legal
                    start = 4 + (int(t) * 4 + rot) * MAX_NX * MAX_NX
                    m[start:start + MAX_NX * MAX_NX] = block.ravel()
            return m

        def step(self, action):
            before = self.sim.info()["score"]
            a = int(action)
            if a in (0, 1):
                self.sim.choose_pack(a)
            elif a == 2:
                self.sim.travel(self._island())
            elif a == 3:
                self.sim.end()
            else:
                k = a - 4
                cell = k % (MAX_NX * MAX_NX)
                tr = k // (MAX_NX * MAX_NX)
                self.sim.place(tr // 4, tr % 4, cell % MAX_NX, cell // MAX_NX)
            i = self.sim.info()
            return self._obs(), float(i["score"] - before), bool(i["done"]), False, {}

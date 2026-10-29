"""Convert island exports from the game mod (JSON) into the simulator's binary .isl format.

    python3 islanders/c/tools/islands_to_bin.py <export.json or folder> [more ...] --out islanders/data/islands_bin

Keeps the terrain scan, nature (trees, flowers, gold veins) and the pre-placed Statue. Buildings a player
placed are dropped, so exports taken from a save become clean islands.

Format (little-endian): "ISL1", int32 nx, nz, stage, seed; float cell, ox, oz, radius; 9 uint8 flags
(water_heavy, vertical, snow, ancient, allow grass/sand/rock/wood/gold); char template[48];
float height[nx*nz]; uint8 surf[nx*nz]; uint8 normal_y[nx*nz]; int32 n_objects; then per object
int16 type, int16 pad, float x, y, z, rot_y, bmin[3], bmax[3].
"""
import argparse
import json
import pathlib
import re
import struct

HERE = pathlib.Path(__file__).resolve().parent
TYPES = json.load(open(HERE.parent / "gen" / "type_names.json"))
SURF = {4: 1, 13: 2, 10: 3, 14: 4, 18: 5}       # game layer -> ISL_S_* (water, sand, grass, rock, plateau top)


def base_name(n):
    return re.sub(r"\s*\(\d+\)$", "", n.replace("(Clone)", "").strip())


def convert(src, out_dir):
    d = json.load(open(src))
    g = d["grid"]
    nx, nz = g["nx"], g["nz"]
    if nx > 160 or nz > 160:
        raise ValueError(f"{src.name}: grid {nx}x{nz} exceeds 160x160")
    height, surf, ny = [], [], []
    for row in d["cells"]:
        for c in row:
            if c["t"]:
                y, layer, n_y = c["t"][0]
                height.append(y); surf.append(SURF.get(layer, 0)); ny.append(max(0, min(255, round(n_y * 255))))
            else:
                height.append(-1.0); surf.append(0); ny.append(0)
    objs = []
    for o in d["objects"]:
        name = base_name(o["name"])
        if o.get("is_building") and name != "Statue":
            continue                       # placed by a player, not part of the island
        t = TYPES.get(name, -1)
        if "bounds_min" not in o:
            continue
        objs.append((t, *o["pos"], o.get("rot_y", 0.0), *o["bounds_min"], *o["bounds_max"]))
    f = d["flags"]
    flags = [f["water_heavy"], f["vertical"], f["snow"], f["ancient"], f["allow_grass"], f["allow_sand"],
             f["allow_rock"], f["allow_wood"], f["allow_gold"]]
    template = d["template"].replace("(Clone)", "").strip()
    buf = bytearray(b"ISL1")
    buf += struct.pack("<4i", nx, nz, d.get("stage", -1), d["seed"])
    buf += struct.pack("<4f", g["cell"], g["origin_x"], g["origin_z"], d["radius"])
    buf += bytes(int(x) for x in flags)
    buf += template.encode()[:47].ljust(48, b"\0")
    buf += struct.pack(f"<{nx*nz}f", *height)
    buf += bytes(surf) + bytes(ny)
    buf += struct.pack("<i", len(objs))
    for o in objs:
        buf += struct.pack("<hh10f", o[0], 0, *o[1:])
    stem = re.sub(r"[^A-Za-z0-9_]+", "_", f"s{d.get('stage', -1)}_{template}_{d['seed']}")
    out = out_dir / f"{stem}.isl"
    out.write_bytes(buf)
    return out, template, sum(1 for s in surf if s not in (0, 1)), len(objs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("--out", default=str(HERE.parents[1] / "data" / "islands_bin"))
    a = ap.parse_args()
    out_dir = pathlib.Path(a.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    files = []
    for p in map(pathlib.Path, a.inputs):
        files += sorted(p.glob("*.json")) if p.is_dir() else [p]
    for src in files:
        try:
            out, t, land, n = convert(src, out_dir)
            print(f"{out.name}: {t}, {land} land cells, {n} objects")
        except Exception as e:
            print(f"skipped {src.name}: {e}")


if __name__ == "__main__":
    main()

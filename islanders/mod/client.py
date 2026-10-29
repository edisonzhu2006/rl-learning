"""Python client for the Island Exporter mod's command channel.

The game must be running through run_bepinex_x64.sh with IslandExporter.dll installed.
Each call writes a request file the mod picks up on the game's main thread and waits for the reply.

    from client import Game
    g = Game()
    g.state()
    g.preview("House", [(1.2, -0.4), (1.5, -0.4)], rot=0)
    g.place("House", 1.2, -0.4, rot=0)
    g.open_pack(); g.choose_pack(0)
"""
import json
import os
import pathlib
import time
import uuid

CMD_DIR = pathlib.Path.home() / "Library/Application Support/Steam/steamapps/common/Islanders/BepInEx/IslandExports/cmd"
SAVE_DIR = pathlib.Path.home() / "Library/Application Support/Coatsink/ISLANDERS"
BACKUP_DIR = pathlib.Path(__file__).resolve().parents[1] / "data" / "save_backups"


def backup_save():
    """Copy the game's save and stats files to islanders/data/save_backups/<timestamp>/. Call before
    restarting the game or anything that places buildings, starts a new game or travels."""
    import shutil
    dst = BACKUP_DIR / time.strftime("%Y%m%d_%H%M%S")
    dst.mkdir(parents=True, exist_ok=True)
    for name in ("TheSaveV06.island", "StatsV06.stats"):
        if (SAVE_DIR / name).exists():
            shutil.copy2(SAVE_DIR / name, dst / name)
    return dst


class Game:
    def __init__(self, cmd_dir=CMD_DIR, timeout=30.0):
        self.dir = pathlib.Path(cmd_dir)
        self.timeout = timeout
        self.dir.mkdir(parents=True, exist_ok=True)

    def call(self, op, **kw):
        rid = f"{time.time_ns()}_{uuid.uuid4().hex[:6]}"
        req = {"id": rid, "op": op, **kw}
        tmp = self.dir / f"tmp_{rid}.json"
        tmp.write_text(json.dumps(req))
        os.rename(tmp, self.dir / f"req_{rid}.json")
        res = self.dir / f"res_{rid}.json"
        t0 = time.time()
        while not res.exists():
            if time.time() - t0 > self.timeout:
                raise TimeoutError(f"no reply to {op} after {self.timeout}s (is the game running with the mod?)")
            time.sleep(0.02)
        out = json.loads(res.read_text())
        res.unlink()
        if not out.get("ok", False):
            raise RuntimeError(out.get("error", out))
        return out

    def state(self):
        return self.call("state")

    def preview(self, building, spots, rot=0.0):
        """Legality and score at each (x, z) spot. Nothing is placed."""
        xs, zs = zip(*spots)
        return self.call("preview", building=building, xs=list(xs), zs=list(zs), rot=float(rot))["results"]

    def place(self, building, x, z, rot=0.0):
        return self.call("place", building=building, xs=[x], zs=[z], rot=float(rot))

    def open_pack(self):
        return self.call("open_pack")["offers"]

    def choose_pack(self, index):
        return self.call("choose_pack", index=index)

    def export(self):
        return self.call("export")

    def screenshot(self):
        return self.call("screenshot")["path"]

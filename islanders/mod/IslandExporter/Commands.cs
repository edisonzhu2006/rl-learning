using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using UnityEngine;

namespace IslandExporter
{
    // File-based command channel so Python can drive the real game.
    // Python writes BepInEx/IslandExports/cmd/req_<id>.json; the mod answers in res_<id>.json.
    // Every command runs on Unity's main thread through the game's own functions.
    [Serializable]
    public class Request
    {
        public string id;
        public string op;          // state | preview | place | open_pack | choose_pack | export | screenshot
        public string building;    // building name as in the game, e.g. "House", "Hop field"
        public float[] xs;         // world x per spot (preview takes many, place uses the first)
        public float[] zs;         // world z per spot
        public float rot;          // rotation in degrees (snapped by the game to 22.5° steps when placing by hand)
        public int index;          // choose_pack: which offered pack (0 or 1)
        public bool use_variation; // preview/diagnose: force a model variant instead of a random one
        public int variation;
        public int[] stages;       // batch: island stages to sample (0 = first island, 4 = fifth and later)
        public int count;          // batch: islands per stage
        public int seed;           // batch: base random seed
    }

    public partial class Plugin
    {
        string cmdDir;
        bool busy;
        float busySince;
        string busyOp;
        float lastHeartbeat;
        static readonly BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;

        void InitCommands()
        {
            cmdDir = Path.Combine(outDir, "cmd");
            Directory.CreateDirectory(cmdDir);
            Application.runInBackground = true;   // keep answering when the window loses focus
        }

        void PollCommands()
        {
            if (Time.realtimeSinceStartup - lastHeartbeat > 1f)
            {
                lastHeartbeat = Time.realtimeSinceStartup;
                File.WriteAllText(Path.Combine(cmdDir, "heartbeat.txt"), $"{DateTime.Now:O} busy={busy} op={busyOp}");
            }
            if (busy && !batchRunning && Time.realtimeSinceStartup - busySince > 60f)
            {
                Logger.LogWarning($"Command '{busyOp}' ran over 60 s; resetting.");
                StopAllCoroutines();
                busy = false; exporting = false;
            }
            if (busy) return;
            var req = Directory.GetFiles(cmdDir, "req_*.json").OrderBy(f => f).FirstOrDefault();
            if (req == null) return;
            Request r;
            try { r = JsonUtility.FromJson<Request>(File.ReadAllText(req)); }
            catch (Exception e) { Respond(Path.GetFileNameWithoutExtension(req).Substring(4), Err($"bad request: {e.Message}")); File.Delete(req); return; }
            File.Delete(req);
            if (string.IsNullOrEmpty(r.id)) r.id = Path.GetFileNameWithoutExtension(req).Substring(4);
            StartCoroutine(Run(r));
        }

        IEnumerator Run(Request r)
        {
            busy = true;
            busySince = Time.realtimeSinceStartup;
            busyOp = r.op;
            Logger.LogInfo($"command {r.id} {r.op} {r.building} start");
            IEnumerator body = null;
            string result = null;
            try
            {
                switch (r.op)
                {
                    case "state": result = StateJson(); break;
                    case "preview": body = Preview(r, s => result = s); break;
                    case "place": body = Place(r, s => result = s); break;
                    case "open_pack": body = OpenPack(s => result = s); break;
                    case "new_game": body = NewGame(s => result = s); break;
                    case "diagnose": body = Diagnose(r, s => result = s); break;
                    case "batch":
                        if (batchRunning) { result = Err("a batch is already running"); break; }
                        StartCoroutine(Batch(r));
                        result = "{\"ok\":true,\"started\":true}"; break;
                    case "batch_status": result = "{\"ok\":true,\"running\":" + (batchRunning ? "true" : "false") + ",\"done\":" + batchDone + ",\"total\":" + batchTotal + ",\"last\":\"" + Esc(batchLast) + "\"}"; break;
                    case "batch_stop": batchStop = true; result = "{\"ok\":true}"; break;
                    case "continue": result = Continue(); break;
                    case "choose_pack": body = ChoosePack(r, s => result = s); break;
                    case "export": Export(TerrainGenerator.singleton); result = "{\"ok\":true}"; break;
                    case "screenshot":
                        var path = Path.Combine(outDir, $"shot_{DateTime.Now:yyyyMMdd_HHmmss}.png");
                        ScreenCapture.CaptureScreenshot(path);
                        result = "{\"ok\":true,\"path\":\"" + Esc(path) + "\"}"; break;
                    default: result = Err($"unknown op '{r.op}'"); break;
                }
            }
            catch (Exception e) { result = Err(e.ToString()); }
            if (body != null)
            {
                while (true)
                {
                    bool more;
                    try { more = body.MoveNext(); }
                    catch (Exception e) { result = Err(e.ToString()); break; }
                    if (!more) break;
                    yield return body.Current;
                }
            }
            try { Respond(r.id, result ?? Err("no result")); }
            catch (Exception e) { Logger.LogError($"respond failed: {e}"); }
            Logger.LogInfo($"command {r.id} {r.op} done");
            busy = false;
        }

        void Respond(string id, string json)
        {
            var tmp = Path.Combine(cmdDir, $"res_{id}.tmp");
            File.WriteAllText(tmp, json);
            File.Move(tmp, Path.Combine(cmdDir, $"res_{id}.json"));
        }

        // ---------- state ----------

        string StateJson()
        {
            var lgm = LocalGameManager.singleton;
            var tg = TerrainGenerator.singleton;
            var sb = new StringBuilder("{\"ok\":true");
            sb.Append(",\"score\":").Append(lgm.IScore)
              .Append(",\"next_pack_at\":").Append(lgm.IRequiredScoreForNextPack)
              .Append(",\"last_pack_at\":").Append(lgm.IRequiredScoreForLastPack)
              .Append(",\"packs_unlocked\":").Append(lgm.IUnlockedBoosterPacks)
              .Append(",\"pending_packs\":[").Append(string.Join(",", lgm.liIPlusBuildingButtonsIncludingBuildingCounts)).Append(']')
              .Append(",\"next_island_at\":").Append(lgm.IRequiredScoreForNextIsland)
              .Append(",\"island_index\":").Append(lgm.ICurrentActiveIsland)
              .Append(",\"game_state\":\"").Append(lgm.GameState).Append('"')
              .Append(",\"template\":\"").Append(tg != null ? Esc(tg.gameObject.name.Replace("(Clone)", "")) : "").Append('"')
              .Append(",\"coins_in_flight\":").Append(CoinManager.Singleton != null ? CoinManager.Singleton.IGetCoinTransitionBalance() : 0)
              .Append(",\"inventory\":{");
            bool first = true;
            foreach (var kv in lgm.dicBuildingInventory)
            {
                if (kv.Key == null) continue;
                if (!first) sb.Append(',');
                first = false;
                sb.Append('"').Append(Esc(kv.Key.name)).Append("\":").Append(kv.Value);
            }
            sb.Append("}}");
            return sb.ToString();
        }

        // ---------- preview: legality + score at many spots, nothing is placed ----------

        GameObject FindPrefab(string name)
        {
            foreach (var kv in LocalGameManager.singleton.dicBuildingInventory)
                if (kv.Key != null && kv.Key.name == name) return kv.Key;
            var brain = BuildorderBrainB.singleton;
            var packs = brain.liBBPacksAll.Concat(new[] { brain.bbPackWaterPlateau, brain.bbPackWallPlateau, brain.bbPackAncientStatue });
            foreach (var p in packs)
                foreach (var go in p.liGoBuildings)
                    if (go != null && go.name == name) return go;
            return null;
        }

        LayerMask PlacementRay()
        {
            var ub = UiBuildingButtonManager.singleton;
            return (LayerMask)typeof(UiBuildingButtonManager).GetField("lmPlacementRay", Any).GetValue(ub);
        }

        bool PutOnGround(GameObject go, Building b, float x, float z, float rot, LayerMask ray, out RaycastHit hit)
        {
            var origin = new Vector3(x, 50f, z);
            if (!Physics.Raycast(origin, Vector3.down, out hit, 200f, ray, QueryTriggerInteraction.Ignore)) return false;
            go.transform.position = hit.point;
            go.transform.rotation = Quaternion.Euler(0f, rot, 0f);
            if (b.bPlaceOnWall)
            {
                var n = new Vector3(hit.normal.x, 0f, hit.normal.z);
                if (n.magnitude > 0.1f) go.transform.rotation = Quaternion.Euler(0f, Mathf.Round(Quaternion.LookRotation(n, Vector3.up).eulerAngles.y / 45f) * 45f, 0f);
            }
            Physics.SyncTransforms();
            return true;
        }

        IEnumerator Preview(Request r, Action<string> done)
        {
            var prefab = FindPrefab(r.building);
            if (prefab == null) { done(Err($"unknown building '{r.building}'")); yield break; }
            var go = UnityEngine.Object.Instantiate(prefab);
            var b = go.GetComponent<Building>();
            b.SelectVariation(r.use_variation ? r.variation : -1);
            b.Init();
            yield return null;   // let Building.Start() fill its interaction table
            yield return null;
            var ray = PlacementRay();
            var contrib = new List<ScoreContributorData>();
            var inv = CultureInfo.InvariantCulture;
            var sb = new StringBuilder("{\"ok\":true,\"building\":\"").Append(Esc(r.building)).Append("\",\"variation\":").Append(b.iVariation).Append(",\"n_variations\":").Append(b.iNumVariations).Append(",\"results\":[");
            int n = Math.Min(r.xs?.Length ?? 0, r.zs?.Length ?? 0);
            for (int i = 0; i < n; i++)
            {
                if (i > 0) sb.Append(',');
                if (!PutOnGround(go, b, r.xs[i], r.zs[i], r.rot, ray, out var hit)) { sb.Append("{\"hit\":false}"); continue; }
                if (i > 0 && i % 64 == 0) yield return null;   // keep the game responsive
                bool legal = b.BCheckBuildSpot(ray);
                int score = b.UpdateIncomeFeedback(ref contrib);
                b.RemoveOutlineFeedback();
                sb.Append("{\"legal\":").Append(legal ? "true" : "false")
                  .Append(",\"score\":").Append(score)
                  .Append(",\"y\":").Append(hit.point.y.ToString("0.####", inv))
                  .Append(",\"surface_layer\":").Append(hit.collider.gameObject.layer)
                  .Append(",\"rot\":").Append(go.transform.eulerAngles.y.ToString("0.#", inv))
                  .Append(",\"from\":[");
                for (int k = 0; k < contrib.Count; k++)
                {
                    if (k > 0) sb.Append(',');
                    sb.Append("[\"").Append(Esc(contrib[k].trans.name.Replace("(Clone)", "").Trim())).Append("\",").Append(contrib[k].iScoreContribution).Append(']');
                }
                sb.Append("]}");
            }
            sb.Append("]}");
            b.RemoveOutlineFeedback();
            UnityEngine.Object.Destroy(go);
            done(sb.ToString());
        }

        // ---------- place: through the game's own placement path ----------

        IEnumerator Place(Request r, Action<string> done)
        {
            var lgm = LocalGameManager.singleton;
            var ub = UiBuildingButtonManager.singleton;
            var button = ub.LiBuildingButtonsExisting.FirstOrDefault(btn =>
            {
                var res = (GameObject)typeof(UiBuildingButton).GetField("goResourceBuilding", Any).GetValue(btn);
                return res != null && res.name == r.building && lgm.IBuildingsInInventory(res) > 0;
            });
            if (button == null) { done(Err($"'{r.building}' is not in the inventory")); yield break; }
            int scoreBefore = lgm.IScore;
            ub.GoSelectedButton = button;           // the game creates its preview instance
            yield return null;
            yield return null;
            var go = ub.GoBuildingPreview;
            if (go == null) { done(Err("the game did not create a preview")); yield break; }
            var b = go.GetComponent<Building>();
            var ray = PlacementRay();
            if (!PutOnGround(go, b, r.xs[0], r.zs[0], r.rot, ray, out var hit)) { ub.GoSelectedButton = null; done(Err("no ground at that spot")); yield break; }
            if (!b.BCheckBuildSpot(ray)) { ub.GoSelectedButton = null; done("{\"ok\":true,\"placed\":false,\"reason\":\"illegal spot\"}"); yield break; }
            var contrib = new List<ScoreContributorData>();
            int preview = b.UpdateIncomeFeedback(ref contrib);
            typeof(UiBuildingButtonManager).GetField("iScorePreview", Any).SetValue(ub, preview);
            typeof(UiBuildingButtonManager).GetMethod("PlaceBuilding", Any).Invoke(ub, new object[] { b });
            typeof(UiBuildingButtonManager).GetField("goBuildingPreview", Any).SetValue(ub, null);
            ub.GoSelectedButton = null;
            // Score arrives through the coin animation; wait for it to land.
            float t0 = Time.realtimeSinceStartup;
            yield return null;
            while ((CoinManager.Singleton.BCoinBarFeedbackRunning || CoinManager.Singleton.IGetCoinTransitionBalance() > 0) && Time.realtimeSinceStartup - t0 < 10f)
                yield return null;
            yield return new WaitForSecondsRealtime(0.3f);
            done("{\"ok\":true,\"placed\":true,\"preview_score\":" + preview + ",\"score_before\":" + scoreBefore + ",\"score_after\":" + lgm.IScore + ",\"state\":" + StateJson() + "}");
        }

        // ---------- continue: enter the loaded island from the main menu ----------
        // Refuses unless the save has finished loading, and refuses if starting the round would
        // immediately end the game (that path autosaves an empty game over the player's save).
        string Continue()
        {
            var lgm = LocalGameManager.singleton;
            if (lgm.GameState == LocalGameManager.EGameState.InGame) return "{\"ok\":true,\"note\":\"already in game\"}";
            var tg = TerrainGenerator.singleton;
            bool loading = SaveLoadManager.bInLoadTransition;
            if (tg == null || !tg.BTerrainGenerationDone || loading) return Err("the save is still loading; try again in a few seconds");
            bool hasBuildings = lgm.dicBuildingInventory.Values.Any(v => v > 0);
            bool hasPack = lgm.liIPlusBuildingButtonsIncludingBuildingCounts.Count > 0;
            if (!hasBuildings && !hasPack && !lgm.BNextIslandAvailable) return Err("refusing: this save has no buildings, no pack and no island to travel to, so starting it would end the game and overwrite the save");
            lgm.StartRound();
            return "{\"ok\":true}";
        }

        // ---------- batch: generate and export many islands with the game's own island creation ----------
        // Replaces the island in memory, so back up the save first and restore it after quitting.

        bool batchRunning, batchStop;
        int batchDone, batchTotal;
        string batchLast = "";

        IEnumerator Batch(Request r)
        {
            batchRunning = true; batchStop = false; batchDone = 0;
            var stages = (r.stages != null && r.stages.Length > 0) ? r.stages : new[] { 0, 1, 2, 3, 4 };
            int per = Math.Max(1, r.count);
            batchTotal = stages.Length * per;
            var im = IslandManager.singleton;
            int k = 0;
            foreach (int stage in stages)
            {
                for (int j = 0; j < per && !batchStop; j++, k++)
                {
                    var sg = SaveLoadManager.SaveGameCurrent;
                    sg.liIPlaceableIds.Clear();
                    sg.liSeriTransOfPlaceables.Clear();
                    im.liIIslandsInThisRun.Clear();
                    im.iCurrentIslandIndex = stage;
                    UnityEngine.Random.InitState(r.seed + k * 7919);
                    var create = im.CreateNewIsland(LocalGameManager.EGameMode.Default, SaveGame.ELoadMode.NewGame);
                    while (true)
                    {
                        bool more;
                        try { more = create.MoveNext(); }
                        catch (Exception e) { Logger.LogError($"island creation failed: {e}"); break; }
                        if (!more) break;
                        yield return create.Current;
                    }
                    yield return new WaitForSecondsRealtime(0.3f);
                    var tg = im.GoCurrentIsland != null ? im.GoCurrentIsland.GetComponentInChildren<TerrainGenerator>() : null;
                    if (tg == null) { Logger.LogWarning("no island after creation"); continue; }
                    try { Export(tg, stage, false); batchLast = tg.gameObject.name.Replace("(Clone)", "") + " " + tg.iIslandSeed; }
                    catch (Exception e) { Logger.LogError($"export failed: {e}"); }
                    batchDone++;
                    lastGenerator = tg; lastDone = true;   // stop the auto-export from exporting it again
                    busySince = Time.realtimeSinceStartup;
                }
            }
            batchRunning = false;
            Logger.LogInfo($"batch finished: {batchDone} islands");
        }

        // ---------- new game: overwrites the current save (back it up first) ----------

        IEnumerator NewGame(Action<string> done)
        {
            LocalGameManager.singleton.NewGame();
            float t0 = Time.realtimeSinceStartup;
            yield return new WaitForSecondsRealtime(1f);
            while (Time.realtimeSinceStartup - t0 < 60f)
            {
                var tg = TerrainGenerator.singleton;
                if (tg != null && tg.BTerrainGenerationDone && LocalGameManager.singleton.GameState == LocalGameManager.EGameState.InGame) break;
                yield return null;
            }
            yield return new WaitForSecondsRealtime(3f);   // let the island settle and the auto-export run
            done("{\"ok\":true,\"state\":" + StateJson() + "}");
        }

        // ---------- diagnose: re-run each part of the placement check and report what fails ----------

        IEnumerator Diagnose(Request r, Action<string> done)
        {
            var prefab = FindPrefab(r.building);
            if (prefab == null) { done(Err($"unknown building '{r.building}'")); yield break; }
            var go = UnityEngine.Object.Instantiate(prefab);
            var b = go.GetComponent<Building>();
            b.SelectVariation(r.use_variation ? r.variation : -1);
            b.Init();
            yield return null;
            yield return null;
            var ray = PlacementRay();
            var inv = CultureInfo.InvariantCulture;
            string F(float v) => v.ToString("0.####", inv);
            var own = new HashSet<Collider>(go.GetComponentsInChildren<Collider>());
            var sb = new StringBuilder("{\"ok\":true,\"building\":\"").Append(Esc(r.building)).Append("\",\"variation\":").Append(b.iVariation)
                .Append(",\"n_variations\":").Append(b.iNumVariations).Append(",\"spots\":[");
            int n = Math.Min(r.xs?.Length ?? 0, r.zs?.Length ?? 0);
            var buf = new Collider[64];
            for (int i = 0; i < n; i++)
            {
                if (i > 0) sb.Append(',');
                if (!PutOnGround(go, b, r.xs[i], r.zs[i], r.rot, ray, out var hit)) { sb.Append("{\"hit\":false}"); continue; }
                sb.Append("{\"x\":").Append(F(r.xs[i])).Append(",\"z\":").Append(F(r.zs[i])).Append(",\"legal\":").Append(b.BCheckBuildSpot(ray) ? "true" : "false");
                // grounded probes
                sb.Append(",\"probes\":[");
                var probes = go.GetComponentsInChildren<GroundedChecker>();
                for (int k = 0; k < probes.Length; k++)
                {
                    var gc = probes[k];
                    if (k > 0) sb.Append(',');
                    bool ok = gc.BCheckGrounded(b.lmBuildingSurface, b.lmGroundedBreakers);
                    var o = gc.transform.position + Vector3.up * gc.fScanOffset;
                    string surf = "null", brk = "null";
                    if (Physics.Raycast(o, Vector3.down, out var h1, 10f, b.lmBuildingSurface)) surf = "[" + F(h1.distance) + "," + h1.collider.gameObject.layer + "]";
                    if (Physics.Raycast(o, Vector3.down, out var h2, 10f, b.lmGroundedBreakers)) brk = "[" + F(h2.distance) + "," + h2.collider.gameObject.layer + ",\"" + Esc(h2.collider.name) + "\"]";
                    sb.Append("{\"ok\":").Append(ok ? "true" : "false").Append(",\"surface\":").Append(surf).Append(",\"breaker\":").Append(brk).Append('}');
                }
                // forbidden overlaps of each box collider
                sb.Append("],\"overlaps\":[");
                bool firstO = true;
                foreach (var bc in go.GetComponentsInChildren<BoxCollider>())
                {
                    Vector3 pos = default, scale = default; Quaternion rot = default;
                    TransformTools.BoxColliderToWorldSpace(bc, ref pos, ref scale, ref rot);
                    int m = Physics.OverlapBoxNonAlloc(pos, scale * 0.5f, buf, rot, b.lmForbiddenOverlaps);
                    for (int k = 0; k < m; k++)
                    {
                        var c = buf[k];
                        if (own.Contains(c)) continue;
                        var sid = c.GetComponentInParent<StructureID>();
                        if (!firstO) sb.Append(',');
                        firstO = false;
                        sb.Append("[\"").Append(Esc(c.name)).Append("\",").Append(c.gameObject.layer).Append(",\"")
                          .Append(Esc(sid != null ? sid.gameObject.name.Replace("(Clone)", "").Trim() : "")).Append("\"]");
                    }
                }
                sb.Append("]}");
            }
            sb.Append("]}");
            UnityEngine.Object.Destroy(go);
            done(sb.ToString());
        }

        // ---------- packs ----------

        IEnumerator OpenPack(Action<string> done)
        {
            var lgm = LocalGameManager.singleton;
            if (lgm.liIPlusBuildingButtonsIncludingBuildingCounts.Count == 0) { done(Err("no pack waiting")); yield break; }
            lgm.OpenBuildingChoice();
            yield return new WaitForSecondsRealtime(1f);
            var sel = UIBuildingChoice.Singleton.liBuildingPackSelection;
            var sb = new StringBuilder("{\"ok\":true,\"offers\":[");
            for (int i = 0; i < sel.Count; i++)
            {
                if (i > 0) sb.Append(',');
                sb.Append("[").Append(string.Join(",", sel[i].liGoBuildings.Select(g => "\"" + Esc(g.name) + "\""))).Append(']');
            }
            sb.Append("]}");
            done(sb.ToString());
        }

        IEnumerator ChoosePack(Request r, Action<string> done)
        {
            var sel = UIBuildingChoice.Singleton.liBuildingPackSelection;
            if (sel.Count == 0) UIBuildingChoice.Singleton.OnNoMoreBBPacksAvailable();
            else if (r.index < 0 || r.index >= sel.Count) { done(Err($"index {r.index} out of range, {sel.Count} offers")); yield break; }
            else UIBuildingChoice.Singleton.OnButtonPress(sel[r.index]);
            yield return new WaitForSecondsRealtime(2.5f);   // buildings fly into the inventory
            done("{\"ok\":true,\"state\":" + StateJson() + "}");
        }

        static string Err(string m) => "{\"ok\":false,\"error\":\"" + Esc(m.Replace("\n", " ")) + "\"}";
    }
}

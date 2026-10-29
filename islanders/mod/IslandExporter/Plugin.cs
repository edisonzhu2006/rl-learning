using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using BepInEx;
using UnityEngine;

namespace IslandExporter
{
    // Exports every island the game finishes generating: a raycast scan of the terrain on the
    // building grid (0.1757 world units) plus every pre-placed object (trees, flowers, gold, statues, ruins).
    // Output: BepInEx/IslandExports/<time>_<template>.json and a matching screenshot .png.
    [BepInPlugin("local.islanders.islandexporter", "Island Exporter", "0.1.0")]
    public partial class Plugin : BaseUnityPlugin
    {
        const float Cell = 0.1757f;
        const float SettleSeconds = 2f;

        // Physics layers from the game's TagManager.
        const int LWater = 4, LStructure = 9, LGrass = 10, LTree = 11, LVegetation = 12, LSand = 13, LStone = 14, LWoodPlatform = 18;
        static readonly int TerrainMask = (1 << LWater) | (1 << LGrass) | (1 << LSand) | (1 << LStone) | (1 << LWoodPlatform);
        static readonly int ObjectMask = (1 << LStructure) | (1 << LTree) | (1 << LVegetation);

        string outDir;
        TerrainGenerator lastGenerator;
        bool lastDone;
        bool exporting;

        void Awake()
        {
            outDir = Path.Combine(Paths.BepInExRootPath, "IslandExports");
            Directory.CreateDirectory(outDir);
            InitCommands();
            Logger.LogInfo($"Island Exporter loaded, writing to {outDir}");
        }

        void Update()
        {
            try { PollCommands(); }
            catch (Exception e) { Logger.LogError($"Command handling failed: {e}"); busy = false; }
            var tg = TerrainGenerator.singleton;
            if (tg == null || exporting || batchRunning) return;
            bool done = tg.BTerrainGenerationDone;
            if (done && (!lastDone || tg != lastGenerator))
                StartCoroutine(ExportAfterSettle(tg));
            lastDone = done;
            lastGenerator = tg;
        }

        IEnumerator ExportAfterSettle(TerrainGenerator tg)
        {
            exporting = true;
            yield return new WaitForSeconds(SettleSeconds);
            try
            {
                if (tg != null && tg.BTerrainGenerationDone) Export(tg);
            }
            catch (Exception e)
            {
                Logger.LogError($"Export failed: {e}");
            }
            exporting = false;
        }

        void Export(TerrainGenerator tg, int stage = -1, bool screenshot = true)
        {
            Physics.SyncTransforms();

            // Grid extent: bounds of all terrain colliders, so we don't depend on where the island is spawned.
            Bounds? terrainBounds = null;
            foreach (var c in FindObjectsOfType<Collider>())
            {
                if (!c.enabled || ((1 << c.gameObject.layer) & TerrainMask) == 0) continue;
                if (terrainBounds == null) terrainBounds = c.bounds;
                else { var b = terrainBounds.Value; b.Encapsulate(c.bounds); terrainBounds = b; }
            }
            if (terrainBounds == null) { Logger.LogWarning("No terrain colliders found; skipping."); return; }
            var tb = terrainBounds.Value;

            Vector3 center = tg.transform.position;
            float half = tg.FRadius * 1.6f + 1f;
            float minX = Mathf.Max(tb.min.x, center.x - half), maxX = Mathf.Min(tb.max.x, center.x + half);
            float minZ = Mathf.Max(tb.min.z, center.z - half), maxZ = Mathf.Min(tb.max.z, center.z + half);
            int nx = Mathf.CeilToInt((maxX - minX) / Cell), nz = Mathf.CeilToInt((maxZ - minZ) / Cell);
            float rayTop = tb.max.y + 5f, rayLen = tb.size.y + 20f;

            var inv = CultureInfo.InvariantCulture;
            string F(float v, string fmt = "0.####") => v.ToString(fmt, inv);

            var sb = new StringBuilder(1 << 20);
            var lgm = LocalGameManager.singleton;
            sb.Append("{\"format\":\"islanders-island-v1\"");
            sb.Append(",\"template\":\"").Append(Esc(tg.gameObject.name)).Append('"');
            sb.Append(",\"seed\":").Append(tg.iIslandSeed);
            sb.Append(",\"stage\":").Append(stage);
            sb.Append(",\"radius\":").Append(F(tg.FRadius));
            sb.Append(",\"flags\":{\"water_heavy\":").Append(B(tg.bWaterHeavy))
              .Append(",\"vertical\":").Append(B(tg.bVertical))
              .Append(",\"snow\":").Append(B(tg.bSnow))
              .Append(",\"ancient\":").Append(B(tg.bAncient))
              .Append(",\"allow_grass\":").Append(B(tg.bAllowGrassUnlocks))
              .Append(",\"allow_sand\":").Append(B(tg.bAllowSandUnlocks))
              .Append(",\"allow_rock\":").Append(B(tg.bAllowRockUnlocks))
              .Append(",\"allow_wood\":").Append(B(tg.bAllowWoodUnlocks))
              .Append(",\"allow_gold\":").Append(B(tg.bAllowGoldUnlocks)).Append('}');
            if (lgm != null) sb.Append(",\"island_index\":").Append(lgm.ICurrentActiveIsland).Append(",\"score\":").Append(lgm.IScore);
            sb.Append(",\"center\":[").Append(F(center.x)).Append(',').Append(F(center.y)).Append(',').Append(F(center.z)).Append(']');
            sb.Append(",\"grid\":{\"cell\":").Append(F(Cell)).Append(",\"origin_x\":").Append(F(minX)).Append(",\"origin_z\":").Append(F(minZ))
              .Append(",\"nx\":").Append(nx).Append(",\"nz\":").Append(nz).Append('}');
            sb.Append(",\"layers\":{");
            int[] layerIds = { LWater, LStructure, LGrass, LTree, LVegetation, LSand, LStone, LWoodPlatform };
            for (int i = 0; i < layerIds.Length; i++)
            {
                if (i > 0) sb.Append(',');
                sb.Append('"').Append(layerIds[i]).Append("\":\"").Append(Esc(LayerMask.LayerToName(layerIds[i]))).Append('"');
            }
            sb.Append('}');

            // Terrain scan: per cell, up to 3 terrain surfaces top-down as [height, layer, normal_y]; [] if nothing.
            // Objects: the topmost tree/vegetation/structure hit as [height, layer, structure_id], or null.
            sb.Append(",\"cells\":[");
            var hitsBuf = new RaycastHit[16];
            int surfaceCells = 0;
            for (int iz = 0; iz < nz; iz++)
            {
                if (iz > 0) sb.Append(',');
                sb.Append('[');
                for (int ix = 0; ix < nx; ix++)
                {
                    if (ix > 0) sb.Append(',');
                    var origin = new Vector3(minX + (ix + 0.5f) * Cell, rayTop, minZ + (iz + 0.5f) * Cell);
                    int n = Physics.RaycastNonAlloc(origin, Vector3.down, hitsBuf, rayLen, TerrainMask, QueryTriggerInteraction.Ignore);
                    var hits = hitsBuf.Take(n).OrderBy(h => h.distance).Take(3).ToList();
                    if (hits.Count > 0) surfaceCells++;
                    sb.Append("{\"t\":[");
                    for (int k = 0; k < hits.Count; k++)
                    {
                        if (k > 0) sb.Append(',');
                        var h = hits[k];
                        sb.Append('[').Append(F(h.point.y)).Append(',').Append(h.collider.gameObject.layer).Append(',').Append(F(h.normal.y, "0.###")).Append(']');
                    }
                    sb.Append(']');
                    if (Physics.Raycast(origin, Vector3.down, out var oh, rayLen, ObjectMask, QueryTriggerInteraction.Ignore))
                    {
                        var sid = oh.collider.GetComponentInParent<StructureID>();
                        sb.Append(",\"o\":[").Append(F(oh.point.y)).Append(',').Append(oh.collider.gameObject.layer).Append(',').Append(sid != null ? sid.iID : -1).Append(']');
                    }
                    sb.Append('}');
                }
                sb.Append(']');
            }
            sb.Append(']');

            // Every object carrying a StructureID (trees, flowers, gold veins, statues, ruins, placed buildings).
            sb.Append(",\"objects\":[");
            bool first = true;
            int objCount = 0;
            foreach (var s in FindObjectsOfType<StructureID>())
            {
                if (!s.isActiveAndEnabled) continue;
                var go = s.gameObject;
                var p = go.transform.position;
                var cols = go.GetComponentsInChildren<Collider>().Where(c => c.enabled).ToArray();
                if (!first) sb.Append(',');
                first = false;
                objCount++;
                sb.Append("{\"id\":").Append(s.iID)
                  .Append(",\"name\":\"").Append(Esc(go.name.Replace("(Clone)", "").Trim())).Append('"')
                  .Append(",\"layer\":").Append(go.layer)
                  .Append(",\"is_building\":").Append(B(go.GetComponent<Building>() != null))
                  .Append(",\"pos\":[").Append(F(p.x)).Append(',').Append(F(p.y)).Append(',').Append(F(p.z)).Append(']')
                  .Append(",\"rot_y\":").Append(F(go.transform.eulerAngles.y, "0.##"));
                if (cols.Length > 0)
                {
                    var b = cols[0].bounds;
                    foreach (var c in cols) b.Encapsulate(c.bounds);
                    sb.Append(",\"bounds_min\":[").Append(F(b.min.x)).Append(',').Append(F(b.min.y)).Append(',').Append(F(b.min.z)).Append(']')
                      .Append(",\"bounds_max\":[").Append(F(b.max.x)).Append(',').Append(F(b.max.y)).Append(',').Append(F(b.max.z)).Append(']');
                }
                sb.Append('}');
            }
            sb.Append("]}");

            string stem = $"{DateTime.Now:yyyyMMdd_HHmmss}_{Safe(tg.gameObject.name)}_{tg.iIslandSeed}";
            if (stage >= 0) stem = $"batch_s{stage}_{Safe(tg.gameObject.name.Replace("(Clone)", ""))}_{tg.iIslandSeed}";
            File.WriteAllText(Path.Combine(outDir, stem + ".json"), sb.ToString());
            if (screenshot) ScreenCapture.CaptureScreenshot(Path.Combine(outDir, stem + ".png"));
            Logger.LogInfo($"Exported {stem}: grid {nx}x{nz}, {surfaceCells} cells with terrain, {objCount} objects");
        }

        static string B(bool v) => v ? "true" : "false";
        static string Esc(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
        static string Safe(string s) => new string(s.Select(ch => char.IsLetterOrDigit(ch) ? ch : '_').ToArray());
    }
}

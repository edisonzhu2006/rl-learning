# ISLANDERS: Real Game Rules (Extracted)

*Created: 2026-10-28*

**Source:** your installed copy of ISLANDERS on Steam (build 17942654, Unity 2021.3.11f1, Mono scripting). Data values were read from the game's asset files. Rules come from decompiling the gameplay classes in `Assembly-CSharp.dll` and are paraphrased here, not copied. The extracted tables are in [data/extracted/](data/extracted/).

**For personal research only:** don't redistribute the extracted data or anything derived from the game's code.

Where this spec disagrees with [DESIGN.md](DESIGN.md) or the community guides, this spec wins.

---

## 1. Scoring (exact)

When a building is placed, its score is:

```
score = iBaseScore
      + iAddScoreSnow                      (snow islands only)
      + Σ W[type(n)]  over each distinct neighbor n that is found in range
```

- **Range test:** for each range point of the building, Unity's `Physics.OverlapSphere(point, fRange)` runs against colliders on the **Structure** and **Tree** layers. A neighbor counts if **any of its colliders touches the sphere**; its center doesn't need to be inside. The range points are the building's position plus optional per-building offsets (rotated with the building), and the results are unioned. So each neighbor counts **once**, even if several spheres touch it.
- **The sphere is 3D.** Height differences matter: a building on a plateau can fail to reach one below it.
- **Range on snow islands** is `fRange × fRangeInSnowMultiplyer`. That multiplier is 1.0 for every building, so in practice only the snow score bonus matters.
- **Only types listed in the building's interaction list count.** Everything else in range is ignored. Each tree model has its own entry: the large **Mighty Oak gives +3**, and the other tree models give +1. That resolves the community's "+1 or +3 per tree" conflict.
- **Scores are frozen at placement.** No building is ever rescored. The live preview runs the same function, so the preview number is exactly what you get.

## 2. Placement legality (exact)

A spot is legal when all of these hold:

1. **Grounded probes.** Each building has several probe points (4–40, depending on the model). From each one, the game casts a ray straight down. It must hit an allowed surface layer within 0.05 units, and it must not hit a "breaker" layer before the surface.
2. **No forbidden overlaps.** Every box or sphere collider of the building must not overlap colliders on the building's forbidden layers (mostly other buildings and terrain geometry). This is what stops buildings clipping into hills or each other.
3. **Steep-surface exception.** A few buildings set `bAlwaysAllowPlacementOnSteepSurfaces`. For them the probes are skipped when the ground normal is between about 18° and 73° from vertical (`0.3 < n·up < 0.95`).
4. **Placing deletes** the Trees and Vegetation (flowers) under the building.
5. **Rotation** snaps to **16 steps** (22.5° each), or 8 steps when placed on walls. Placement snapping uses a 0.3-unit snap distance.

**Surface layers:** Grass, Sand, Stone (rock), Water, and **Wood Platform**, which is the top of a plateau. Cliffs aren't a separate surface type. They're steep Stone, and steep faces fail the probe and overlap tests unless the building allows them.

## 3. Buildings

**Units:** range is in world units. Islands have a radius of 5–10 units (most are 7). Footprint is the largest Structure-layer collider box, measured in **u = 0.176 world units**. Almost every footprint is a whole number of u (Hut 1×1, House 2×2, Temple 5×5), so **a grid of 0.176-unit cells reproduces footprints closely**. Buildings with several model variants (House, Mansion, Park, Field) show the largest variant.

**Deck copies:** how many copies of a type go into the draw deck when it's unlocked (§4.3).

**Draw gate:** a random draw of this building is only allowed if you've already received at least `amount × (1 + copies of this building received)` of the listed building. Example: each Brewery needs 2 more Hop Fields received first. The gate is ignored on snow islands.

| Building | Base | Snow bonus | Range | Footprint (u) | Surfaces | Deck copies | Draw gate | Interactions (W) |
|---|---|---|---|---|---|---|---|---|
| Balloon | −15 | 0 | 2.5 | 2.0 × 2.0 | Grass, Sand, Rock, Plateau top | 1 | — | Resort Oasis +15, Market +15, Tower +15, Circus +15, Statue +10, Temple −25, Self −45 |
| Brewery | 3 | +4 | 2.0 | 3.3 × 3.9 | Grass, Sand, Plateau top | 2 | Hop Field ≥ 2×(1+n) | Hop Field +5, Statue +4, Warehouse +2, Seaweed Farmer +2, Self −11 |
| Brickyard | 6 | +4 | 2.0 | 3.0 × 2.0 | Grass, Sand, Rock, Plateau top | 5 | Sandpit ≥ 1×(1+n) | Mason +5, Warehouse +4, Sandpit +4, Statue +3, Self −8 |
| Circus | 0 | 0 | 1.2 | 3.0 × 3.0 | Grass, Sand, Rock, Plateau top | 2 | — | Statue +10, House +4, Mansion −5, Self −10, Temple −25 |
| City Center | 15 | 0 | 2.5 | 2.0 × 2.0 | Grass, Sand, Rock, Plateau top | 5 | — | Fisher +7, Statue +5, Mason +4, Mill +2, Seaweed Farmer +2, Shaman −5, Resort Oasis −15, Self −40 |
| Field | 3 | 0 | 0.55 | 2.2 × 2.2 | Grass | 24 | Mill ≥ 0.15×(1+n) | Mill +5, Statue +5, Plateau +3, Resort Oasis +3, Self +2 |
| Fisher | 4 | 0 | 3.75 | 1.4 × 1.6 | Water | 7 | — | City Center +7, Warehouse +2, Statue +2, Hut +1, Seaweed Field +1, Seaweed Farmer −4, Self −11 |
| Fountain | 0 | 0 | 1.0 | 2.0 × 2.0 | Grass, Sand, Rock, Plateau top | 5 | — | City Center +7, Statue +3, Park +3, Mansion +3, House +2, Self −15 |
| Gold Mine | −20 | 0 | 0.5 | 1.7 × 1.7 | Grass, Sand, Rock, Plateau top | 1 | — | Gold Vein +35, Warehouse +10, Jewelry +10, Statue +5, Resort Oasis −10 |
| Hop Field | 5 | 0 | 1.2 | 2.3 × 3.2 | Grass, Sand | 8 | — | Statue +4, Brewery +4, Resort Oasis +3, Self +2 |
| House | 1 | 0 | 1.0 | 2.0 × 2.0 | Grass, Sand, Rock, Plateau top | 40 | — | City Center +6, Shaman +5, Statue +4, Plateau +3, Wall Plateau +3, Circus +3, Fountain +2, Market +2, Temple +2, Tavern +2, Tower +2, Mason +2, Park +1, Self +1, Wall −2, Jewelry −2 |
| Hut | 4 | 0 | 3.0 | 1.0 × 1.0 | Grass, Sand, Rock, Plateau top | 20 | — | City Center +6, Statue +2, Shaman +2, Fisher +1, Seaweed Farmer +1, Sawmill −1, Lumberjack −1, Warehouse −1, Brickyard −1, Mill −1, Mason −1 |
| Jewelry | −35 | 0 | 1.5 | 2.0 × 2.0 | Grass, Sand, Rock, Plateau top | 2 | — | Gold Mine +30, Statue +6, Mansion +5, Wall −3, House −4, Self −30 |
| Lumberjack | 0 | 0 | 1.6 | 0.8 × 1.6 | Grass, Sand, Rock, Plateau top | 7 | — | Sawmill +5, Statue +4, Mighty Oak +3, Tree +1, Self −6 |
| Mansion | 1 | 0 | 0.525 | 2.0 × 2.0 | Grass, Sand, Rock, Plateau top | 40 | — | City Center +8, Shaman +6, Plateau +6, Wall Plateau +6, Statue +5, Fountain +3, Park +2, Market +2, Tower +2, Temple +2, Jewelry +2, Self +1, Circus −3, Wall −3 |
| Market | −20 | 0 | 3.0 | 4.8 × 5.1 | Grass, Sand, Rock, Plateau top | 3 | — | Warehouse +20, City Center +5, Mason +4, Statue +4, Fisher +3, House +2, Mansion +2, Self −35 |
| Mason | 9 | 0 | 1.75 | 2.0 × 1.9 | Rock | 7 | — | Brickyard +4, Statue +3, Warehouse +3, Tower +2, Market +2, City Center +2, Plateau +2, Temple +2, Wall +2, Self −4, Shaman −5 |
| Mill | 8 | +2 | 2.2 | 0.7 × 1.3 * | Grass, Plateau top | 4 | Field ≥ 2×(1+n) | Statue +5, Warehouse +5, Field +4, City Center +3, Mason +2, Self −20 |
| Monument | −15 | 0 | 2.0 | 2.0 × 2.0 | Grass, Sand, Rock, Plateau top | 1 | — | Temple +20, City Center +15, Park +8, Statue +4, Field −5, Hop Field −7, Self −100 |
| Park | 2 | 0 | 0.9 | 2.0 × 2.0 | Grass, Sand | 40 | — | Resort Oasis +5, Fountain +4, Temple +4, Hop Field +3, Mighty Oak +3, Tower +3, Field +2, Plateau +2, Statue +2, City Center +2, Tree +1, House +1, Mansion +1, Wall −2, Warehouse −2, Brickyard −3, Mill −4, Lumberjack −4, Sawmill −5 |
| Plateau | 0 | 0 | 2.1 | ~7 × 7 * | Water, Grass, Sand, Rock, Plateau top | 5 | — | City Center +10, Mason +8, Self +4, Statue +2, Seaweed Field +1 |
| Resort Oasis | 0 | 0 | 2.0 | 4.8 × 3.9 | Grass, Sand, Rock | 2 | — | Hop Field +7, Plateau +6, Water Plateau +5, Park +5, Fountain +5, Statue +4, Mighty Oak +3, Field +3, Fisher +2, Brewery +2, Flower +2, Tree +1, Mill −3, Brickyard −5, Seaweed Field −5, Lumberjack −5, House −5, Mansion −7, Mason −10, Warehouse −10, City Center −10, Sawmill −10, Gold Mine −10 |
| Sandpit | 9 | 0 | 3.2 | 2.6 × 2.6 | Sand | 9 | Brickyard ≥ 0.25×(1+n) | Statue +3, Brickyard +3, Self −3 |
| Sawmill | **0** | 0 | 1.6 | 2.1 × 1.2 | Grass, Sand, Rock, Plateau top | 4 | Lumberjack ≥ 1×(1+n) | Lumberjack +7, Warehouse +6, Statue +4, Self −12 |
| Seaweed Farmer | 10 | 0 | 1.7 | 2.0 × 1.0 | Grass, Sand, Rock, Plateau top | 5 | Seaweed Field ≥ 2×(1+n) | Seaweed Field +4, Statue +3, Self −24 |
| Seaweed Field | −1 | 0 | 0.8 | 2.1 × 3.1 | Water | 18 | Seaweed Farmer ≥ 0.15×(1+n) | Seaweed Farmer +9, Plateau +4, Statue +3, Self +1 |
| Shaman | 0 | +5 | 1.5 | 1.0 × 1.0 | Grass, Sand, Rock, Plateau top | 6 | — | Statue +5, Temple +5, Flower +3, Seaweed Field +2, Mason −6, City Center −10, Self −13 |
| Statue | 15 | 0 | 3.0 | 1.7 × 1.7 | Grass, Sand, Rock, Plateau top | 3 | — | Self −25 |
| Tavern | −15 | 0 | 2.0 | 2.0 × 1.0 | Grass, Sand, Rock, Plateau top | 2 | — | Brewery +18, City Center +8, Statue +4, House +2, Self −15 |
| Temple | −75 | 0 | 1.1 | 5.0 × 5.0 | Grass, Sand, Rock, Plateau top | 1 | — | Statue +40, Plateau +25, Mason +25, Shaman +15, City Center +10, Tower +10, Mansion +10, Wall +10, House +8, Park +5, Market −20, Sawmill −20, Brickyard −20, Mill −20, Gold Mine −20, Tavern −20, Circus −20, Warehouse −20 |
| Tower | 0 | 0 | 2.75 | 1.0 × 1.0 | Grass, Sand, Rock, Plateau top | 4 | — | Temple +8, Mason +5, Balloon +5, Statue +4, Wall +1, House +1, Park +1, Mansion +1, Self −30 |
| Wall | 2 | 0 | 0.3 | 3.8 × 0.6 | Water, Grass, Sand, Rock, Plateau top | 25 | — | Temple +10, Market +7, Jewelry +7, City Center +6, Warehouse +6, Statue +4, Plateau +4, Park +3, Mason +3, House +3, Mansion +3, Hut +1, Self −12 |
| Wall Plateau | 10 | 0 | 2.5 | 5.4 × 4.4 | Grass, Sand, Rock (not Water) | 10 | — | City Center +4, Statue +2, Self +1 |
| Warehouse | 0 | 0 | 2.2 | 4.2 × 2.8 | Grass, Sand, Rock, Plateau top | 3 | — | Market +7, Gold Mine +7, Mill +7, Fisher +6, Brickyard +6, Mason +5, Brewery +5, Statue +4, Sawmill +4, Seaweed Farmer +4, Resort Oasis −5, Self −15 |
| Water Plateau | 13 | 0 | 1.5 | 5.4 × 5.4 | Water | 10 (−6 on water-heavy islands) | — | Sawmill +3, Mighty Oak +3, Statue +2, Self +1, Tree +1 |

\* The Mill's Structure colliders cover only part of the model, so its footprint is approximate. The Plateau's main body uses a mesh collider, so its footprint is approximate too.

**Corrections to the community data:** Sawmill base is 0, not 3. Trees are +1 each except the Mighty Oak at +3. Brewery, Brickyard and Mill get a snow bonus (+4, +4, +2), and so does Shaman (+5).

### 3.1 Model variants (verified in the game)

Several buildings come in more than one model, and **variants can have different footprints**. Each time you select a building, the game picks a variant at random and deletes the others; the player can't choose it outside Sandbox. Per-variant boxes and probe points are in [data/extracted/variants.json](data/extracted/variants.json).

| Building | Variants | Footprints (u = 0.176) |
|---|---|---|
| House | 7 | five 2.2 × 2.2, two L-shaped 3.2 × 2.1 |
| Mansion | 8 | four 2.2 × 2.2, four 3.2 × 2.2 |
| Park | 5 | three 2.1 × 2.1, two 3.1 × 1.1 |
| Water Plateau | 6 | four 5.4 × 5.4, two 5.3 × 8 |
| Field, Plateau, Seaweed Field, Seaweed Farmer, Tower | 2–4 | all about the same size |

**Effect on legality**, measured with the mod's diagnostic command on two islands:
- **Empty island:** the 2×2 House variants are legal at 470 of the 471 cells where a City Center fits. The big variants are legal at only 433 of those cells. Every extra failure comes from the wing reaching the shoreline (a probe hits a water or rock "breaker" layer first) or clipping a rock mesh.
- **Next to placed buildings:** 2×2 Houses and City Centers are legal in exactly the same cells (16 of 32). The smaller Lumberjack fits in 21 of 32. Every House failure there is an overlap with a placed building's Structure collider.
- **The original "House illegal everywhere" result** came from random variants. At the spot where a City Center was legal, House variants 0, 1, 2, 4 and 5 are legal, and variants 3 and 6 are blocked by "Rock Flat 01 a".

**For the simulator:** draw a random variant whenever a building is selected, and show it to the agent (for example as the footprint mask of the building in hand). Don't assume one fixed footprint per type.

## 4. Packs and the building deck (exact)

### 4.1 Milestones
- The game tracks pack thresholds against your **total score**.
- The first pack of a game comes at 0 points and gives 3 buildings. Every later pack gives **6 buildings**. This comes from the pack-size curve (3 at the first pack, 6 from then on) and matches the 3, 6, 6 recorded in your stats file.
- After each pack: `next += INC[min(k, 29)] + 2 × max(k − 30, 0)`, then round **down** to a multiple of 5. Here `k` is the number of packs unlocked so far, and `INC = [0, 20, 50, 55, 60, 65, 70, 75, 80, 85, 90, 90, 95, 95, 100, 100, 105, 105, 110, 110, 115, 115, 120, 120, 125, 125, 130, 130, 135, 135]`.
- That gives cumulative thresholds of 20, 70, 125, 185, 250, 320, 395, 475, 560, 650, 740, 835, 930, …
- Each milestone offers **2** packs to choose from.

### 4.2 Which packs are offered
1. On a **water-heavy** island the Water Plateau pack is offered first, on its own. On a **vertical** island the Wall Plateau pack comes first, and on an **ancient** island the Statue pack.
2. Otherwise the game collects every remaining pack that passes its rules:
   - The island allows the pack's terrain (grass, sand, rock, wood, gold, or "no sand").
   - The season matches (summer, winter or both).
   - At most `jokers` of the pack's prerequisite buildings are still locked.
   - The number of unlocked building types is at least the pack's minimum.

   If no pack qualifies, the game retries with tolerance 1, 2, … 10, which allows that many extra missing prerequisites and lowers the unlock minimum by the same amount.
3. Packs marked "always show" are always included; only the City pack has this flag. Random picks from the qualifying set fill the rest up to 2. The pack-offer RNG is stored separately and kept in the save.

| # | Pack (buildings) | Prerequisites (jokers) | Min unlocks | Terrain | Season |
|---|---|---|---|---|---|
| 0 | Brewery, Hop Field | Lumberjack, Mill (1) | 0 | — | Summer |
| 1 | Brickyard, Sandpit | — | 0 | Sand | Both |
| 2 | City Center, House, Mansion (always shown) | — | 3 | — | Both |
| 3 | Lumberjack, Sawmill | — | 0 | Wood | Both |
| 4 | Mason | — | 1 | Rock | Both |
| 5 | Mill, Field | — | 0 | Grass | Summer |
| 6 | Shaman | — | 3 | — | Both |
| 7 | Fisher | City Center | 0 | — | Both |
| 8 | Fountain | City Center | 6 | — | Both |
| 9 | Hut | City Center | 0 | — | Both |
| 10 | Park | City Center | 6 | — | Both |
| 11 | Plateau | City Center | 7 | — | Both |
| 12 | Tower | City Center | 0 | — | Both |
| 13 | Warehouse | Mill, Sawmill, Brewery, Fisher, City Center, House, Mansion (2) | 0 | — | Both |
| 14 | Circus | City Center | 10 | — | Both |
| 15 | Gold Mine, Jewelry | City Center | 12 | Gold | Both |
| 16 | Market | Warehouse, City Center | 0 | — | Both |
| 17 | Resort Oasis | City Center | 12 | — | Both |
| 18 | Tavern | Brewery, House | 0 | — | Both |
| 19 | Temple | City Center | 12 | — | Both |
| 20 | Wall | City Center | 12 | — | Both |
| 21 | Brewery | Lumberjack, Mill (1) | 0 | — | Winter |
| 22 | Brickyard | — | 0 | No sand | Winter |
| 23 | Mill | — | 0 | Grass | Winter |
| 24 | Seaweed Field, Seaweed Farmer | — | 0 | — | Both |
| 25 | Balloon | Circus, Tower, Resort Oasis (1) | 0 | — | Both |
| 26 | Monument | Temple | 12 | — | Both |
| — | Statue (ancient islands) | — | — | — | Both |
| — | Wall Plateau (vertical islands) | City Center | 9 | — | Both |
| — | Water Plateau (water-heavy islands) | — | 5 | — | Both |

Season comes from each pack's availability setting (Winter, Summer or Both). Winter packs only appear on snow islands, and Summer packs only on non-snow islands.

### 4.3 What a pack gives you
1. **Choosing a pack** queues one copy of each of its buildings to be given out first (three copies for Wall Plateau). Each newly unlocked type adds its **deck copies** to the draw deck, and the deck is shuffled.
2. **Per-unlock extras:** at each pack, every unlocked type may add guaranteed copies based on its per-unlock rate. In practice that's only the Water Plateau on water-heavy islands, at 1.1 per pack (the 0.1 is a random chance of one more).
3. **Filling the pack:** the pack hands out N buildings (3 for the first pack, then 6). Queued buildings come first, then random draws from the deck, re-drawn up to 100 times until one passes its draw gate. When the deck runs out, every unlocked type is refilled with its deck copies.

So a pack is: its new buildings, plus random fills from everything you've unlocked so far. For example, the City pack gives a City Center, a House and a Mansion, plus 3 random draws, which are usually more Houses and Mansions because the deck holds 40 copies of each.

## 5. Islands and the session (exact)

- **Island unlock goals**, measured in points earned **on this island**: 200, 400, 800, 1600, 2200, 2400, 2500, 2600, 2700, 2800, 2900, 3000, 3100, 3200, 3300, 3400, 3500, 3600, 3650, 3700. From island 20 on, the goal is 10,000,000, which effectively locks progress.
- **Traveling** adds **+100** points, then rounds the total score **up** to a multiple of 10. The next pack becomes due immediately, and the next goal is measured from your score on arrival.
- **Each island starts a fresh deck.** Unlocked building types, the remaining packs and the received-building counts all reset.
- **Game over** happens only when every inventory count is 0, no pack is waiting to be opened, and the next island isn't available. Running out of **space** doesn't end the game by itself.
- **Undo** reverts the last placement. It's cleared whenever you open a pack.
- **51 island templates** ([data/extracted/island_templates.json](data/extracted/island_templates.json)). Each has a radius (5–10 units) and flags: water-heavy, vertical, snow, ancient, and which terrains allow packs (grass, sand, rock, wood, gold). Examples include Start Island (radius 5, no sand, rock or gold packs), Desert, Snowland, Water L (radius 9, water-heavy), Ruin Circles L (radius 10) and All Towers (vertical). Terrain is generated procedurally from each template.

### What happens on arrival at a new island
- **From the code:** the next pack is due the moment you arrive, so you start with a pack choice. The deck and unlocks reset.
- **From several independent player reports:**
  - **Unplaced buildings are lost when you travel** ("buildings in your inventory do not move to your new island", [Steam thread](https://steamcommunity.com/app/1046030/discussions/0/3570700856124684734/); also [this thread](https://steamcommunity.com/app/1046030/discussions/0/1643168364647271634/)).
  - **You start empty with a choice of two packs** ([Nintendo World Report](http://www.nintendoworldreport.com/review/58076/islanders-console-edition-switch-review)).
  - **The next hurdle is 20 points above your arrival score** ("The game will then reset the target score to just 20 above your current", [Refined Geek](https://therefinedgeek.com.au/index.php/2019/04/07/islanders-impeccable-placement/)).
- **I didn't fully trace the inventory-clear and counter-reset code paths.** The model should default to: empty inventory, pack counter reset (first pack at arrival, next at +20, and so on), deck reset. Keep both behind config flags.
- **The island goals extracted above match the current values players compiled after the November 2023 rebalance** (2,400 for island 6, then +100 each up to 3,600, then 3,650 and 3,700; [guide 3546049746](https://steamcommunity.com/sharedfiles/filedetails/?id=3546049746)). They also fill in islands 1–5 (200, 400, 800, 1,600, 2,200), which players hadn't fully recorded. The session stops progressing at island 21, matching the February 2023 patch notes.
- **Unpicked packs usually come back.** A pack you don't choose stays in the eligible pool, so it tends to be offered again. Players describe this as being "forced" to take it eventually ([janner66 review](https://steamcommunity.com/id/janner66/recommended/1046030/)). Nothing guarantees it, though; the code picks randomly from the eligible packs.
- **Each milestone you cross queues one pack.** Unopened packs wait behind the "+" button, and you can open several in a row.

## 6. What the model needs to change

| Area | Current model | Change to |
|---|---|---|
| Values | Community table | [data/extracted/buildings.json](data/extracted/buildings.json): exact base, interactions and snow bonus for all 34 buildings |
| Radius | Guessed, center-to-center 2D | Exact `fRange` in world units. A neighbor counts if **any part of its footprint** is within range, in 3D |
| Grid | Arbitrary cells | **0.176-unit cells**. A radius-7 island is about 80 cells across |
| Footprints | Guessed rectangles | Collider boxes from [data/extracted/colliders.json](data/extracted/colliders.json), plus 16 rotation steps (or approximate with 4 orientations) |
| Trees | One type | Tree +1, Mighty Oak +3, Flower as a separate type. All are deleted under placed buildings |
| Packs | Invented | §4: the real pack table, offer rules, deck copies, draw gates, 3-then-6 pack size |
| Milestones | 20, +50, +5 per pack | The exact increment list and round-down rule (§4.1) |
| Islands | Single island | Goals list (island points), +100 travel bonus rounded up to 10, and on arrival: inventory emptied, deck and pack counter reset, pack choice offered at once. Plus each template's island flags |
| Game over | Out of buildings or space | Out of buildings with nothing pending (§5). Agents need an explicit "end session" action for the out-of-space case |
| Terrain generation | Simple noise | Still approximate: real islands are procedural per template. Match each template's radius and flags, and the share of each terrain type |

## 7. Still approximate

- **Terrain generation.** The 51 templates are procedural and not extracted. The model can match each template's radius, flags and rough terrain mix, but not exact shapes.
- **Exact 3D geometry:** collider heights, model variants, and the slope at each probe point.
- **Inventory and pack-counter reset** between islands. Player reports say both reset; the code path isn't fully traced (§5).

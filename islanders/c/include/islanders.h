/* ISLANDERS simulator: a fast C model of the real game's rules (see islanders/REAL_GAME_SPEC.md).
 *
 * The island terrain comes from real islands exported from the game (islanders/data/real_islands,
 * converted to .isl files by tools/islands_to_bin.py). Rules come from the game's extracted data
 * (gen/gen_rules.py writes src/rules_data.c).
 *
 * Coordinates: the island is a grid of ISL_CELL world units. A building is placed by its pivot cell
 * (px, pz) and rotation rot in quarter turns (rot k = k * 90 degrees, Unity's convention). Its footprint
 * cells and ground probes are offsets from the pivot, taken from its model variant.
 *
 * The state is one flat struct with no pointers except to the read-only island, so it can be copied
 * with memcpy (isl_clone) for search.
 */
#ifndef ISLANDERS_H
#define ISLANDERS_H

#include <stdint.h>
#include <string.h>
#include "islanders_types.h"

#define ISL_MAX_NX 160
#define ISL_MAX_GRID (ISL_MAX_NX * ISL_MAX_NX)
#define ISL_MAX_ENT 1024
#define ISL_MAX_PENDING 32
#define ISL_MAX_GUARANTEED 64
#define ISL_ILLEGAL (-32768)

enum { ISL_S_NONE = 0, ISL_S_WATER, ISL_S_SAND, ISL_S_GRASS, ISL_S_ROCK, ISL_S_PLATEAU, ISL_N_SURF };
enum { ISL_SEASON_WINTER = 0, ISL_SEASON_SUMMER = 1, ISL_SEASON_BOTH = 2 };
enum { ISL_OBSTACLE = 255 };   /* entity type for blocking objects that don't score (rocks, ruins) */

/* ---------- rule tables (generated) ---------- */

typedef struct {
    float offset[2];                                      /* model pivot relative to the pivot cell centre (x, z), rot 0 */
    uint8_t n_cells;  int8_t cells[ISL_MAX_CELLS][2];     /* footprint offsets (dx, dz) at rot 0 */
    uint8_t n_probes; int8_t probes[ISL_MAX_PROBES][2];   /* ground probe offsets at rot 0 */
    uint8_t n_boxes;  float boxes[ISL_MAX_BOXES][6];       /* collider AABBs (min xyz, max xyz) in local space */
} isl_variant;

typedef struct {
    const char *name;
    int16_t base, snow_bonus;
    float range;
    uint8_t n_range_pts; float range_pts[ISL_MAX_RANGE_PTS][3];  /* sphere centres relative to the pivot */
    uint8_t surf_mask;      /* surfaces the ground probes may land on */
    uint8_t breaker_mask;   /* surfaces that fail a probe if hit first */
    uint8_t steep_ok;       /* may sit on slopes (Mason, Seaweed Farmer) */
    uint8_t clip_ok;        /* may intersect terrain (plateaus, Mason, Tower, Seaweed Farmer) */
    uint8_t is_plateau; float plateau_top;   /* plateaus become buildable ground at base + plateau_top */
    uint8_t on_wall;        /* Wall Plateau: attaches to cliff walls */
    int16_t deck, deck_water; float per_unlock_water;
    int16_t gate_type; float gate_amount;    /* random draw only if received[gate] >= amount * (1 + received[self]) */
    uint8_t re_add;
    uint8_t n_variants; isl_variant variants[ISL_MAX_VARIANTS];
} isl_def;

typedef struct {
    const char *name;
    uint8_t n_b; uint8_t b[4];
    uint8_t n_req; uint8_t req[8];
    int8_t jokers; int32_t min_unlocks;
    uint8_t need_grass, need_sand, need_no_sand, need_rock, need_wood, need_gold, season, always_show;
} isl_pack;

extern const isl_def ISL_DEFS[ISL_N_TYPES];
extern const int16_t ISL_W[ISL_N_TYPES][ISL_N_TYPES];
extern const isl_pack ISL_PACKS[ISL_N_PACKS];
extern const int ISL_PACK_INC[ISL_N_PACK_INC];
extern const int ISL_ISLAND_GOALS[ISL_N_ISLAND_GOALS];

/* ---------- island (read-only, loaded from a .isl file) ---------- */

typedef struct {
    int16_t type;            /* ISL_T_* for nature and the Statue, -1 for blocking obstacles */
    float x, y, z, rot_y;
    float bmin[3], bmax[3];
} isl_object;

typedef struct {
    char template_name[48];
    int32_t stage, seed;
    int32_t nx, nz;
    float cell, ox, oz, radius;
    uint8_t water_heavy, vertical, snow, ancient;
    uint8_t allow_grass, allow_sand, allow_rock, allow_wood, allow_gold;
    float *height;           /* top surface height per cell (world units) */
    uint8_t *surf;           /* ISL_S_* of the top surface */
    uint8_t *normal_y;       /* surface normal y * 255 */
    int32_t n_objects;
    isl_object *objects;
} isl_island;

isl_island *isl_island_load(const char *path);
void isl_island_free(isl_island *isl);

/* ---------- game state ---------- */

typedef struct {
    uint8_t type, variant, rot, alive;
    int16_t px, pz;          /* pivot cell (buildings) */
    float x, y, z;           /* pivot / object position */
    float bmin[3], bmax[3];  /* world AABB of all colliders */
    int16_t score;
} isl_entity;

typedef struct {
    const isl_island *island;
    /* mutable terrain (plateaus raise ground and change its surface) */
    float height[ISL_MAX_GRID];
    uint8_t surf[ISL_MAX_GRID];
    uint8_t normal_y[ISL_MAX_GRID];
    int16_t occ[ISL_MAX_GRID];          /* -1 free, -2 obstacle, else index of the building on it */
    int32_t n_ent;
    isl_entity ent[ISL_MAX_ENT];
    /* inventory and the building deck */
    uint8_t inv[ISL_N_TYPES];
    uint8_t next_variant[ISL_N_TYPES];  /* model variant you get next time you place this type */
    int16_t deck[ISL_N_TYPES];
    uint8_t unlocked[ISL_N_TYPES]; int32_t n_unlocked;
    int16_t received[ISL_N_TYPES];
    uint8_t guaranteed[ISL_MAX_GUARANTEED]; int32_t n_guaranteed;
    uint64_t packs_remaining;           /* bit per ISL_PACKS index */
    /* progression */
    int32_t score, island_start_score;
    int32_t next_pack_at, last_pack_at, packs_k;
    uint8_t pending[ISL_MAX_PENDING]; int32_t n_pending;   /* sizes of packs waiting to be opened */
    int8_t offer[2]; int8_t n_offer;    /* -1 = not drawn yet */
    int32_t island_index, next_island_at;
    uint8_t island_unlocked, done;
    /* options */
    uint8_t reset_inventory_on_travel, reset_pack_counter_on_travel;
    uint64_t rng[4];
} isl_state;

enum { ISL_OK = 0, ISL_ERR_ILLEGAL = -1, ISL_ERR_NO_BUILDING = -2, ISL_ERR_NO_PACK = -3, ISL_ERR_LOCKED = -4, ISL_ERR_DONE = -5 };

/* setup */
void isl_reset(isl_state *s, const isl_island *isl, uint64_t seed);
static inline void isl_clone(isl_state *dst, const isl_state *src) { memcpy(dst, src, sizeof *dst); }

/* queries (variant < 0 means the variant the player would get next) */
int isl_legal(const isl_state *s, int type, int variant, int rot, int px, int pz);
int isl_score_at(const isl_state *s, int type, int variant, int rot, int px, int pz);
int isl_score_map(const isl_state *s, int type, int rot, int16_t *out);  /* ISL_ILLEGAL where illegal; returns #legal */

/* actions */
int isl_place(isl_state *s, int type, int rot, int px, int pz, int *reward);
int isl_offers(isl_state *s, int8_t out[2]);   /* draws the offer for the next waiting pack; returns count (0 = random pack) */
int isl_choose_pack(isl_state *s, int option);
int isl_travel(isl_state *s, const isl_island *next);
void isl_end(isl_state *s);

/* helpers */
int isl_pack_threshold(int k);
size_t isl_state_size(void);
void isl_info(const isl_state *s, int32_t *info);
void isl_inventory(const isl_state *s, uint8_t *inv, uint8_t *next_variant);
int isl_n_planes(void);
void isl_observe(const isl_state *s, float *planes);
const char *isl_type_name(int t);
const char *isl_pack_name(int p);
int isl_n_buildings(void);
int isl_n_types(void);
void isl_placement_world(const isl_state *s, int type, int variant, int rot, int px, int pz, float out[3]);
int isl_cell_index(const isl_state *s, int px, int pz);
void isl_world(const isl_state *s, int px, int pz, float *x, float *z);

#endif

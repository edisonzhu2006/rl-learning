/* ISLANDERS simulator core. Rules follow islanders/REAL_GAME_SPEC.md; tables come from rules_data.c. */
#include "islanders.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Tolerances for the grid approximation of the game's physics checks (world units). The game's probes
 * accept ground within +-0.025 of the building's base; a cell-sized grid needs a little slack. */
#define PROBE_TOL 0.03f
#define PENETRATION_TOL 0.03f

/* ---------- random numbers: xoshiro256** ---------- */

static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t rng_next(uint64_t *s) {
    uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45);
    return r;
}

static double rng_unit(uint64_t *s) { return (double)(rng_next(s) >> 11) * (1.0 / 9007199254740992.0); }
static int rng_int(uint64_t *s, int n) { return n <= 0 ? 0 : (int)(rng_unit(s) * n); }

static void rng_seed(uint64_t *s, uint64_t seed) {
    for (int i = 0; i < 4; i++) {
        uint64_t z = (seed += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        s[i] = z ^ (z >> 31);
    }
}

/* ---------- island files ---------- */

isl_island *isl_island_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    isl_island *isl = calloc(1, sizeof *isl);
    char magic[4];
    uint8_t flags[9];
    int ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "ISL1", 4) == 0;
    ok = ok && fread(&isl->nx, 4, 1, f) && fread(&isl->nz, 4, 1, f) && fread(&isl->stage, 4, 1, f) && fread(&isl->seed, 4, 1, f);
    ok = ok && fread(&isl->cell, 4, 1, f) && fread(&isl->ox, 4, 1, f) && fread(&isl->oz, 4, 1, f) && fread(&isl->radius, 4, 1, f);
    ok = ok && fread(flags, 1, 9, f) == 9 && fread(isl->template_name, 1, 48, f) == 48;
    ok = ok && isl->nx > 0 && isl->nz > 0 && isl->nx <= ISL_MAX_NX && isl->nz <= ISL_MAX_NX;
    if (ok) {
        int n = isl->nx * isl->nz;
        isl->height = malloc(sizeof(float) * n);
        isl->surf = malloc(n);
        isl->normal_y = malloc(n);
        ok = fread(isl->height, sizeof(float), n, f) == (size_t)n && fread(isl->surf, 1, n, f) == (size_t)n &&
             fread(isl->normal_y, 1, n, f) == (size_t)n && fread(&isl->n_objects, 4, 1, f) == 1;
    }
    if (ok && isl->n_objects > 0) {
        isl->objects = calloc(isl->n_objects, sizeof(isl_object));
        for (int i = 0; ok && i < isl->n_objects; i++) {
            int16_t t[2];
            float v[10];
            ok = fread(t, 2, 2, f) == 2 && fread(v, 4, 10, f) == 10;
            isl_object *o = &isl->objects[i];
            o->type = t[0];
            o->x = v[0]; o->y = v[1]; o->z = v[2]; o->rot_y = v[3];
            for (int k = 0; k < 3; k++) { o->bmin[k] = v[4 + k]; o->bmax[k] = v[7 + k]; }
        }
    }
    fclose(f);
    if (!ok) { isl_island_free(isl); return NULL; }
    isl->water_heavy = flags[0]; isl->vertical = flags[1]; isl->snow = flags[2]; isl->ancient = flags[3];
    isl->allow_grass = flags[4]; isl->allow_sand = flags[5]; isl->allow_rock = flags[6];
    isl->allow_wood = flags[7]; isl->allow_gold = flags[8];
    return isl;
}

void isl_island_free(isl_island *isl) {
    if (!isl) return;
    free(isl->height); free(isl->surf); free(isl->normal_y); free(isl->objects); free(isl);
}

/* ---------- geometry ---------- */

int isl_cell_index(const isl_state *s, int px, int pz) {
    const isl_island *I = s->island;
    if (px < 0 || pz < 0 || px >= I->nx || pz >= I->nz) return -1;
    return pz * I->nx + px;
}

void isl_world(const isl_state *s, int px, int pz, float *x, float *z) {
    *x = s->island->ox + (px + 0.5f) * s->island->cell;
    *z = s->island->oz + (pz + 0.5f) * s->island->cell;
}

/* Quarter-turn rotation about +y in Unity's convention: local (x, z) -> world (z, -x) per turn. */
static inline void rot_i(int rot, int dx, int dz, int *ox, int *oz) {
    for (int k = 0; k < (rot & 3); k++) { int t = dx; dx = dz; dz = -t; }
    *ox = dx; *oz = dz;
}

static inline void rot_f(int rot, float x, float z, float *ox, float *oz) {
    for (int k = 0; k < (rot & 3); k++) { float t = x; x = z; z = -t; }
    *ox = x; *oz = z;
}

static const isl_variant *variant_of(int type, int variant) {
    const isl_def *d = &ISL_DEFS[type];
    if (d->n_variants == 0) return NULL;
    if (variant < 0 || variant >= d->n_variants) variant = 0;
    return &d->variants[variant];
}

static void model_position(const isl_state *s, const isl_variant *v, int rot, int px, int pz, float p[3]) {
    float cx, cz, dx, dz;
    isl_world(s, px, pz, &cx, &cz);
    rot_f(rot, v->offset[0], v->offset[1], &dx, &dz);
    int i = isl_cell_index(s, px, pz);
    p[0] = cx + dx; p[1] = i >= 0 ? s->height[i] : 0.f; p[2] = cz + dz;
}

static void box_world(const float *local, int rot, const float p[3], float out_min[3], float out_max[3]) {
    float ax, az, bx, bz;
    rot_f(rot, local[0], local[2], &ax, &az);
    rot_f(rot, local[3], local[5], &bx, &bz);
    out_min[0] = p[0] + fminf(ax, bx); out_max[0] = p[0] + fmaxf(ax, bx);
    out_min[2] = p[2] + fminf(az, bz); out_max[2] = p[2] + fmaxf(az, bz);
    out_min[1] = p[1] + local[1];     out_max[1] = p[1] + local[4];
}

static inline float dist2_point_box(const float q[3], const float bmin[3], const float bmax[3]) {
    float d = 0.f;
    for (int k = 0; k < 3; k++) {
        float v = q[k] < bmin[k] ? bmin[k] - q[k] : (q[k] > bmax[k] ? q[k] - bmax[k] : 0.f);
        d += v * v;
    }
    return d;
}

static inline int boxes_overlap_xz(const float amin[3], const float amax[3], const float bmin[3], const float bmax[3]) {
    return amin[0] <= bmax[0] && bmin[0] <= amax[0] && amin[2] <= bmax[2] && bmin[2] <= amax[2];
}

static inline int is_nature(int t) { return t == ISL_T_TREE || t == ISL_T_MIGHTY_OAK || t == ISL_T_FLOWER; }

/* Would a building with these boxes delete this nature object? (Placement deletes Tree and Vegetation.) */
static int covers(const isl_variant *v, int rot, const float p[3], const isl_entity *e) {
    for (int b = 0; b < v->n_boxes; b++) {
        float mn[3], mx[3];
        box_world(v->boxes[b], rot, p, mn, mx);
        if (boxes_overlap_xz(mn, mx, e->bmin, e->bmax)) return 1;
    }
    return 0;
}

/* Is any collider of entity e within the sphere (q, r)? */
static int entity_in_range(const isl_entity *e, const float q[3], float r2) {
    if (dist2_point_box(q, e->bmin, e->bmax) > r2) return 0;
    if (e->type >= ISL_N_BUILDINGS) return 1;
    const isl_variant *v = variant_of(e->type, e->variant);
    float p[3] = { e->x, e->y, e->z };
    for (int b = 0; b < v->n_boxes; b++) {
        float mn[3], mx[3];
        box_world(v->boxes[b], e->rot, p, mn, mx);
        if (dist2_point_box(q, mn, mx) <= r2) return 1;
    }
    return v->n_boxes == 0;
}

/* ---------- legality and score ---------- */

int isl_legal(const isl_state *s, int type, int variant, int rot, int px, int pz) {
    if (type < 0 || type >= ISL_N_BUILDINGS) return 0;
    if (variant < 0) variant = s->next_variant[type];
    const isl_def *d = &ISL_DEFS[type];
    const isl_variant *v = variant_of(type, variant);
    int pi = isl_cell_index(s, px, pz);
    if (!v || pi < 0 || s->surf[pi] == ISL_S_NONE) return 0;
    float base = s->height[pi];
    for (int c = 0; c < v->n_cells; c++) {
        int dx, dz;
        rot_i(rot, v->cells[c][0], v->cells[c][1], &dx, &dz);
        int i = isl_cell_index(s, px + dx, pz + dz);
        if (i < 0 || s->occ[i] != -1) return 0;
        if (!d->clip_ok && s->surf[i] != ISL_S_NONE && s->height[i] > base + PENETRATION_TOL) return 0;
    }
    if (d->on_wall) {
        /* Wall Plateau attaches to a cliff face: the pivot has to be on steep ground. */
        if (s->normal_y[pi] >= 242) return 0;
    }
    int skip_probes = d->steep_ok && s->normal_y[pi] > 76 && s->normal_y[pi] < 242;   /* 0.3 < n.y < 0.95 */
    if (!skip_probes) {
        for (int k = 0; k < v->n_probes; k++) {
            int dx, dz;
            rot_i(rot, v->probes[k][0], v->probes[k][1], &dx, &dz);
            int i = isl_cell_index(s, px + dx, pz + dz);
            if (i < 0) return 0;
            int sf = s->surf[i];
            if (sf == ISL_S_NONE || !(d->surf_mask & (1u << sf)) || (d->breaker_mask & (1u << sf))) return 0;
            if (fabsf(s->height[i] - base) > PROBE_TOL) return 0;
        }
    }
    return 1;
}

int isl_score_at(const isl_state *s, int type, int variant, int rot, int px, int pz) {
    if (type < 0 || type >= ISL_N_BUILDINGS) return 0;
    if (variant < 0) variant = s->next_variant[type];
    const isl_def *d = &ISL_DEFS[type];
    const isl_variant *v = variant_of(type, variant);
    float p[3];
    model_position(s, v, rot, px, pz, p);
    float q[ISL_MAX_RANGE_PTS][3];
    for (int k = 0; k < d->n_range_pts; k++) {
        float dx, dz;
        rot_f(rot, d->range_pts[k][0], d->range_pts[k][2], &dx, &dz);
        q[k][0] = p[0] + dx; q[k][1] = p[1] + d->range_pts[k][1]; q[k][2] = p[2] + dz;
    }
    float r2 = d->range * d->range;
    int score = d->base + (s->island->snow ? d->snow_bonus : 0);
    const int16_t *W = ISL_W[type];
    for (int e = 0; e < s->n_ent; e++) {
        const isl_entity *en = &s->ent[e];
        if (!en->alive || en->type >= ISL_N_TYPES || W[en->type] == 0) continue;
        if (is_nature(en->type) && covers(v, rot, p, en)) continue;   /* deleted by this placement */
        for (int k = 0; k < d->n_range_pts; k++)
            if (entity_in_range(en, q[k], r2)) { score += W[en->type]; break; }
    }
    return score;
}

int isl_score_map(const isl_state *s, int type, int rot, int16_t *out) {
    const isl_island *I = s->island;
    int n = 0;
    for (int pz = 0; pz < I->nz; pz++)
        for (int px = 0; px < I->nx; px++) {
            int i = pz * I->nx + px;
            if (isl_legal(s, type, -1, rot, px, pz)) { out[i] = (int16_t)isl_score_at(s, type, -1, rot, px, pz); n++; }
            else out[i] = ISL_ILLEGAL;
        }
    return n;
}

/* ---------- entities ---------- */

static int add_entity(isl_state *s, int type, int variant, int rot, int px, int pz, const float p[3]) {
    if (s->n_ent >= ISL_MAX_ENT) return -1;
    isl_entity *e = &s->ent[s->n_ent];
    memset(e, 0, sizeof *e);
    e->type = (uint8_t)type; e->variant = (uint8_t)variant; e->rot = (uint8_t)rot; e->alive = 1;
    e->px = (int16_t)px; e->pz = (int16_t)pz; e->x = p[0]; e->y = p[1]; e->z = p[2];
    return s->n_ent++;
}

static void building_bounds(isl_entity *e) {
    const isl_variant *v = variant_of(e->type, e->variant);
    float p[3] = { e->x, e->y, e->z };
    for (int k = 0; k < 3; k++) { e->bmin[k] = p[k]; e->bmax[k] = p[k]; }
    for (int b = 0; b < v->n_boxes; b++) {
        float mn[3], mx[3];
        box_world(v->boxes[b], e->rot, p, mn, mx);
        for (int k = 0; k < 3; k++) { e->bmin[k] = fminf(e->bmin[k], mn[k]); e->bmax[k] = fmaxf(e->bmax[k], mx[k]); }
    }
}

static void occupy(isl_state *s, int type, int variant, int rot, int px, int pz, int16_t who) {
    const isl_variant *v = variant_of(type, variant);
    for (int c = 0; c < v->n_cells; c++) {
        int dx, dz;
        rot_i(rot, v->cells[c][0], v->cells[c][1], &dx, &dz);
        int i = isl_cell_index(s, px + dx, pz + dz);
        if (i >= 0) s->occ[i] = who;
    }
}

static void block_box(isl_state *s, const float bmin[3], const float bmax[3]) {
    const isl_island *I = s->island;
    int x0 = (int)floorf((bmin[0] - I->ox) / I->cell), x1 = (int)floorf((bmax[0] - I->ox) / I->cell);
    int z0 = (int)floorf((bmin[2] - I->oz) / I->cell), z1 = (int)floorf((bmax[2] - I->oz) / I->cell);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            int i = isl_cell_index(s, x, z);
            if (i >= 0) s->occ[i] = -2;
        }
}

static void load_island(isl_state *s, const isl_island *isl) {
    s->island = isl;
    int n = isl->nx * isl->nz;
    memcpy(s->height, isl->height, sizeof(float) * n);
    memcpy(s->surf, isl->surf, n);
    memcpy(s->normal_y, isl->normal_y, n);
    for (int i = 0; i < n; i++) s->occ[i] = -1;
    s->n_ent = 0;
    for (int k = 0; k < isl->n_objects; k++) {
        const isl_object *o = &isl->objects[k];
        if (o->type == ISL_T_STATUE) {
            const isl_variant *v = variant_of(ISL_T_STATUE, 0);
            int rot = ((int)lroundf(o->rot_y / 90.f)) & 3;
            float dx, dz;
            rot_f(rot, v->offset[0], v->offset[1], &dx, &dz);
            int px = (int)floorf((o->x - dx - isl->ox) / isl->cell), pz = (int)floorf((o->z - dz - isl->oz) / isl->cell);
            float p[3] = { o->x, o->y, o->z };
            int e = add_entity(s, ISL_T_STATUE, 0, rot, px, pz, p);
            if (e >= 0) { building_bounds(&s->ent[e]); occupy(s, ISL_T_STATUE, 0, rot, px, pz, (int16_t)e); }
        } else if (o->type >= ISL_N_BUILDINGS && o->type < ISL_N_TYPES) {
            float p[3] = { o->x, o->y, o->z };
            int e = add_entity(s, o->type, 0, 0, -1, -1, p);
            if (e >= 0) {
                memcpy(s->ent[e].bmin, o->bmin, sizeof o->bmin);
                memcpy(s->ent[e].bmax, o->bmax, sizeof o->bmax);
                if (o->type == ISL_T_GOLD_VEIN) block_box(s, o->bmin, o->bmax);   /* veins are solid */
            }
        } else if (o->type < 0) {
            block_box(s, o->bmin, o->bmax);
        }
    }
}

/* ---------- packs and the deck ---------- */

static int pack_size(int k) { return k == 0 ? 3 : 6; }

static void check_packs(isl_state *s) {
    for (int guard = 0; guard < 100 && s->score >= s->next_pack_at && s->n_pending < ISL_MAX_PENDING; guard++) {
        s->pending[s->n_pending++] = (uint8_t)pack_size(s->packs_k);
        s->packs_k++;
        s->last_pack_at = s->next_pack_at;
        int k = s->packs_k;
        s->next_pack_at += ISL_PACK_INC[k < ISL_N_PACK_INC ? k : ISL_N_PACK_INC - 1];
        s->next_pack_at += ISL_INC_AFTER_LIST * (k - ISL_N_PACK_INC > 0 ? k - ISL_N_PACK_INC : 0);
        s->next_pack_at = (s->next_pack_at / 5) * 5;
    }
}

static void check_island(isl_state *s) {
    if (!s->island_unlocked && s->score >= s->next_island_at) s->island_unlocked = 1;
}

static void check_done(isl_state *s) {
    if (s->n_pending > 0 || s->island_unlocked) { s->done = 0; return; }
    for (int t = 0; t < ISL_N_BUILDINGS; t++) if (s->inv[t]) { s->done = 0; return; }
    s->done = 1;
}

static int can_unlock(const isl_state *s, int p, int tol) {
    const isl_pack *P = &ISL_PACKS[p];
    const isl_island *I = s->island;
    if ((P->need_gold && !I->allow_gold) || (P->need_grass && !I->allow_grass) || (P->need_rock && !I->allow_rock) ||
        (P->need_sand && !I->allow_sand) || (P->need_no_sand && I->allow_sand) || (P->need_wood && !I->allow_wood))
        return 0;
    if (P->season == ISL_SEASON_WINTER && !I->snow) return 0;
    if (P->season == ISL_SEASON_SUMMER && I->snow) return 0;
    int jokers = P->jokers + tol;
    for (int r = 0; r < P->n_req; r++)
        if (!s->unlocked[P->req[r]] && --jokers < 0) return 0;
    return s->n_unlocked + tol >= P->min_unlocks;
}

static int remaining(const isl_state *s, int p) { return (int)((s->packs_remaining >> p) & 1u); }

static int unlockable(const isl_state *s, int out[ISL_N_PACKS]) {
    const isl_island *I = s->island;
    if (I->water_heavy && remaining(s, ISL_PACK_WATER_PLATEAU)) { out[0] = ISL_PACK_WATER_PLATEAU; return 1; }
    if (I->vertical && remaining(s, ISL_PACK_WALL_PLATEAU)) { out[0] = ISL_PACK_WALL_PLATEAU; return 1; }
    if (I->ancient && remaining(s, ISL_PACK_STATUE)) { out[0] = ISL_PACK_STATUE; return 1; }
    for (int tol = 0; tol <= 10; tol++) {
        int n = 0;
        for (int p = 0; p < ISL_N_PACKS; p++)
            if (remaining(s, p) && can_unlock(s, p, tol)) out[n++] = p;
        if (n > 0) return n;
    }
    return 0;
}

int isl_offers(isl_state *s, int8_t out[2]) {
    if (s->n_pending == 0) return ISL_ERR_NO_PACK;
    if (s->n_offer < 0) {
        int list[ISL_N_PACKS];
        int n = unlockable(s, list);
        s->n_offer = 0;
        for (int i = n - 1; i >= 0; i--)
            if (ISL_PACKS[list[i]].always_show && s->n_offer < 2) { s->offer[s->n_offer++] = (int8_t)list[i]; list[i] = list[--n]; }
        while (s->n_offer < 2 && n > 0) {
            int j = rng_int(s->rng, n);
            s->offer[s->n_offer++] = (int8_t)list[j];
            list[j] = list[--n];
        }
    }
    if (out) { out[0] = s->n_offer > 0 ? s->offer[0] : -1; out[1] = s->n_offer > 1 ? s->offer[1] : -1; }
    return s->n_offer;
}

static void push_guaranteed(isl_state *s, int t) {
    if (s->n_guaranteed < ISL_MAX_GUARANTEED) s->guaranteed[s->n_guaranteed++] = (uint8_t)t;
}

static void unlock_pack(isl_state *s, int p) {
    s->packs_remaining &= ~(1ull << p);
    const isl_pack *P = &ISL_PACKS[p];
    for (int k = 0; k < P->n_b; k++) {
        int t = P->b[k];
        push_guaranteed(s, t);
        if (t == ISL_T_WALL_PLATEAU) { push_guaranteed(s, t); push_guaranteed(s, t); }
        if (s->unlocked[t]) continue;
        s->unlocked[t] = 1; s->n_unlocked++;
        if (t != ISL_T_WALL_PLATEAU) s->deck[t] += ISL_DEFS[t].deck + (s->island->water_heavy ? ISL_DEFS[t].deck_water : 0);
    }
}

static void per_unlock_extras(isl_state *s) {
    int list[ISL_N_PACKS];
    if (unlockable(s, list) == 0) return;
    if (!s->island->water_heavy) return;   /* only the Water Plateau has a per-unlock rate, on water-heavy islands */
    for (int t = 0; t < ISL_N_BUILDINGS; t++) {
        float f = ISL_DEFS[t].per_unlock_water;
        if (!s->unlocked[t] || f <= 0.f) continue;
        int n = (int)floorf(f) + (rng_unit(s->rng) < f - floorf(f) ? 1 : 0);
        while (n--) push_guaranteed(s, t);
    }
}

static int gate_ok(const isl_state *s, int t) {
    if (s->island->snow) return 1;
    int g = ISL_DEFS[t].gate_type;
    float need = ISL_DEFS[t].gate_amount * (1.f + s->received[t]);
    return (g < 0 ? 0.f : (float)s->received[g]) >= need;
}

static void draw_building(isl_state *s) {
    int t = -1;
    if (s->n_guaranteed > 0) {
        t = s->guaranteed[0];
        memmove(s->guaranteed, s->guaranteed + 1, --s->n_guaranteed);
    } else {
        int total = 0;
        for (int b = 0; b < ISL_N_BUILDINGS; b++) total += s->deck[b];
        if (total == 0) {
            for (int b = 0; b < ISL_N_BUILDINGS; b++)
                if (s->unlocked[b] && ISL_DEFS[b].re_add) s->deck[b] += ISL_DEFS[b].deck + (s->island->water_heavy ? ISL_DEFS[b].deck_water : 0);
            for (int b = 0; b < ISL_N_BUILDINGS; b++) total += s->deck[b];
        }
        if (total == 0) return;
        for (int tries = 0; tries < 100; tries++) {
            int r = rng_int(s->rng, total);
            for (t = 0; t < ISL_N_BUILDINGS; t++) { r -= s->deck[t]; if (r < 0) break; }
            if (gate_ok(s, t)) break;
        }
        if (s->deck[t] > 0) s->deck[t]--;
    }
    s->received[t]++;
    if (s->inv[t] < 255) s->inv[t]++;
}

int isl_choose_pack(isl_state *s, int option) {
    if (s->done) return ISL_ERR_DONE;
    if (s->n_pending == 0) return ISL_ERR_NO_PACK;
    isl_offers(s, NULL);
    if (s->n_offer > 0) {
        if (option < 0 || option >= s->n_offer) return ISL_ERR_ILLEGAL;
        unlock_pack(s, s->offer[option]);
    }
    per_unlock_extras(s);
    int size = s->pending[0];
    memmove(s->pending, s->pending + 1, --s->n_pending);
    for (int k = 0; k < size; k++) draw_building(s);
    s->n_offer = -1;
    check_done(s);
    return ISL_OK;
}

/* ---------- placing ---------- */

int isl_place(isl_state *s, int type, int rot, int px, int pz, int *reward) {
    if (s->done) return ISL_ERR_DONE;
    if (type < 0 || type >= ISL_N_BUILDINGS || s->inv[type] == 0) return ISL_ERR_NO_BUILDING;
    rot &= 3;
    int variant = s->next_variant[type];
    if (!isl_legal(s, type, variant, rot, px, pz)) return ISL_ERR_ILLEGAL;
    int gain = isl_score_at(s, type, variant, rot, px, pz);
    const isl_def *d = &ISL_DEFS[type];
    const isl_variant *v = variant_of(type, variant);
    float p[3];
    model_position(s, v, rot, px, pz, p);
    for (int e = 0; e < s->n_ent; e++)
        if (s->ent[e].alive && is_nature(s->ent[e].type) && covers(v, rot, p, &s->ent[e])) s->ent[e].alive = 0;
    int e = add_entity(s, type, variant, rot, px, pz, p);
    if (e < 0) return ISL_ERR_ILLEGAL;
    s->ent[e].score = (int16_t)gain;
    building_bounds(&s->ent[e]);
    if (d->is_plateau) {
        float top = p[1] + d->plateau_top;
        for (int c = 0; c < v->n_cells; c++) {
            int dx, dz;
            rot_i(rot, v->cells[c][0], v->cells[c][1], &dx, &dz);
            int i = isl_cell_index(s, px + dx, pz + dz);
            if (i >= 0) { s->height[i] = top; s->surf[i] = ISL_S_PLATEAU; s->normal_y[i] = 255; }
        }
    } else {
        occupy(s, type, variant, rot, px, pz, (int16_t)e);
    }
    s->inv[type]--;
    s->next_variant[type] = (uint8_t)rng_int(s->rng, d->n_variants > 0 ? d->n_variants : 1);
    s->score += gain;
    check_packs(s);
    check_island(s);
    check_done(s);
    if (reward) *reward = gain;
    return ISL_OK;
}

/* ---------- islands ---------- */

static void new_island_state(isl_state *s, const isl_island *isl) {
    load_island(s, isl);
    memset(s->deck, 0, sizeof s->deck);
    memset(s->unlocked, 0, sizeof s->unlocked);
    memset(s->received, 0, sizeof s->received);
    s->n_unlocked = 0; s->n_guaranteed = 0;
    s->packs_remaining = (ISL_N_PACKS >= 64) ? ~0ull : ((1ull << ISL_N_PACKS) - 1);
    s->n_offer = -1;
    for (int t = 0; t < ISL_N_BUILDINGS; t++)
        s->next_variant[t] = (uint8_t)rng_int(s->rng, ISL_DEFS[t].n_variants > 0 ? ISL_DEFS[t].n_variants : 1);
}

void isl_reset(isl_state *s, const isl_island *isl, uint64_t seed) {
    memset(s, 0, sizeof *s);
    rng_seed(s->rng, seed);
    s->reset_inventory_on_travel = 1;
    s->reset_pack_counter_on_travel = 1;
    new_island_state(s, isl);
    s->score = 0; s->island_start_score = 0;
    s->next_pack_at = 0; s->last_pack_at = 0; s->packs_k = 0;
    s->island_index = 0;
    s->next_island_at = ISL_ISLAND_GOALS[0];
    check_packs(s);
    check_done(s);
}

int isl_travel(isl_state *s, const isl_island *next) {
    if (s->done) return ISL_ERR_DONE;
    if (!s->island_unlocked || !next) return ISL_ERR_LOCKED;
    s->score = ((s->score + ISL_TRAVEL_BONUS + 9) / 10) * 10;
    new_island_state(s, next);
    if (s->reset_inventory_on_travel) memset(s->inv, 0, sizeof s->inv);
    if (s->reset_pack_counter_on_travel) s->packs_k = 0;
    s->n_pending = 0;
    s->next_pack_at = s->score; s->last_pack_at = s->score;
    s->island_index++;
    s->island_start_score = s->score;
    int g = s->island_index < ISL_N_ISLAND_GOALS ? s->island_index : ISL_N_ISLAND_GOALS - 1;
    s->next_island_at = s->score + ISL_ISLAND_GOALS[g];
    s->island_unlocked = 0;
    check_packs(s);
    check_done(s);
    return ISL_OK;
}

void isl_end(isl_state *s) { s->done = 1; }

/* Score at which the k-th pack of an island becomes available (k = 0 is the free first pack). */
int isl_pack_threshold(int k) {
    int next = 0;
    for (int j = 0; j < k; j++) {
        int n = j + 1;
        next += ISL_PACK_INC[n < ISL_N_PACK_INC ? n : ISL_N_PACK_INC - 1];
        next += ISL_INC_AFTER_LIST * (n - ISL_N_PACK_INC > 0 ? n - ISL_N_PACK_INC : 0);
        next = (next / 5) * 5;
    }
    return next;
}

/* ---------- accessors for bindings (Python ctypes) ---------- */

size_t isl_state_size(void) { return sizeof(isl_state); }

/* info[16]: score, next_pack_at, last_pack_at, n_pending, n_offer, offer0, offer1, island_unlocked, done,
 *           island_index, next_island_at, nx, nz, n_entities, packs_k, island_start_score */
void isl_info(const isl_state *s, int32_t *info) {
    info[0] = s->score; info[1] = s->next_pack_at; info[2] = s->last_pack_at; info[3] = s->n_pending;
    info[4] = s->n_offer; info[5] = s->n_offer > 0 ? s->offer[0] : -1; info[6] = s->n_offer > 1 ? s->offer[1] : -1;
    info[7] = s->island_unlocked; info[8] = s->done; info[9] = s->island_index; info[10] = s->next_island_at;
    info[11] = s->island->nx; info[12] = s->island->nz; info[13] = s->n_ent; info[14] = s->packs_k; info[15] = s->island_start_score;
}

void isl_inventory(const isl_state *s, uint8_t *inv, uint8_t *next_variant) {
    memcpy(inv, s->inv, ISL_N_BUILDINGS);
    if (next_variant) memcpy(next_variant, s->next_variant, ISL_N_BUILDINGS);
}

/* Observation planes, each nx*nz floats, in this order:
 *   0 height, 1 normal_y, 2..6 surface one-hot (water, sand, grass, rock, plateau top), 7 blocked (obstacle or building),
 *   8.. one plane per type (ISL_N_TYPES planes): 1 where a building's footprint or a nature object sits. */
int isl_n_planes(void) { return 8 + ISL_N_TYPES; }

void isl_observe(const isl_state *s, float *planes) {
    const isl_island *I = s->island;
    int n = I->nx * I->nz;
    memset(planes, 0, sizeof(float) * n * isl_n_planes());
    for (int i = 0; i < n; i++) {
        planes[i] = s->height[i];
        planes[n + i] = s->normal_y[i] / 255.f;
        if (s->surf[i] >= ISL_S_WATER && s->surf[i] <= ISL_S_PLATEAU) planes[(1 + s->surf[i]) * n + i] = 1.f;
        planes[7 * n + i] = s->occ[i] != -1;
    }
    for (int e = 0; e < s->n_ent; e++) {
        const isl_entity *en = &s->ent[e];
        if (!en->alive || en->type >= ISL_N_TYPES) continue;
        float *pl = planes + (8 + en->type) * n;
        if (en->type < ISL_N_BUILDINGS) {
            const isl_variant *v = variant_of(en->type, en->variant);
            for (int c = 0; c < v->n_cells; c++) {
                int dx, dz;
                rot_i(en->rot, v->cells[c][0], v->cells[c][1], &dx, &dz);
                int i = isl_cell_index(s, en->px + dx, en->pz + dz);
                if (i >= 0) pl[i] = 1.f;
            }
        } else {
            int px = (int)floorf((en->x - I->ox) / I->cell), pz = (int)floorf((en->z - I->oz) / I->cell);
            int i = isl_cell_index(s, px, pz);
            if (i >= 0) pl[i] = 1.f;
        }
    }
}

const char *isl_type_name(int t) { return (t >= 0 && t < ISL_N_TYPES) ? ISL_DEFS[t].name : ""; }
const char *isl_pack_name(int p) { return (p >= 0 && p < ISL_N_PACKS) ? ISL_PACKS[p].name : ""; }
int isl_n_buildings(void) { return ISL_N_BUILDINGS; }
int isl_n_types(void) { return ISL_N_TYPES; }

/* World position of a placement (the game's building pivot), for comparing against the real game. */
void isl_placement_world(const isl_state *s, int type, int variant, int rot, int px, int pz, float out[3]) {
    if (variant < 0) variant = s->next_variant[type];
    model_position(s, variant_of(type, variant), rot & 3, px, pz, out);
}

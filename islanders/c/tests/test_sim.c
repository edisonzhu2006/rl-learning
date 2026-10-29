/* Unit tests for the simulator, on a synthetic flat island so the expected numbers are easy to check. */
#include "islanders.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* 60 x 60 cells of flat grass at height 0.1, with a strip of water along z = 0..4. */
static isl_island *flat_island(void) {
    isl_island *I = calloc(1, sizeof *I);
    strcpy(I->template_name, "test flat");
    I->nx = I->nz = 60; I->cell = ISL_CELL; I->ox = -30 * ISL_CELL; I->oz = -30 * ISL_CELL; I->radius = 5;
    I->allow_grass = I->allow_sand = I->allow_rock = I->allow_wood = I->allow_gold = 1;
    int n = I->nx * I->nz;
    I->height = malloc(sizeof(float) * n); I->surf = malloc(n); I->normal_y = malloc(n);
    for (int z = 0; z < I->nz; z++)
        for (int x = 0; x < I->nx; x++) {
            int i = z * I->nx + x;
            int water = z < 5;
            I->height[i] = water ? 0.015f : 0.1f;
            I->surf[i] = water ? ISL_S_WATER : ISL_S_GRASS;
            I->normal_y[i] = 255;
        }
    return I;
}

static void give(isl_state *s, int type, int n) { s->inv[type] = (uint8_t)n; s->done = 0; }

static void test_thresholds(void) {
    int want[] = { 0, 20, 70, 125, 185, 250, 320, 395, 475, 560 };
    for (int k = 0; k < 10; k++) CHECK(isl_pack_threshold(k) == want[k], "pack %d at %d, want %d", k, isl_pack_threshold(k), want[k]);
}

static void test_start(const isl_island *I) {
    isl_state *s = malloc(sizeof *s);
    isl_reset(s, I, 1);
    CHECK(s->n_pending == 1 && s->pending[0] == 3, "start: %d packs waiting, first of size %d (want 1 of 3)", s->n_pending, s->pending[0]);
    CHECK(s->next_pack_at == 20, "start: next pack at %d (want 20)", s->next_pack_at);
    CHECK(s->next_island_at == 200, "start: next island at %d (want 200)", s->next_island_at);
    int8_t off[2];
    int n = isl_offers(s, off);
    CHECK(n >= 1 && n <= 2, "start: %d offers", n);
    CHECK(isl_choose_pack(s, 0) == ISL_OK, "choose pack failed");
    int total = 0;
    for (int t = 0; t < ISL_N_BUILDINGS; t++) total += s->inv[t];
    CHECK(total == 3, "first pack gave %d buildings (want 3)", total);
    printf("  first offer: %s | %s -> got %d buildings\n", ISL_PACKS[off[0]].name, n > 1 ? ISL_PACKS[off[1]].name : "-", total);
    free(s);
}

static void test_order_matters(const isl_island *I) {
    /* Lumberjack then Sawmill: 0 + (0 + 7). Sawmill then Lumberjack: 0 + (0 + 5). */
    isl_state *s = malloc(sizeof *s);
    int r1, r2;
    isl_reset(s, I, 2);
    s->n_pending = 0; give(s, ISL_T_LUMBERJACK, 1); give(s, ISL_T_SAWMILL, 1);
    CHECK(isl_place(s, ISL_T_LUMBERJACK, 0, 30, 30, &r1) == ISL_OK, "place lumberjack");
    CHECK(isl_place(s, ISL_T_SAWMILL, 0, 34, 30, &r2) == ISL_OK, "place sawmill");
    CHECK(r1 == 0 && r2 == 7, "lumberjack first: %d + %d (want 0 + 7)", r1, r2);
    isl_reset(s, I, 2);
    s->n_pending = 0; give(s, ISL_T_LUMBERJACK, 1); give(s, ISL_T_SAWMILL, 1);
    CHECK(isl_place(s, ISL_T_SAWMILL, 0, 34, 30, &r1) == ISL_OK, "place sawmill");
    CHECK(isl_place(s, ISL_T_LUMBERJACK, 0, 30, 30, &r2) == ISL_OK, "place lumberjack");
    CHECK(r1 == 0 && r2 == 5, "sawmill first: %d + %d (want 0 + 5)", r1, r2);
    free(s);
}

static void test_range_is_3d_and_by_collider(const isl_island *I) {
    /* Mansion range 0.525: a City Center whose collider edge is within 0.525 counts, one further away doesn't. */
    isl_state *s = malloc(sizeof *s);
    int r;
    isl_reset(s, I, 3);
    s->n_pending = 0; give(s, ISL_T_CITY_CENTER, 1); give(s, ISL_T_MANSION, 2);
    CHECK(isl_place(s, ISL_T_CITY_CENTER, 0, 30, 30, &r) == ISL_OK && r == 15, "city center scores base 15, got %d", r);
    s->next_variant[ISL_T_MANSION] = 1;
    CHECK(isl_place(s, ISL_T_MANSION, 0, 30, 33, &r) == ISL_OK, "mansion near");
    CHECK(r == 1 + 8, "mansion 3 cells from the city center: %d (want 9)", r);
    s->next_variant[ISL_T_MANSION] = 1;
    CHECK(isl_place(s, ISL_T_MANSION, 0, 30, 46, &r) == ISL_OK, "mansion far");
    CHECK(r == 1, "mansion 16 cells away: %d (want base 1)", r);
    free(s);
}

static void test_legality(const isl_island *I) {
    isl_state *s = malloc(sizeof *s);
    int r;
    isl_reset(s, I, 4);
    s->n_pending = 0; give(s, ISL_T_HOUSE, 2); give(s, ISL_T_FISHER, 1);
    s->next_variant[ISL_T_HOUSE] = 0;
    CHECK(isl_place(s, ISL_T_HOUSE, 0, 30, 30, &r) == ISL_OK, "house on grass");
    s->next_variant[ISL_T_HOUSE] = 0;
    CHECK(isl_place(s, ISL_T_HOUSE, 0, 30, 30, &r) == ISL_ERR_ILLEGAL, "second house on the same spot must be illegal");
    CHECK(!isl_legal(s, ISL_T_HOUSE, 0, 0, 30, 2), "house on water must be illegal");
    CHECK(isl_legal(s, ISL_T_FISHER, 0, 0, 30, 2), "fisher on water must be legal");
    CHECK(!isl_legal(s, ISL_T_FISHER, 0, 0, 30, 20), "fisher on grass must be illegal");
    CHECK(!isl_legal(s, ISL_T_MASON, 0, 0, 30, 20), "mason on grass must be illegal (needs rock)");
    free(s);
}

static void test_clone_is_deterministic(const isl_island *I) {
    isl_state *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    isl_reset(a, I, 5);
    isl_clone(b, a);
    isl_choose_pack(a, 0); isl_choose_pack(b, 0);
    CHECK(memcmp(a->inv, b->inv, sizeof a->inv) == 0, "clones diverged after the same action");
    free(a); free(b);
}

static void test_real_island(const char *path) {
    isl_island *I = isl_island_load(path);
    CHECK(I != NULL, "could not load %s", path);
    if (!I) return;
    isl_state *s = malloc(sizeof *s);
    isl_reset(s, I, 6);
    int land = 0, statues = 0, trees = 0;
    for (int i = 0; i < I->nx * I->nz; i++) land += I->surf[i] > ISL_S_WATER;
    for (int e = 0; e < s->n_ent; e++) { statues += s->ent[e].type == ISL_T_STATUE; trees += s->ent[e].type == ISL_T_TREE; }
    printf("  %s: %dx%d grid, %d land cells, %d statue(s), %d trees\n", I->template_name, I->nx, I->nz, land, statues, trees);
    CHECK(land > 100, "real island has only %d land cells", land);
    free(s);
    isl_island_free(I);
}

int main(int argc, char **argv) {
    isl_island *I = flat_island();
    printf("state size: %zu bytes\n", sizeof(isl_state));
    test_thresholds();
    test_start(I);
    test_order_matters(I);
    test_range_is_3d_and_by_collider(I);
    test_legality(I);
    test_clone_is_deterministic(I);
    for (int k = 1; k < argc; k++) test_real_island(argv[k]);
    printf(failures ? "%d test(s) FAILED\n" : "all tests passed\n", failures);
    return failures != 0;
}

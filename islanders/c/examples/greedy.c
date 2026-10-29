/* Greedy player: at every turn place the highest-scoring legal building, and pick the pack whose best
 * first placement scores highest. Plays one island per .isl file given and reports score and speed.
 *
 *   ./build/greedy ../data/islands_bin/s0_*.isl
 */
#include "islanders.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int16_t map[ISL_MAX_GRID];
static long evaluations = 0;

/* Best placement over all held types and rotations. Returns its score, or ISL_ILLEGAL if none. */
static int best_move(const isl_state *s, int *bt, int *br, int *bx, int *bz) {
    int best = ISL_ILLEGAL;
    const isl_island *I = s->island;
    for (int t = 0; t < ISL_N_BUILDINGS; t++) {
        if (!s->inv[t]) continue;
        for (int r = 0; r < 4; r++) {
            isl_score_map(s, t, r, map);
            evaluations += I->nx * I->nz;
            for (int i = 0; i < I->nx * I->nz; i++)
                if (map[i] != ISL_ILLEGAL && map[i] > best) { best = map[i]; *bt = t; *br = r; *bx = i % I->nx; *bz = i / I->nx; }
        }
    }
    return best;
}

static int play(isl_state *s, int *placements, int *packs) {
    static isl_state tmp;
    *placements = 0; *packs = 0;
    while (!s->done) {
        if (s->n_pending > 0) {
            int8_t off[2];
            int n = isl_offers(s, off), pick = 0, pick_val = ISL_ILLEGAL - 1;
            for (int o = 0; o < (n > 0 ? n : 1); o++) {
                isl_clone(&tmp, s);
                isl_choose_pack(&tmp, o);
                int t, r, x, z, v = best_move(&tmp, &t, &r, &x, &z);
                if (v > pick_val) { pick_val = v; pick = o; }
            }
            isl_choose_pack(s, pick);
            (*packs)++;
            continue;
        }
        int t, r, x, z, gain;
        if (best_move(s, &t, &r, &x, &z) == ISL_ILLEGAL) break;   /* buildings left but no space */
        if (isl_place(s, t, r, x, z, &gain) != ISL_OK) break;
        (*placements)++;
    }
    return s->score;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s island.isl [...]\n", argv[0]); return 1; }
    isl_state *s = malloc(sizeof *s);
    double total_time = 0; long total_score = 0; int n_islands = 0;
    for (int k = 1; k < argc; k++) {
        isl_island *I = isl_island_load(argv[k]);
        if (!I) { fprintf(stderr, "could not load %s\n", argv[k]); continue; }
        isl_reset(s, I, 12345 + k);
        clock_t t0 = clock();
        int placements, packs, score = play(s, &placements, &packs);
        double dt = (double)(clock() - t0) / CLOCKS_PER_SEC;
        total_time += dt; total_score += score; n_islands++;
        printf("%-34s stage %d  score %4d  placements %3d  packs %2d  island %s  %.2fs\n", I->template_name, I->stage, score,
               placements, packs, s->island_unlocked ? "unlocked" : "locked  ", dt);
        isl_island_free(I);
    }
    if (n_islands)
        printf("\n%d islands, mean score %.0f, %.0f cell evaluations/s\n", n_islands, (double)total_score / n_islands, evaluations / total_time);
    free(s);
    return 0;
}

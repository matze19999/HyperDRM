#ifndef HYPERDRM_EDGE_OVERLAP_H
#define HYPERDRM_EDGE_OVERLAP_H

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "edge_response.h"

enum { EDGE_STEPS = 33, EDGE_LOCATIONS = EDGE_SIDES * EDGE_STEPS + EDGE_INTERIOR };
enum { EDGE_TOP, EDGE_RIGHT, EDGE_BOTTOM, EDGE_LEFT };
#include "g4_scan_response.h"

typedef struct {
    float region[EDGE_REGIONS], peak, position;
    unsigned edge;
} EdgeLocation;

typedef struct {
    unsigned short first, second;
    float similarity, inverse;
} EdgePair;

typedef struct {
    const EdgeLocation *location;
    float rgb[3];
} EdgeAccent;

typedef struct {
    unsigned char background[3];
    EdgeAccent accents[2];
    unsigned count;
} EdgeFit;

static EdgeLocation edge_locations[EDGE_LOCATIONS];
static EdgePair edge_pairs[(EDGE_LOCATIONS * (EDGE_LOCATIONS - 1)) / 2];
static unsigned edge_pair_count, edge_response_revision;
static const EdgeResponse *cached_edge_response;

static inline void add_edge_location(const EdgeResponse *response, EdgeLocation *location, unsigned edge, float position,
                                   unsigned a, unsigned b, float weight) {
    float squared = 0, peak = 0;
    for (unsigned r = 0; r < EDGE_REGIONS; ++r) {
        float value = (*response)[a][r] * (1 - weight) + (*response)[b][r] * weight;
        location->region[r] = value;
        squared += value * value;
        if (value > peak)
            peak = value;
    }
    float norm = sqrtf(squared);
    for (unsigned r = 0; r < EDGE_REGIONS; ++r)
        location->region[r] /= norm;
    location->edge = edge;
    location->position = position;
    location->peak = peak / norm;
}

static inline void prepare_edge_locations(const EdgeResponse *response, unsigned revision) {
    if (response == cached_edge_response && revision == edge_response_revision)
        return;
    cached_edge_response = response;
    edge_response_revision = revision;
    edge_pair_count = 0;
    for (unsigned edge = 0; edge < EDGE_SIDES; ++edge) {
        for (unsigned k = 0; k < EDGE_STEPS; ++k) {
            float position = (20 + 5 * k) / 200.f, weight;
            unsigned a = edge * EDGE_SAMPLES + edge_anchor_interval(position, &weight);
            add_edge_location(response, &edge_locations[edge * EDGE_STEPS + k], edge, position, a, a + 1, weight);
        }
    }
    for (unsigned i = 0; i < EDGE_INTERIOR; ++i) {
        unsigned a = EDGE_SIDES * EDGE_SAMPLES + i;
        add_edge_location(response, &edge_locations[EDGE_SIDES * EDGE_STEPS + i], EDGE_SIDES, (float)i, a, a, 0);
    }
    for (unsigned i = 0; i < EDGE_LOCATIONS; ++i)
        for (unsigned j = i + 1; j < EDGE_LOCATIONS; ++j) {
            const EdgeLocation *a = &edge_locations[i], *b = &edge_locations[j];
            if (a->edge == b->edge && b->position - a->position < .149f)
                continue;
            float dot = 0;
            for (unsigned r = 0; r < EDGE_REGIONS; ++r)
                dot += a->region[r] * b->region[r];
            if (dot < .995f)
                edge_pairs[edge_pair_count++] = (EdgePair){i, j, dot, 1 / (1 - dot * dot)};
        }
}

static inline bool same_lower_corner(const EdgeLocation *a, const EdgeLocation *b) {
    if (b->edge == EDGE_BOTTOM) {
        const EdgeLocation *swap = a;
        a = b;
        b = swap;
    }
    return a->edge == EDGE_BOTTOM && b->position >= .84f &&
           ((b->edge == EDGE_RIGHT && a->position >= .84f) ||
            (b->edge == EDGE_LEFT && a->position <= .16f));
}

static inline bool fit_edge_response(const unsigned char rgb[EDGE_REGIONS][3], EdgeFit *output,
                                     const EdgeResponse *response, unsigned revision) {
    EdgeFit fit = {0};
    float data[EDGE_REGIONS][3], energy = 0, peak = 0;
    for (unsigned c = 0; c < 3; ++c) {
        unsigned char ordered[EDGE_REGIONS];
        for (unsigned r = 0; r < EDGE_REGIONS; ++r) {
            unsigned i = r;
            while (i && ordered[i - 1] > rgb[r][c]) {
                ordered[i] = ordered[i - 1];
                --i;
            }
            ordered[i] = rgb[r][c];
        }
        unsigned char background = ordered[4];
        if (background > 48)
            return false;
        fit.background[c] = background;
        for (unsigned r = 0; r < EDGE_REGIONS; ++r) {
            float value = rgb[r][c] > background ? rgb[r][c] - background : 0;
            data[r][c] = value;
            energy += value * value;
            if (value > peak)
                peak = value;
        }
    }
    if (peak < 24)
        return false;

    prepare_edge_locations(response, revision);
    float projection[EDGE_LOCATIONS][3] = {{0}};
    float best = 0;
    for (unsigned i = 0; i < EDGE_LOCATIONS; ++i) {
        for (unsigned r = 0; r < EDGE_REGIONS; ++r)
            for (unsigned c = 0; c < 3; ++c)
                projection[i][c] += data[r][c] * edge_locations[i].region[r];
        float score = 0;
        for (unsigned c = 0; c < 3; ++c)
            score += projection[i][c] * projection[i][c];
        if (score > best) {
            best = score;
            fit.count = 1;
            fit.accents[0].location = &edge_locations[i];
            memcpy(fit.accents[0].rgb, projection[i], sizeof(projection[i]));
        }
    }

    /* For two sources, project onto each nonnegative span. If the unconstrained
     * optimum has a negative coefficient, the constrained optimum is on its
     * boundary. Keep the single source when the second adds little evidence. */
    float minimum_pair_score = best + energy * .015f;
    if (minimum_pair_score < energy)
        for (unsigned i = 0; i < edge_pair_count; ++i) {
            EdgePair pair = edge_pairs[i];
            float a[3], b[3], score = 0;
            for (unsigned c = 0; c < 3; ++c) {
                float p = projection[pair.first][c], q = projection[pair.second][c];
                a[c] = (p - pair.similarity * q) * pair.inverse;
                b[c] = (q - pair.similarity * p) * pair.inverse;
                if (a[c] < 0) {
                    a[c] = 0;
                    b[c] = q;
                } else if (b[c] < 0) {
                    a[c] = p;
                    b[c] = 0;
                }
                score += a[c] * p + b[c] * q;
            }
            if (score > minimum_pair_score && score > best) {
                best = score;
                fit.count = 2;
                fit.accents[0].location = &edge_locations[pair.first];
                fit.accents[1].location = &edge_locations[pair.second];
                memcpy(fit.accents[0].rgb, a, sizeof(a));
                memcpy(fit.accents[1].rgb, b, sizeof(b));
            }
        }
    if (best < energy * .92f)
        return false;
    /* At the lower corners, equal responses cannot distinguish an accent on
     * the side from one on the bottom. Keep both projections local to that
     * corner. A three-sided layout only samples the fitted side. */
    if (fit.count == 1 && fit.accents[0].location->edge != EDGE_TOP) {
        const EdgeLocation *first = fit.accents[0].location;
        float alternate_score = best - energy * .003f;
        for (unsigned i = 0; i < EDGE_LOCATIONS; ++i) {
            const EdgeLocation *alternate = &edge_locations[i];
            if (!same_lower_corner(first, alternate))
                continue;
            float score = 0, dot = 0;
            for (unsigned c = 0; c < 3; ++c)
                score += projection[i][c] * projection[i][c];
            for (unsigned r = 0; r < EDGE_REGIONS; ++r)
                dot += first->region[r] * alternate->region[r];
            if (score > alternate_score && dot > .995f) {
                alternate_score = score;
                fit.count = 2;
                fit.accents[1].location = alternate;
                memcpy(fit.accents[1].rgb, projection[i], sizeof(projection[i]));
            }
        }
    }
    *output = fit;
    return true;
}

#endif

#ifndef HYPERDRM_EDGE_RESPONSE_H
#define HYPERDRM_EDGE_RESPONSE_H

enum { EDGE_REGIONS = 16, EDGE_SIDES = 4, EDGE_SAMPLES = 7, EDGE_INTERIOR = 5,
       EDGE_RESPONSE_ROWS = EDGE_SIDES * EDGE_SAMPLES + EDGE_INTERIOR };
typedef unsigned short EdgeResponse[EDGE_RESPONSE_ROWS][EDGE_REGIONS];

/* Anchor positions are in half-percent units, in left-to-right or top-to-bottom order. */
static inline unsigned edge_anchor_interval(float position, float *weight) {
    static const unsigned anchors[] = {20, 50, 75, 100, 125, 150, 180};
    float units = position * 200;
    if (units < anchors[0])
        units = anchors[0];
    if (units > anchors[EDGE_SAMPLES - 1])
        units = anchors[EDGE_SAMPLES - 1];
    unsigned i = 0;
    while (units > anchors[i + 1])
        ++i;
    *weight = (units - anchors[i]) / (anchors[i + 1] - anchors[i]);
    return i;
}

#endif

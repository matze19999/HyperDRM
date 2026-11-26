#ifndef HYPERDRM_DARK_COLOR_H
#define HYPERDRM_DARK_COLOR_H
/* Suppress near-black histogram residuals without changing channel ratios.
 * Apply only to fresh histogram colors, before continuity and interpolation. */
static inline void dark_color_filter(unsigned char color[3], unsigned cutoff) {
    if (!cutoff)
        return;
    unsigned peak = color[0];
    for (unsigned c = 1; c < 3; c++)
        if (color[c] > peak)
            peak = color[c];
    if (peak >= cutoff * 2)
        return;
    unsigned gain = peak > cutoff ? peak - cutoff : 0;
    for (unsigned c = 0; c < 3; c++)
        color[c] = (unsigned char)((color[c] * gain + cutoff / 2) / cutoff);
}
#endif

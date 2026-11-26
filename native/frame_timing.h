#ifndef HYPERDRM_FRAME_TIMING_H
#define HYPERDRM_FRAME_TIMING_H
#include <stdint.h>
#include <stdbool.h>

/* All times are monotonic milliseconds. A zero sample marks a held color,
 * registration, clear command or any other non-capture transaction. */
typedef struct {
    uint64_t sampled, queued, sent, confirmed;
} FrameTiming;

typedef struct {
    uint64_t last_sample;
    unsigned period_tenths;
} SampleTiming;

/* Preserve the requested phase through small wake-up delays. When an entire
 * slot is missed, start a new cadence instead of reading a burst of frames. */
static inline bool capture_slot(uint64_t *due, uint64_t now, unsigned interval) {
    if (*due && now < *due)
        return false;
    *due = !*due || now - *due >= interval ? now + interval : *due + interval;
    return true;
}

static inline void sample_timing_record(SampleTiming *timing, uint64_t now) {
    if (timing->last_sample && now > timing->last_sample) {
        uint64_t period = now - timing->last_sample;
        if (period > 5000) {
            timing->period_tenths = 0;
        } else {
            unsigned measure = (unsigned)period * 10;
            timing->period_tenths = timing->period_tenths
                                        ? (7 * timing->period_tenths + measure + 4) / 8
                                        : measure;
        }
    }
    timing->last_sample = now;
}
#endif

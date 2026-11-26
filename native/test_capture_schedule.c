#include <assert.h>
#include <stdio.h>
#include "frame_timing.h"

int main(void) {
    uint64_t due = 0;
    assert(capture_slot(&due, 1000, 50) && due == 1050);
    assert(!capture_slot(&due, 1049, 50));
    assert(capture_slot(&due, 1054, 50) && due == 1100);
    assert(!capture_slot(&due, 1054, 50));
    assert(capture_slot(&due, 1101, 50) && due == 1150);
    assert(capture_slot(&due, 1250, 50) && due == 1300);
    assert(!capture_slot(&due, 1250, 50));
    puts("PASS: capture keeps its cadence through small delays and skips missed frames");
}

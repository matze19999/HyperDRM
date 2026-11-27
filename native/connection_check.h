#ifndef HYPERDRM_CONNECTION_CHECK_H
#define HYPERDRM_CONNECTION_CHECK_H

static ReceiverProbe connection_test = {.fd = -1};
static ReceiverInstances test_instances;
static FlatbufferOutput test_flatbuffer = {.fd = -1};

static inline const char *test_stage_name(void) {
    return probe_stage_name(&connection_test);
}
static inline bool test_busy(void) {
    return probe_busy(&connection_test) || connection_test.stage == TEST_FLATBUFFER;
}
static inline void test_close(void) {
    probe_close(&connection_test);
    fb_out_release(&test_flatbuffer);
    connection_test = (ReceiverProbe){.fd = -1};
}
static inline bool test_begin(const TargetConfig *target, uint64_t now) {
    if (test_busy())
        return false;
    return target_flatbuffer(target)
               ? probe_begin_view(&connection_test, target, now, &test_instances, true)
               : probe_begin(&connection_test, target, now);
}
static inline void test_tick(PlaybackJson *json, uint64_t now) {
    if (connection_test.stage == TEST_FLATBUFFER) {
        fb_out_tick(&test_flatbuffer, now);
        if (test_flatbuffer.stage == FB_READY || test_flatbuffer.stage == FB_FAILED) {
            bool ready = test_flatbuffer.stage == FB_READY;
            probe_finish(&connection_test, ready ? NULL : test_flatbuffer.error, now);
            fb_out_release(&test_flatbuffer);
        }
        return;
    }
    if (!probe_busy(&connection_test))
        return;
    probe_tick(&connection_test, json, now);
    if (connection_test.stage != TEST_DONE || !connection_test.view)
        return;
    unsigned priority = receiver_test_priority(&test_instances);
    if (!priority)
        probe_finish(&connection_test, "No safe FlatBuffer test priority is available", now);
    else if (fb_out_begin(&test_flatbuffer, &connection_test.target, priority, 512, now))
        connection_test.stage = TEST_FLATBUFFER;
    else {
        probe_finish(&connection_test, test_flatbuffer.error, now);
        fb_out_release(&test_flatbuffer);
    }
}

#define DISPLAY_POLL_MS 5000
static ReceiverProbe display_probe = {.fd = -1};
static ReceiverInstances display_pending, display_snapshot;
static uint64_t next_display_poll;
static bool display_known;

static void display_reset(void) {
    probe_close(&display_probe);
    display_probe.stage = TEST_IDLE;
    next_display_poll = 0;
    display_known = false;
}
static void display_tick(PlaybackJson *json, uint64_t now, bool enabled) {
    if (!enabled || strcmp(target.receiver, "HyperHDR")) {
        if (display_probe.stage != TEST_IDLE || next_display_poll)
            display_reset();
        return;
    }
    if (probe_busy(&display_probe)) {
        probe_tick(&display_probe, json, now);
        if (probe_busy(&display_probe))
            return;
        display_known = display_probe.stage == TEST_DONE;
        if (display_known)
            display_snapshot = display_pending;
        next_display_poll = now + DISPLAY_POLL_MS;
        return;
    }
    if (now >= next_display_poll) {
        probe_begin_view(&display_probe, &target, now, &display_pending, false);
        next_display_poll = now + DISPLAY_POLL_MS;
        if (!probe_busy(&display_probe))
            display_known = false;
    }
}
#endif

#ifndef HYPERDRM_FRAME_OUTPUT_H
#define HYPERDRM_FRAME_OUTPUT_H
/* Transport dispatch. Hyperion and single-instance HyperHDR use the JSON API,
 * where a connection is bound to one LED hardware instance. HyperHDR with the
 * FlatBuffer transport decodes one image in the receiver and fans it out to
 * every running instance, so no per-instance connection is needed. */
typedef struct {
    char *line;
    unsigned char *plane;
    bool overlap_applied;
} OutputFrame;

static const EdgeResponse *frame_overlap_response(void) {
    CalibrationKind kind = calibration_effective();
    if (kind == CAL_MEASURED)
        return &calibration.response;
    if (kind == CAL_G4_OVERLAP && !strcmp(target.receiver, "HyperHDR"))
        return &g4_response;
    return NULL;
}

static OutputFrame frame_build(const unsigned char colors[16][3], const LedBlend *mapping,
                               unsigned count, unsigned duration, const unsigned char regions[16][3]) {
    OutputFrame frame = {0};
    const EdgeResponse *response = frame_overlap_response();
    if (target_flatbuffer(&target)) {
        frame.plane = malloc((size_t)SPATIAL_PIXELS * 3);
        if (frame.plane) {
            frame.overlap_applied = regions && response && spatial_edge_response(
                regions, frame.plane, target.black_cutoff, response, calibration_revision());
            if (!frame.overlap_applied)
                spatial_plane(colors, frame.plane);
        }
        return frame;
    }
    if (response && count && mapping) {
        static unsigned char plane[SPATIAL_PIXELS * 3];
        frame.overlap_applied = regions && spatial_edge_response(
            regions, plane, target.black_cutoff, response, calibration_revision());
        if (!frame.overlap_applied)
            spatial_plane(colors, plane);
        /* Hyperion treats a nonempty format as a named image codec. */
        frame.line = spatial_rgb_command_format(plane, PRIORITY, duration,
                                                !strcmp(target.receiver, "Hyperion") ? "" : "rgb");
        return frame;
    }
    frame.line = region_command(colors, mapping, count, duration);
    return frame;
}
static void frame_release(OutputFrame *frame) {
    free(frame->line);
    free(frame->plane);
    frame->line = NULL;
    frame->plane = NULL;
}
static bool frame_send(const OutputFrame *frame, unsigned duration, uint64_t sampled) {
    size_t budget = 65536;
    if (target_flatbuffer(&target)) {
        FlatbufferOutput *output = &receiver_runtime.flatbuffer;
        if (!frame->plane || !fb_out_frame(output, frame->plane, SPATIAL_WIDTH,
                                           SPATIAL_HEIGHT, (int)duration, milliseconds()))
            return false;
        uint64_t queued = milliseconds();
        output->pending_timing = (FrameTiming){.sampled = sampled, .queued = queued};
        return fb_out_flush(output, queued, &budget);
    } else {
        ReceiverOutput *output = &receiver_runtime.output;
        if (!frame->line || !output_frame(output, frame->line))
            return false;
        uint64_t queued = milliseconds();
        output->latest_timing = (FrameTiming){.sampled = sampled, .queued = queued};
        return output_start_latest(output, queued) && output_flush(output, &budget, queued);
    }
}
static FrameTiming receiver_confirmed_timing(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.confirmed_timing
                                      : receiver_runtime.output.confirmed_timing;
}
static bool receiver_begin(uint64_t now) {
    if (target_flatbuffer(&target))
        return fb_out_begin(&receiver_runtime.flatbuffer, &target, PRIORITY,
                            (size_t)SPATIAL_PIXELS * 3 + 512, now);
    return output_begin(&receiver_runtime.output, &target, now);
}
static void receiver_tick(PlaybackJson *json, uint64_t now) {
    /* Complete a previous transport's Clear after settings select another one. */
    fb_out_tick(&receiver_runtime.flatbuffer, now);
    output_tick(&receiver_runtime.output, json, now);
}
static void receiver_stop(uint64_t now, bool clear) {
    if (target_flatbuffer(&target))
        fb_out_stop(&receiver_runtime.flatbuffer, now, clear);
    else
        output_stop(&receiver_runtime.output, now, clear);
}
static void receiver_close(void) {
    fb_out_close(&receiver_runtime.flatbuffer);
    receiver_runtime.flatbuffer.stage = FB_IDLE;
    output_close(&receiver_runtime.output);
}
static bool receiver_ready(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.stage == FB_READY
                                      : receiver_runtime.output.stage == OUTPUT_READY;
}
static bool receiver_failed(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.stage == FB_FAILED
                                      : receiver_runtime.output.stage == OUTPUT_FAILED;
}
static const char *receiver_error(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.error
                                      : receiver_runtime.output.error;
}
static unsigned long receiver_frames(void) {
    return target_flatbuffer(&target) ? receiver_runtime.flatbuffer.frames
                                      : receiver_runtime.output.acknowledgements;
}
static bool receiver_closing(void) {
    return receiver_runtime.flatbuffer.closing || receiver_runtime.output.closing;
}
#endif

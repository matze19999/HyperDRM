/* Read-only runtime status serialization. */
static bool status(void *h, void *m, void *ctx) {
    (void)ctx;
    char json[12288], safe[512], endpoint[80], test_error[256];
    size_t n = 0;
    snprintf(endpoint, sizeof(endpoint), strchr(target.host, ':') ? "[%s]:%u" : "%s:%u",
             target.host, target.port);
    for (size_t i = 0; last_error[i] && n + 2 < sizeof(safe); i++) {
        char c = last_error[i];
        if (c == '"' || c == '\\')
            safe[n++] = '\\';
        if ((unsigned char)c >= 32)
            safe[n++] = c;
    }
    safe[n] = 0;
    size_t error_used = 0;
    for (const unsigned char *p = (const unsigned char *)connection_test.error; *p; p++) {
        if (*p < 32)
            continue;
        if (*p == '"' || *p == '\\')
            test_error[error_used++] = '\\';
        test_error[error_used++] = (char)*p;
    }
    test_error[error_used] = 0;
    uint64_t now = milliseconds();
    FrameTiming frame = receiver_confirmed_timing();
    bool fresh_timing = capture_ctx.handle && capture_ctx.color_valid && frame.sampled &&
                        frame.sampled <= frame.queued && frame.queued <= frame.sent &&
                        frame.sent <= frame.confirmed && frame.confirmed <= now &&
                        now - frame.confirmed <= 3000;
    unsigned period = capture_ctx.handle && capture_ctx.color_valid
                          ? capture_ctx.sample_timing.period_tenths
                          : 0;
    char report[512];
    size_t k = 0;
    for (size_t i = 0; calibration_ctx.message[i] && k + 2 < sizeof(report); i++) {
        char c = calibration_ctx.message[i];
        if (c == '"' || c == '\\')
            report[k++] = '\\';
        if ((unsigned char)c >= 32)
            report[k++] = c;
    }
    report[k] = 0;
    unsigned spatial_rectangles = 0;
    if (calibration_rectangles())
        calibration_blocks(&spatial_rectangles);
    int used = snprintf(
        json, sizeof(json),
        "{\"receiverConnecting\":%s,\"calibrationProfile\":\"%s\",\"effectiveCalibration\":\"%s\","
        "\"tvModel\":\"%s\",\"customModel\":\"%s\",\"customPoints\":[",
        receiver_runtime.starting ? "true" : "false", calibration_key(calibration.kind),
        calibration_key(calibration_effective()), tv_model, calibration.model);
    for (unsigned i = 0; i < calibration.count; i++)
        used +=
            snprintf(json + used, sizeof(json) - used, "%s{\"position\":%.12g,\"region\":%u}",
                     i ? "," : "", calibration.points[i].position, calibration.points[i].region);
    used += snprintf(json + used, sizeof(json) - used, "],\"customRectangles\":[");
    for (unsigned i = 0; i < calibration.rect_count && used + 96 < (int)sizeof(json); i++)
        used += snprintf(json + used, sizeof(json) - used,
                         "%s{\"region\":%u,\"x0\":%.12g,\"y0\":%.12g,\"x1\":%.12g,\"y1\":%.12g}",
                         i ? "," : "", calibration.rects[i].region, calibration.rects[i].x0,
                         calibration.rects[i].y0, calibration.rects[i].x1, calibration.rects[i].y1);
    used += snprintf(json + used, sizeof(json) - used, "],\"customRectangleCount\":%u,\"spatialRectangleCount\":%u,"
                                                             "\"customGeometry\":\"%s\","
                     "\"spatialGeometry\":\"%s\",\"overlapApplied\":%s,\"layoutUpdates\":%u,"
                     "\"responseCalibrationSupported\":true,\"responseCalibrationAvailable\":%s,"
                     "\"lastLayoutCheckAgeMs\":%lld,\"layoutError\":\"%s\","
                     "\"piccapFallbacks\":%u,\"piccapCooldownMs\":%llu,",
                     calibration.rect_count, spatial_rectangles,
                     calibration.rect_geometry ? "rectangles" : "rim",
                     frame_overlap_response() ? calibration_key(calibration_effective()) : calibration_rectangles() ? "rectangles" : "rim",
                     frame_overlap_response() && capture_ctx.color_valid && capture_ctx.overlap_applied ? "true" : "false",
                     layout_updates,
                     calibration_measured_available() ? "true" : "false",
                     last_layout_check ? (long long)(now - last_layout_check) : -1, layout_error,
                     piccap_fallbacks,
                     (unsigned long long)(handover_cooldown > now ? handover_cooldown - now : 0));
    used += snprintf(json + used, sizeof(json) - used, "\"calibrationMessage\":\"%s\",", report);
    used += snprintf(json + used, sizeof(json) - used,
                     "\"calibrationDraft\":{\"model\":\"%s\",\"points\":[",
                     calibration_ctx.draft.model);
    for (unsigned i = 0; i < calibration_ctx.draft.count; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s{\"position\":%.12g,\"region\":%u}",
                         i ? "," : "", calibration_ctx.draft.points[i].position,
                         calibration_ctx.draft.points[i].region);
    used += snprintf(json + used, sizeof(json) - used, "]},\"receiverStreamConnected\":%s,",
                     stream.stage == STREAM_SUBSCRIBED ? "true" : "false");
    used += snprintf(json + used, sizeof(json) - used,
                     "\"sampleSequence\":%u,\"sampleAgeMs\":%lld,\"holdingColors\":%s,"
                     "\"sourceFade\":%s,\"calibrating\":%s,",
                     capture_ctx.sample_sequence,
                     capture_ctx.sample_at ? (long long)(now - capture_ctx.sample_at) : -1,
                     capture_ctx.continuity.holding ? "true" : "false",
                     (capture_ctx.continuity.fading || bridge_phase == BRIDGE_FADING) ? "true"
                                                                                      : "false",
                     CALIBRATION_ACTIVE ? "true" : "false");
    used += snprintf(json + used, sizeof(json) - used,
                     "\"blackCutoff\":%u,\"timing\":{\"sampleIntervalMs\":%.1f,"
                     "\"captureMs\":%lld,\"queueMs\":%lld,\"ackMs\":%lld,\"totalMs\":%lld},",
                     target.black_cutoff, period / 10.0,
                     fresh_timing ? (long long)(frame.queued - frame.sampled) : -1,
                     fresh_timing ? (long long)(frame.sent - frame.queued) : -1,
                     fresh_timing ? (long long)(frame.confirmed - frame.sent) : -1,
                     fresh_timing ? (long long)(frame.confirmed - frame.sampled) : -1);
    used +=
        snprintf(json + used, sizeof(json) - used,
                 "\"returnValue\":true,\"captureIntervalMs\":%u,\"piccapPort\":%u,\"connectionTest\":{\"state\":\"%s\","
                 "\"busy\":%s,\"transport\":\"%s\",\"ledCount\":%u,\"elapsedMs\":%llu,\"error\":\"%s\"},\"lastOutputAgeMs\":%"
                 "lld,\"retryInMs\":%llu,\"captureState\":\"%s\",\"inputPreview\":\"%s\",\"piccapError\":\"%s\",",
                 target_capture_interval(&target), target.piccap_port, test_stage_name(), test_busy() ? "true" : "false",
                 target_flatbuffer(&target) ? "FlatBuffer" : "JSON", connection_test.leds,
                 (unsigned long long)connection_test.elapsed, test_error,
                 capture_ctx.last_output_at ? (long long)(now - capture_ctx.last_output_at) : -1,
                 (unsigned long long)(retries.due > now ? retries.due - now : 0),
                 capture_source == SOURCE_PICCAP_STARTING   ? "Starting PicCap"
                 : capture_source == SOURCE_PICCAP_STOPPING ? "Stopping PicCap"
                 : piccap_owned                             ? "PicCap"
                 : capture_ctx.handle                       ? "Histogram"
                                                            : "Stopped",
                 input_preview_status(), piccap_error);
    used += snprintf(
        json + used, sizeof(json) - used,
        "\"version\":\"" HYPERDRM_VERSION
        "\",\"running\":%s,\"colorValid\":%s,\"frames\":%lu,\"captureRecoveries\":%u,"
        "\"lastRecoveryMs\":%llu,\"endpoint\":\"%s\",\"host\":\"%s\",\"port\":%u,"
        "\"receiver\":\"%s\",\"output\":\"%s\",\"flatbufferConnected\":%s,"
        "\"tokenSet\":%s,\"piccapTargetCompatible\":%s,"
        "\"priority\":%d,\"uid\":%u,\"drmVerified\":false,\"mappingVerified\":false,"
        "\"outputColor\":[%u,%u,%u],\"error\":\"%s\",\"regions\":[",
        (capture_ctx.handle || capture_source == SOURCE_PICCAP) ? "true" : "false",
        capture_ctx.color_valid ? "true" : "false", capture_ctx.frames, capture_ctx.recoveries,
        (unsigned long long)capture_ctx.last_recovery_ms, endpoint, target.host, target.port,
        target.receiver, target_flatbuffer(&target) ? "flatbuffer" : "json",
        target_flatbuffer(&target) && receiver_runtime.flatbuffer.stage == FB_READY ? "true"
                                                                                   : "false",
        target.token[0] ? "true" : "false",
        target_piccap_compatible(&target) ? "true" : "false", PRIORITY, (unsigned)geteuid(),
        capture_ctx.sent_rgb[0], capture_ctx.sent_rgb[1], capture_ctx.sent_rgb[2], safe);
    for (int i = 0; i < 16; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s[%u,%u,%u]", i ? "," : "",
                         capture_ctx.rgb[i][0], capture_ctx.rgb[i][1], capture_ctx.rgb[i][2]);
    used += snprintf(json + used, sizeof(json) - used,
                     "],\"ledCount\":%u,\"spatialOutput\":%s,\"blackPercent\":[",
                     receiver_layout.count, (receiver_layout.count || target_flatbuffer(&target)) ? "true" : "false");
    for (int i = 0; i < 16; i++)
        used += snprintf(json + used, sizeof(json) - used, "%s%u", i ? "," : "",
                         capture_ctx.black_percent[i]);
    snprintf(json + used, sizeof(json) - used,
             "],\"preset\":%u,\"saturationGain\":1.0,\"brightnessGain\":1.0,"
             "\"spatialInterpolation\":%s,\"blackBarDetection\":%s,\"blackBarDetectionWritable\":true,\"autostart\":%s,"
             "\"retryScheduled\":%s,\"retryPaused\":%s,\"retryBlocked\":%s,\"retryAttempts\":%u,"
             "\"piccapInstalled\":%s,\"piccapHandover\":%s,\"piccapConnected\":%s,\"activeSource\":"
             "\"%s\",\"playbackProtection\":\"%s\",\"letterboxDetected\":%s}",
             capture_ctx.preset, receiver_layout.interpolation ? "true" : "false",
             target.black_bar_detection_disabled ? "false" : "true",
             boot_enabled() ? "true" : "false", retries.due ? "true" : "false",
             retries.paused ? "true" : "false", retries.blocked ? "true" : "false",
             retries.attempts, piccap_installed() ? "true" : "false",
             handover_enabled ? "true" : "false", piccap_connected ? "true" : "false",
             piccap_owned ? "PicCap" : "HyperDRM",
             protection == PLAYBACK_PROTECTED ? "protected"
             : protection == PLAYBACK_CLEAR   ? "non-secure decoder"
                                              : "unknown",
             !target.black_bar_detection_disabled && capture_ctx.handle && capture_ctx.letterbox_mode ? "true" : "false");
    return respond(h, m, json);
}

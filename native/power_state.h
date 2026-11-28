/* Follow the TV's display state independently of the manual Stop preference. */
#define POWER_STATE_URI "luna://com.webos.service.tvpower/power/getPowerState"

static bool capture_power_suspended;
static struct {
    unsigned long token;
    uintptr_t serial;
    uint64_t next_probe, deadline;
    bool confirmed;
} power_monitor;

static void power_state_cancel(void) {
    unsigned long token = power_monitor.token;
    power_monitor.token = 0;
    power_monitor.confirmed = false;
    if (token) {
        LSError error;
        error_init(&error);
        cancel_call(service_handle, token, &error);
        error_free(&error);
    }
}

static void power_state_apply(bool suspended, uint64_t now) {
    if (capture_power_suspended == suspended)
        return;
    capture_power_suspended = suspended;
    if (!suspended) {
        if (retries.due)
            retry_source_transition(&retries, now);
        return;
    }
    if (!retries.paused && !retries.blocked &&
        (capture_ctx.handle || receiver_runtime.starting || piccap_owned || retries.due))
        retries.due = now;
    calibration_ctx.until = 0;
    protection = PLAYBACK_UNKNOWN;
    clear_samples = 0;
    clear_since = unknown_since = 0;
    if (piccap_owned && capture_source != SOURCE_PICCAP_STOPPING) {
        capture_source = SOURCE_PICCAP_STOPPING;
        next_piccap_probe = 0;
    }
    if (bridge_phase != BRIDGE_IDLE && bridge_clear())
        stream_tick(&playback_json, now, true);
    stream_close(now);
    bridge_phase = BRIDGE_IDLE;
    bridge_source = capture_source;
    capture_ctx.continuity = (Continuity){0};
    stop_capture();
    receiver_tick(&playback_json, now);
}

static bool power_state_reply(void *h, void *message, void *context) {
    (void)h;
    if (!power_monitor.token || (uintptr_t)context != power_monitor.serial)
        return true;
    PlaybackJson *json = &playback_json;
    void *root = playback_parse_exact(json, payload(message));
    void *result = playback_field(json, root, "returnValue");
    uint64_t now = milliseconds();
    if (result && json->type(result) == 1 && !json->boolean(result)) {
        power_state_cancel();
        power_monitor.next_probe = now + 5000;
    } else {
        if (playback_true(json, root, "subscribed"))
            power_monitor.confirmed = true;
        if (playback_is(json, root, "state", "Active Standby") ||
            playback_is(json, root, "state", "Standby") ||
            playback_is(json, root, "state", "Suspend") ||
            playback_is(json, root, "state", "Power Off") ||
            playback_is(json, root, "state", "Screen Off"))
            power_state_apply(true, now);
        else if (playback_is(json, root, "state", "Active") ||
                 playback_is(json, root, "state", "Screen On") ||
                 playback_is(json, root, "state", "Screen Saver"))
            power_state_apply(false, now);
    }
    if (root)
        json->put(root);
    return true;
}

static void power_state_tick(uint64_t now) {
    if (!call_subscribe || !playback_json.lib)
        return;
    if (power_monitor.token) {
        if (!power_monitor.confirmed && now >= power_monitor.deadline)
            power_state_cancel();
        return;
    }
    if (now < power_monitor.next_probe)
        return;
    LSError error;
    error_init(&error);
    power_monitor.next_probe = now + 5000;
    power_monitor.deadline = now + 2500;
    if (!call_subscribe(service_handle, POWER_STATE_URI, "{\"subscribe\":true}", power_state_reply,
                        (void *)++power_monitor.serial, &power_monitor.token, &error))
        power_monitor.token = 0;
    error_free(&error);
}

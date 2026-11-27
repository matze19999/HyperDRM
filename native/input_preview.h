#ifndef HYPERDRM_INPUT_PREVIEW_H
#define HYPERDRM_INPUT_PREVIEW_H

typedef enum { PREVIEW_UNKNOWN, PREVIEW_ENABLED, PREVIEW_DISABLED } InputPreviewState;
typedef enum { PREVIEW_IDLE, PREVIEW_FOREGROUND, PREVIEW_WRITE } InputPreviewRequest;

static struct {
    bool enabled;
    InputPreviewState desired, applied, pending;
    InputPreviewRequest request;
    uint64_t next_read, next_write, deadline;
    unsigned long token;
    uintptr_t serial;
} input_preview;

static const char *input_preview_status(void) {
    static const char *const states[] = {"Unknown", "Enabled", "Disabled"};
    return states[input_preview.applied];
}

static InputPreviewState input_preview_foreground(PlaybackJson *json, void *root) {
    if (!playback_true(json, root, "returnValue"))
        return PREVIEW_UNKNOWN;
    void *app = playback_field(json, root, "appId");
    if (!app || json->type(app) != 6 || !json->string(app)[0])
        return PREVIEW_UNKNOWN;
    return !strcmp(json->string(app), "com.webos.app.home") ? PREVIEW_ENABLED : PREVIEW_DISABLED;
}

static void input_preview_failure(InputPreviewRequest request, uint64_t now) {
    if (request == PREVIEW_FOREGROUND) {
        input_preview.desired = PREVIEW_UNKNOWN;
        input_preview.next_read = now + 1000;
    } else if (request == PREVIEW_WRITE) {
        input_preview.applied = PREVIEW_UNKNOWN;
        input_preview.next_write = now + 5000;
    }
}

static bool input_preview_reply(void *h, void *message, void *context) {
    (void)h;
    if (input_preview.request == PREVIEW_IDLE || (uintptr_t)context != input_preview.serial)
        return true;
    InputPreviewRequest request = input_preview.request;
    input_preview.request = PREVIEW_IDLE;
    input_preview.token = 0;
    void *root = playback_parse_exact(&playback_json, payload(message));
    if (request == PREVIEW_FOREGROUND) {
        input_preview.desired = input_preview_foreground(&playback_json, root);
        input_preview.next_read = milliseconds() + 1000;
    } else if (playback_true(&playback_json, root, "returnValue")) {
        input_preview.applied = input_preview.pending;
        input_preview.next_write = 0;
    } else {
        input_preview_failure(request, milliseconds());
    }
    if (root)
        playback_json.put(root);
    return true;
}

static void input_preview_cancel(void) {
    unsigned long token = input_preview.token;
    input_preview.request = PREVIEW_IDLE;
    input_preview.token = 0;
    if (token) {
        LSError error;
        error_init(&error);
        cancel_call(service_handle, token, &error);
        error_free(&error);
    }
}

static void input_preview_send(InputPreviewRequest request, uint64_t now, const char *uri,
                               const char *body) {
    LSError error;
    error_init(&error);
    input_preview.request = request;
    input_preview.deadline = now + 2500;
    if (!call_once(service_handle, uri, body, input_preview_reply,
                   (void *)++input_preview.serial, &input_preview.token, &error)) {
        input_preview.request = PREVIEW_IDLE;
        input_preview.token = 0;
        input_preview_failure(request, now);
    }
    error_free(&error);
}

static void input_preview_tick(uint64_t now) {
    if (!input_preview.enabled)
        return;
    if (input_preview.request != PREVIEW_IDLE) {
        if (now >= input_preview.deadline) {
            input_preview_failure(input_preview.request, now);
            input_preview_cancel();
        }
        return;
    }
    if (now >= input_preview.next_read) {
        input_preview_send(PREVIEW_FOREGROUND, now,
                           "luna://com.webos.service.applicationmanager/getForegroundAppInfo", "{}");
    } else if (input_preview.desired != PREVIEW_UNKNOWN &&
               input_preview.desired != input_preview.applied && now >= input_preview.next_write) {
        input_preview.pending = input_preview.desired;
        const char *body = input_preview.pending == PREVIEW_DISABLED
                               ? "{\"configs\":{\"com.webos.surfacemanager.disableCapture\":true},\"volatile\":true}"
                               : "{\"configs\":{\"com.webos.surfacemanager.disableCapture\":false},\"volatile\":true}";
        input_preview_send(PREVIEW_WRITE, now, "luna://com.webos.service.config/setConfigs", body);
    }
}
#endif

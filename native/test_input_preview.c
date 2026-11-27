#define clock_gettime test_clock_gettime
#define main service_entry_point
#include "service.c"
#undef main
#undef clock_gettime
#include <assert.h>

static uint64_t test_now;
int test_clock_gettime(clockid_t clock, struct timespec *time) {
    (void)clock;
    time->tv_sec = (time_t)(test_now / 1000);
    time->tv_nsec = (long)(test_now % 1000) * 1000000L;
    return 0;
}

static const char *last_uri;
static char request_body[140];
static unsigned sent, cancelled;
static Callback callback;
static void *callback_context;

static bool fake_error_init(LSError *error) {
    *error = (LSError){0};
    return true;
}
static void fake_error_free(LSError *error) {
    (void)error;
}
static const char *fake_payload(void *message) {
    return message;
}
static bool fake_call(void *handle, const char *uri, const char *body, Callback cb,
                      void *context, unsigned long *token, LSError *error) {
    (void)error;
    assert(handle == service_handle);
    last_uri = uri;
    snprintf(request_body, sizeof(request_body), "%s", body);
    callback = cb;
    callback_context = context;
    *token = ++sent;
    return true;
}
static bool fake_cancel(void *handle, unsigned long token, LSError *error) {
    (void)error;
    assert(handle == service_handle && token == sent);
    cancelled++;
    return true;
}
static void answer(const char *text) {
    assert(callback(service_handle, (void *)text, callback_context));
}
static void advance(uint64_t now) {
    test_now = now;
    input_preview_tick(now);
}
static void expect_write(uint64_t now, bool disable) {
    unsigned before = sent;
    advance(now);
    assert(sent == before + 1 && strstr(last_uri, "/setConfigs"));
    void *root = playback_parse_exact(&playback_json, request_body);
    void *configs = playback_field(&playback_json, root, "configs");
    void *field = playback_field(&playback_json, configs, "com.webos.surfacemanager.disableCapture");
    assert(playback_true(&playback_json, root, "volatile"));
    assert(field && playback_json.type(field) == 1 && !!playback_json.boolean(field) == disable);
    playback_json.put(root);
}

int main(void) {
    static const char home[] = "{\"returnValue\":true,\"appId\":\"com.webos.app.home\"}";
    static const char hdmi[] = "{\"returnValue\":true,\"appId\":\"com.webos.app.hdmi2\"}";
    static const char app[] = "{\"returnValue\":true,\"appId\":\"com.example.player\"}";
    static const char success[] = "{\"returnValue\":true}";
    call_once = fake_call;
    cancel_call = fake_cancel;
    error_init = fake_error_init;
    error_free = fake_error_free;
    payload = fake_payload;
    service_handle = &sent;
    assert(playback_json_init(&playback_json));

    uint64_t now = 10000;
    advance(now);
    assert(!sent);
    input_preview.enabled = true;
    advance(now);
    assert(sent == 1 && strstr(last_uri, "/getForegroundAppInfo"));
    advance(now + 1500);
    assert(sent == 1);
    answer(home);
    now += 1500;
    expect_write(now + 5, false);
    answer(success);
    assert(!strcmp(input_preview_status(), "Enabled"));

    advance(now + 1000);
    answer("{\"returnValue\":true,\"appId\":\"\"}");
    advance(now + 1010);
    assert(sent == 3 && !strcmp(input_preview_status(), "Enabled"));
    advance(now + 2000);
    answer(hdmi);
    expect_write(now + 2010, true);
    answer(success);
    assert(!strcmp(input_preview_status(), "Disabled"));

    advance(now + 3000);
    answer(app);
    advance(now + 3010);
    assert(sent == 6);
    advance(now + 4000);
    answer(home);
    expect_write(now + 4010, false);
    Callback cancelled_callback = callback;
    void *cancelled_context = callback_context;
    advance(now + 6600);
    assert(cancelled == 1 && !strcmp(input_preview_status(), "Unknown"));
    advance(now + 6605);
    answer(hdmi);
    assert(cancelled_callback(service_handle, (void *)success, cancelled_context));
    advance(now + 6610);
    assert(sent == 9 && !strcmp(input_preview_status(), "Unknown"));
    uint64_t retry_at = input_preview.next_write;
    advance(retry_at);
    answer(hdmi);
    expect_write(retry_at + 5, true);
    answer(success);
    assert(!strcmp(input_preview_status(), "Disabled"));

    advance(retry_at + 1000);
    answer(home);
    expect_write(retry_at + 1005, false);
    answer("{\"returnValue\":false}");
    assert(!strcmp(input_preview_status(), "Unknown"));
    assert(input_preview.next_write > milliseconds());
    input_preview_cancel();
    dlclose(playback_json.lib);
    puts("PASS: Home and app preview policy, volatile writes, malformed replies, retries and late callbacks");
}

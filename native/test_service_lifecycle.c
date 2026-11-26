#define DEV_MODE "/tmp/hyperdrm-lifecycle-marker"
#define TARGET_PREF "/tmp/hyperdrm-lifecycle-target.json"
#define main service_entry_point
#include "service.c"
#undef main
#include <assert.h>
static unsigned initializes, finalizes;
static int fake_handle;
static char answer[8192];
static bool fake_reply(void *h, void *m, const char *s, LSError *e) {
    (void)h;
    (void)m;
    (void)e;
    snprintf(answer, sizeof(answer), "%s", s);
    return true;
}
static const char *fake_payload(void *m) {
    return m;
}
static bool fake_error_init(LSError *e) {
    memset(e, 0, sizeof(*e));
    return true;
}
static void fake_error_free(LSError *e) {
    (void)e;
}
static bool fake_call(void *h, const char *u, const char *p, Callback cb, void *ctx,
                      unsigned long *t, LSError *e) {
    (void)h;
    (void)u;
    (void)p;
    (void)cb;
    (void)ctx;
    (void)t;
    (void)e;
    return false;
}
static bool fake_cancel(void *h, unsigned long t, LSError *e) {
    (void)h;
    (void)t;
    (void)e;
    return true;
}
static void *fake_init(bool (*p)(void)) {
    (void)p;
    initializes++;
    return &fake_handle;
}
static int fake_preset(void *h, int p) {
    assert(h == &fake_handle);
    (void)p;
    return 0;
}
static int fake_get(void *h, Histogram *hist) {
    assert(h == &fake_handle);
    for (unsigned r = 0; r < 16; r++)
        for (unsigned c = 0; c < 3; c++)
            hist[r].bins[c][80 + c * 20] = 100;
    return 0;
}
static int fake_finish(void *h) {
    assert(h == &fake_handle);
    finalizes++;
    return 0;
}
static unsigned request_id(const char *line) {
    const char *p = strstr(line, "\"tan\":");
    assert(p);
    return strtoul(p + 6, NULL, 10);
}
static void success(int fd, unsigned id, bool layout) {
    char data[256];
    int n = snprintf(data, sizeof(data),
                     layout ? "{\"success\":true,\"tan\":%u,\"info\":{\"leds\":[{\"hmin\":0,"
                              "\"hmax\":1,\"vmin\":0,\"vmax\":0.1}]}}\n"
                            : "{\"success\":true,\"tan\":%u}\n",
                     id);
    assert(send(fd, data, n, MSG_NOSIGNAL) == n);
}
static void test_restart_after_flatbuffer_clear(void) {
    int peer[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, peer) == 0);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr = {htonl(INADDR_LOOPBACK)}};
    assert(!bind(server, (void *)&address, sizeof(address)) && !listen(server, 1));
    socklen_t size = sizeof(address);
    assert(!getsockname(server, (void *)&address, &size));

    retry_reset(&retries);
    strcpy(target.receiver, "HyperHDR");
    strcpy(target.output, "flatbuffer");
    FlatbufferOutput *old = &receiver_runtime.flatbuffer;
    old->fd = peer[0];
    old->stage = FB_READY;
    old->priority = PRIORITY;
    old->scratch_capacity = 128;
    old->scratch = malloc(old->scratch_capacity);
    old->replies = malloc(FB_REPLY_LIMIT);
    assert(old->scratch && old->replies);
    capture_ctx.handle = &fake_handle;

    char change[160];
    snprintf(change, sizeof(change),
             "{\"host\":\"127.0.0.1\",\"port\":%u,\"receiver\":\"Hyperion\",\"output\":\"json\"}",
             ntohs(address.sin_port));
    assert(settings_request((void *)1, "{}", change));
    assert(strstr(answer, "\"returnValue\":true") && !capture_ctx.handle && receiver_closing());
    assert(!target_flatbuffer(&target) && retries.due);
    tick(NULL);
    assert(!receiver_runtime.starting && old->closing && old->awaiting);
    unsigned char request[128];
    assert(recv(peer[1], request, sizeof(request), 0) > 4);

    const unsigned char ack[] = {0,0,0,12, 8,0,0,0, 4,0,4,0, 4,0,0,0};
    assert(send(peer[1], ack, sizeof(ack), MSG_NOSIGNAL) == sizeof(ack));
    tick(NULL);
    assert(old->stage == FB_IDLE && !receiver_closing() && receiver_runtime.starting);
    int connection = accept(server, NULL, NULL);
    assert(connection >= 0);
    close(connection);
    receiver_close();
    fb_out_release(old);
    close(peer[1]);
    close(server);
    assert(unlink(TARGET_PREF) == 0);
}
int main(void) {
    assert(access(DEV_MODE, F_OK) != 0);
    reply = fake_reply;
    payload = fake_payload;
    error_init = fake_error_init;
    error_free = fake_error_free;
    call_once = fake_call;
    cancel_call = fake_cancel;
    vt_init = fake_init;
    vt_preset = fake_preset;
    vt_get = fake_get;
    vt_finish = fake_finish;
    assert(playback_json_init(&playback_json));
    next_layout_check = UINT64_MAX;
    calibration.kind = CAL_G4;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr = {htonl(INADDR_LOOPBACK)}};
    assert(!bind(server, (void *)&address, sizeof(address)) && !listen(server, 2));
    socklen_t size = sizeof(address);
    assert(!getsockname(server, (void *)&address, &size));
    target.port = ntohs(address.sin_port);
    assert(start((void *)1, "{}", NULL) && strstr(answer, "connecting"));
    assert(receiver_runtime.starting && !capture_ctx.handle && !initializes);
    assert(stop((void *)1, "{}", NULL));
    assert(!receiver_runtime.starting && receiver_runtime.output.fd < 0 && !initializes);
    int cancelled = accept(server, NULL, NULL);
    assert(cancelled >= 0);
    close(cancelled);
    tick(NULL);
    assert(!initializes && retries.paused);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int fd = accept(server, NULL, NULL);
        assert(fd >= 0);
        FILE *input = fdopen(dup(fd), "r");
        char line[2048];
        assert(fgets(line, sizeof(line), input) && strstr(line, "serverinfo"));
        success(fd, request_id(line), true);
        assert(fgets(line, sizeof(line), input) && strstr(line, "color"));
        unsigned id = request_id(line);
        usleep(200000);
        success(fd, id, false);
        assert(fgets(line, sizeof(line), input) && strstr(line, "clear"));
        success(fd, request_id(line), false);
        usleep(20000);
        fclose(input);
        close(fd);
        close(server);
        _exit(0);
    }
    assert(start((void *)1, "{}", NULL));
    for (unsigned i = 0; i < 1000 && !receiver_runtime.output.awaiting; i++) {
        tick(NULL);
        usleep(1000);
    }
    for (unsigned i = 0; i < 1000 && (!capture_ctx.handle || !receiver_runtime.output.awaiting);
         i++) {
        tick(NULL);
        usleep(1000);
    }
    assert(capture_ctx.handle && initializes == 1 && receiver_runtime.output.awaiting);
    uint64_t before = milliseconds();
    assert(status((void *)1, "{}", NULL));
    assert(milliseconds() - before < 100 && strstr(answer, "\"running\":true"));
    before = milliseconds();
    assert(stop((void *)1, "{}", NULL));
    assert(milliseconds() - before < 100);
    assert(!capture_ctx.handle && finalizes == 1 && retries.paused);
    for (unsigned i = 0; i < 1000 && receiver_runtime.output.stage != OUTPUT_IDLE; i++) {
        tick(NULL);
        usleep(1000);
    }
    assert(receiver_runtime.output.stage == OUTPUT_IDLE && initializes == 1 && finalizes == 1);
    int result;
    waitpid(child, &result, 0);
    assert(WIFEXITED(result) && !WEXITSTATUS(result));
    assert(access(DEV_MODE, F_OK) != 0);
    close(server);
    test_restart_after_flatbuffer_clear();
    dlclose(playback_json.lib);
    puts("PASS: service Start/Stop during connect, delayed receiver ACK leaves Status/Stop "
         "responsive, fake capture lifecycle, asynchronous clear and no restart after Stop");
}

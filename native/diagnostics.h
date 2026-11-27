#ifndef HYPERDRM_DIAGNOSTICS_H
#define HYPERDRM_DIAGNOSTICS_H
#include "receiver_instances.h"
/* An independent nonblocking connection. Tests never send colors or acquire
 * capture. Work per timer iteration is bounded to keep output responsive. */
typedef enum {
    TEST_IDLE,
    TEST_CONNECT,
    TEST_SEND,
    TEST_LOGIN,
    TEST_LAYOUT,
    TEST_INSTANCES,
    TEST_SWITCH,
    TEST_FLATBUFFER,
    TEST_DONE,
    TEST_FAILED
} TestStage;
typedef struct {
    int fd;
    TestStage stage, after_send;
    TargetConfig target;
    uint64_t started, deadline, elapsed;
    unsigned leds;
    char request[384], error[128];
    size_t sent, used;
    char *response;
    LedBlend *mapping;
    ReceiverInstances *view;
    bool listed, require_layout;
} ReceiverProbe;
static inline const char *probe_stage_name(const ReceiverProbe *probe) {
    static const char *names[] = {
        "Not tested",         "Connecting", "Sending request", "Checking authentication",
        "Reading LED layout", "Reading receiver instances", "Checking receiver instance",
        "Testing FlatBuffer", "Passed", "Failed"};
    return names[probe->stage];
}
static bool probe_busy(const ReceiverProbe *probe) {
    return probe->stage >= TEST_CONNECT && probe->stage <= TEST_SWITCH;
}
static void probe_close(ReceiverProbe *probe) {
    if (probe->fd >= 0)
        close(probe->fd);
    probe->fd = -1;
    free(probe->mapping);
    probe->mapping = NULL;
    free(probe->response);
    probe->response = NULL;
    memset(probe->request, 0, sizeof(probe->request));
    memset(probe->target.token, 0, sizeof(probe->target.token));
}
static void probe_finish(ReceiverProbe *probe, const char *error, uint64_t now) {
    probe_close(probe);
    probe->elapsed = now - probe->started;
    probe->stage = error ? TEST_FAILED : TEST_DONE;
    snprintf(probe->error, sizeof(probe->error), "%s", error ? error : "");
}
static void probe_request(ReceiverProbe *probe, TestStage response, uint64_t now) {
    if (response == TEST_LOGIN)
        snprintf(probe->request, sizeof(probe->request),
                 "{\"command\":\"authorize\",\"subcommand\":\"login\",\"token\":\"%s\"}\n",
                 probe->target.token);
    else if (response == TEST_SWITCH)
        snprintf(probe->request, sizeof(probe->request),
                 "{\"command\":\"instance\",\"subcommand\":\"switchTo\",\"instance\":%u}\n",
                 probe->view->rows[probe->view->index].id);
    else
        snprintf(probe->request, sizeof(probe->request), "{\"command\":\"serverinfo\"}\n");
    probe->sent = probe->used = 0;
    probe->after_send = response;
    probe->stage = TEST_SEND;
    probe->deadline = now + 3000;
}
static bool probe_begin_view(ReceiverProbe *probe, const TargetConfig *target, uint64_t now,
                             ReceiverInstances *view, bool require_layout) {
    if (probe_busy(probe))
        return false;
    probe_close(probe);
    memset(probe, 0, sizeof(*probe));
    probe->fd = -1;
    probe->target = *target;
    probe->view = view;
    probe->require_layout = require_layout;
    if (view)
        memset(view, 0, sizeof(*view));
    probe->started = now;
    probe->response = malloc(2 * 1024 * 1024 + 1);
    if (!probe->response) {
        probe_finish(probe, "Cannot allocate connection test buffer", now);
        return true;
    }
    probe->fd = target_open(target);
    if (probe->fd < 0) {
        probe_finish(probe, "Cannot open receiver connection", now);
        return true;
    }
    probe->stage = TEST_CONNECT;
    probe->deadline = now + 3000;
    return true;
}
static inline bool probe_begin(ReceiverProbe *probe, const TargetConfig *target, uint64_t now) {
    return probe_begin_view(probe, target, now, NULL, true);
}

static void probe_instances_reply(ReceiverProbe *probe, PlaybackJson *j, void *root, uint64_t now) {
    ReceiverInstances *view = probe->view;
    if (probe->stage == TEST_SWITCH) {
        if (playback_is(j, root, "command", "instance-switchTo"))
            probe_request(probe, TEST_INSTANCES, now);
        else
            probe_finish(probe, "Receiver did not confirm the instance switch", now);
        return;
    }
    if (!playback_is(j, root, "command", "serverinfo")) {
        probe_finish(probe, "Receiver did not confirm serverinfo", now);
        return;
    }
    void *info = playback_field(j, root, "info");
    int current = receiver_byte(j, info, "currentInstance");
    if (!probe->listed) {
        if (!receiver_instances_read(view, j, info)) {
            probe_finish(probe, "Invalid receiver instance list or more than 16 instances", now);
            return;
        }
        if (probe->require_layout) {
            LedBlend *mapping = NULL;
            bool valid = layout_from_json(probe->response, &mapping, &probe->leds);
            free(mapping);
            if (!valid) {
                probe_finish(probe, "Receiver returned an invalid or empty LED layout", now);
                return;
            }
        }
        for (unsigned i = 0; i < view->count; i++) {
            DisplayInstance *instance = &view->rows[i];
            if ((int)instance->id == current && instance->source == DISPLAY_UNKNOWN &&
                !receiver_priorities_read(view, instance, j, info)) {
                probe_finish(probe, "Receiver returned invalid instance priorities", now);
                return;
            }
        }
        probe->listed = true;
    } else {
        DisplayInstance *instance = &view->rows[view->index];
        if ((current >= 0 && (unsigned)current != instance->id) ||
            !receiver_priorities_read(view, instance, j, info)) {
            probe_finish(probe, "Receiver returned invalid instance priorities", now);
            return;
        }
    }
    while (view->index < view->count && view->rows[view->index].source != DISPLAY_UNKNOWN)
        view->index++;
    if (view->index == view->count)
        probe_finish(probe, NULL, now);
    else
        probe_request(probe, TEST_SWITCH, now);
}

static void probe_tick(ReceiverProbe *probe, PlaybackJson *json, uint64_t now) {
    if (!probe_busy(probe))
        return;
    if (now >= probe->deadline || now - probe->started >= 8000) {
        probe_finish(probe, "Receiver timed out", now);
        return;
    }
    int fd = probe->fd;
    if (probe->stage == TEST_CONNECT) {
        struct pollfd p = {fd, POLLOUT, 0};
        int error = 0;
        socklen_t size = sizeof(error);
        if (poll(&p, 1, 0) <= 0)
            return;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error) {
            probe_finish(probe, "Receiver connection refused or unreachable", now);
            return;
        }
        probe_request(probe, probe->target.token[0] ? TEST_LOGIN :
                      probe->view ? TEST_INSTANCES : TEST_LAYOUT, now);
        return;
    }
    if (probe->stage == TEST_SEND) {
        size_t length = strlen(probe->request);
        ssize_t n = send(fd, probe->request + probe->sent, length - probe->sent, MSG_NOSIGNAL);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        if (n <= 0) {
            probe_finish(probe, "Receiver closed the test connection", now);
            return;
        }
        probe->sent += (size_t)n;
        if (probe->sent == length) {
            probe->stage = probe->after_send;
            probe->deadline = now + 3000;
            memset(probe->request, 0, sizeof(probe->request));
        }
        return;
    }
    size_t available = 2 * 1024 * 1024 - probe->used;
    if (available > 65536)
        available = 65536;
    if (!available) {
        probe_finish(probe, "Receiver response exceeds 2 MiB", now);
        return;
    }
    ssize_t n = recv(fd, probe->response + probe->used, available, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return;
    if (n <= 0) {
        probe_finish(probe, "Receiver closed the test connection", now);
        return;
    }
    if (memchr(probe->response + probe->used, 0, (size_t)n)) {
        probe_finish(probe, "Receiver sent an embedded NUL", now);
        return;
    }
    probe->used += (size_t)n;
    probe->response[probe->used] = 0;
    char *end = strchr(probe->response, '\n');
    if (!end)
        return;
    *end = 0;
    void *root = playback_parse_exact(json, probe->response);
    bool success = playback_true(json, root, "success");
    if (!success) {
        if (root)
            json->put(root);
        probe_finish(probe,
                     probe->stage == TEST_LOGIN
                         ? "Receiver authentication failed"
                         : probe->stage == TEST_SWITCH ? "Receiver refused the instance switch"
                         : "Receiver rejected serverinfo; check authorization",
                     now);
        return;
    }
    if (probe->stage == TEST_LOGIN) {
        json->put(root);
        probe_request(probe, probe->view ? TEST_INSTANCES : TEST_LAYOUT, now);
        return;
    }
    if (probe->view) {
        probe_instances_reply(probe, json, root, now);
        json->put(root);
        return;
    }
    json->put(root);
    LedBlend *mapping = NULL;
    unsigned count = 0;
    if (!layout_from_json(probe->response, &mapping, &count)) {
        probe_finish(probe, "Receiver returned an invalid or empty LED layout", now);
        return;
    }
    probe->leds = count;
    probe_finish(probe, NULL, now);
    probe->mapping = mapping;
}
#endif

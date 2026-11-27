#ifndef HYPERDRM_RECEIVER_INSTANCES_H
#define HYPERDRM_RECEIVER_INSTANCES_H
#include <strings.h>

#define RECEIVER_INSTANCE_LIMIT 16
#define FB_DEFAULT_PRIORITY 140
#define FB_TEST_LOW 235
#define FB_TEST_HIGH 249

typedef enum {
    DISPLAY_UNKNOWN, DISPLAY_STOPPED, DISPLAY_NONE, DISPLAY_HYPERDRM, DISPLAY_PICCAP, DISPLAY_OTHER
} DisplaySource;

typedef struct {
    unsigned id;
    int priority;
    DisplaySource source;
    char name[64], origin[64];
} DisplayInstance;

typedef struct {
    DisplayInstance rows[RECEIVER_INSTANCE_LIMIT];
    unsigned count, index;
    bool occupied[256];
} ReceiverInstances;

static inline const char *display_source_name(DisplaySource source) {
    static const char *const names[] = {"Unknown", "Stopped", "None", "HyperDRM", "PicCap", "Other"};
    return names[source];
}

static int receiver_byte(PlaybackJson *j, void *root, const char *field) {
    void *value = playback_field(j, root, field);
    if (!value || j->type(value) != 3)
        return -1;
    long n = strtol(j->string(value), NULL, 10);
    return n >= 0 && n <= 255 ? (int)n : -1;
}

static void receiver_copy_label(char *out, size_t capacity, const char *s) {
    size_t len = strnlen(s, capacity - 1);
    while (len && ((unsigned char)s[len] & 0xc0) == 0x80)
        len--;
    memcpy(out, s, len);
    out[len] = 0;
}

static bool receiver_instances_read(ReceiverInstances *view, PlaybackJson *j, void *info) {
    void *list = playback_field(j, info, "instance");
    if (!list || j->type(list) != 5 || j->length(list) > RECEIVER_INSTANCE_LIMIT)
        return false;
    bool seen[256] = {0};
    for (size_t i = 0; i < j->length(list); i++) {
        void *row = j->at(list, i);
        int id = receiver_byte(j, row, "instance");
        void *running = playback_field(j, row, "running");
        if (id < 0 || seen[id] || !running || j->type(running) != 1)
            return false;
        seen[id] = true;
        DisplayInstance *instance = &view->rows[view->count++];
        instance->id = (unsigned)id;
        instance->priority = -1;
        instance->source = j->boolean(running) ? DISPLAY_UNKNOWN : DISPLAY_STOPPED;
        void *name = playback_field(j, row, "friendly_name");
        if (name && j->type(name) == 6)
            receiver_copy_label(instance->name, sizeof(instance->name), j->string(name));
        if (!instance->name[0])
            snprintf(instance->name, sizeof(instance->name), "Instance %u", instance->id);
    }
    return true;
}

static bool receiver_origin(const char *origin, const char *name) {
    size_t length = strlen(name);
    return !strncasecmp(origin, name, length) && (!origin[length] || origin[length] == '@');
}

static bool receiver_priorities_read(ReceiverInstances *view, DisplayInstance *instance,
                                     PlaybackJson *j, void *info) {
    void *list = playback_field(j, info, "priorities");
    if (!list || j->type(list) != 5 || j->length(list) > 256)
        return false;
    instance->source = DISPLAY_NONE;
    for (size_t i = 0; i < j->length(list); i++) {
        void *row = j->at(list, i);
        int priority = receiver_byte(j, row, "priority");
        void *active = playback_field(j, row, "active");
        void *visible = playback_field(j, row, "visible");
        if (priority < 0 || !active || j->type(active) != 1 || !visible || j->type(visible) != 1)
            return false;
        view->occupied[priority] = true;
        if (!j->boolean(active) || !j->boolean(visible))
            continue;
        if (instance->priority >= 0)
            return false;
        instance->priority = priority;
        void *field = playback_field(j, row, "origin");
        const char *origin = field && j->type(field) == 6 ? j->string(field) : "";
        bool flatbuffer = playback_is(j, row, "componentId", "FLATBUFSERVER");
        bool image = playback_is(j, row, "componentId", "IMAGE");
        if ((image || flatbuffer) && receiver_origin(origin, "HyperDRM"))
            instance->source = DISPLAY_HYPERDRM;
        else if (flatbuffer && (receiver_origin(origin, "webos") || receiver_origin(origin, "PicCap")))
            instance->source = DISPLAY_PICCAP;
        else {
            instance->source = DISPLAY_OTHER;
            /* Keep the client label, leaving the receiver's peer address out of MQTT. */
            receiver_copy_label(instance->origin, sizeof(instance->origin), origin);
            char *peer = strchr(instance->origin, '@');
            if (peer)
                *peer = 0;
        }
    }
    return true;
}

static inline unsigned receiver_test_priority(const ReceiverInstances *view) {
    /* The server may clear its initial priority if it disconnects before Register. */
    if (!view->occupied[FB_DEFAULT_PRIORITY])
        for (unsigned n = FB_TEST_HIGH; n >= FB_TEST_LOW; n--)
            if (!view->occupied[n])
                return n;
    return 0;
}
#endif

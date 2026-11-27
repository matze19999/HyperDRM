#include "mqtt_config.h"

static uint64_t mqtt_status_at;
static char mqtt_last_state[12288];
static void mqtt_on_ready(void);
static void mqtt_on_command(const char *topic, const char *value);
#include "mqtt_transport.h"

static bool mqtt_settings_request(void *h, void *m, const char *body) {
    struct MqttConfig next = mqtt_config;
    if (geteuid() != 0 || !playback_json.lib)
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"MQTT setup requires root service and JSON-C\"}");
    if (!mqtt_config_parse(body, &next, false))
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Enter a broker IP, valid port and username for password authentication\"}");
    if ((!next.id[0] && !mqtt_make_id(next.id)) || (next.enabled && !mqtt_hook(true)))
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Cannot configure the MQTT boot hook or device identity\"}");
    if (!mqtt_config_save(&next)) {
        if (!mqtt_config.enabled && next.enabled)
            mqtt_hook(false);
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Cannot save MQTT settings\"}");
    }
    if (!next.enabled)
        mqtt_hook(false);
    if (memcmp(&mqtt_config, &next, sizeof(next))) {
        mqtt_close(milliseconds(), NULL);
        mqtt.retry = 0;
        mqtt_config = next;
    }
    return respond(h, m, "{\"returnValue\":true}");
}

static bool mqtt_settings(void *h, void *m, void *ctx) {
    (void)ctx;
    return mqtt_settings_request(h, m, payload(m));
}

static bool mqtt_status(void *h, void *m, void *ctx) {
    (void)ctx;
    if (geteuid() != 0) {
        struct stat saved;
        bool known_setup = lstat(MQTT_PREF, &saved) == 0 && S_ISREG(saved.st_mode) &&
                           saved.st_uid == 0 && (saved.st_mode & 0777) == 0600;
        return respond(h, m, known_setup
                                 ? "{\"returnValue\":true,\"rootRequired\":true,\"restoreAllowed\":true}"
                                 : "{\"returnValue\":true,\"rootRequired\":true}");
    }
    char user[259], body[800];
    mqtt_quote(mqtt_config.user, user);
    snprintf(body, sizeof(body),
             "{\"returnValue\":true,\"enabled\":%s,\"host\":\"%s\",\"port\":%u,"
             "\"username\":%s,\"passwordSet\":%s,\"topic\":\"%s%s\",\"connected\":%s,\"error\":\"%s\","
             "\"sourceMode\":\"%s\",\"piccapAvailable\":%s}",
             mqtt_config.enabled ? "true" : "false", mqtt_config.host, mqtt_config.port, user,
             mqtt_config.password[0] ? "true" : "false", mqtt_config.id[0] ? "hyperdrm/" : "",
             mqtt_config.id, mqtt.stage == MQTT_READY ? "true" : "false", mqtt.error ? mqtt.error : "",
             source_modes[requested_source], piccap_installed() && target_piccap_compatible(&target) ? "true" : "false");
    return respond(h, m, body);
}

static bool source_mode_request(void *h, void *m, const char *mode) {
    unsigned selection = MODE_AUTO;
    while (selection <= MODE_PICCAP && strcmp(mode, source_modes[selection]))
        selection++;
    if (selection > MODE_PICCAP || !playback_json.lib || geteuid() != 0)
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Source mode requires root and Auto, HyperDRM or PicCap\"}");
    if (selection == MODE_PICCAP && (!piccap_installed() || !target_piccap_compatible(&target)))
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"PicCap requires its installed service and a compatible receiver address\"}");
    if (selection != MODE_AUTO && (retries.blocked || CALIBRATION_ACTIVE))
        return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Finish calibration or resolve the service error before switching source\"}");
    if (selection != requested_source) {
        if (!source_save(handover_enabled, selection))
            return respond(h, m, "{\"returnValue\":false,\"errorText\":\"Cannot save source mode\"}");
        requested_source = selection;
        handover_cooldown = 0;
    }
    if (selection != MODE_AUTO)
        select_manual_source(selection, capture_ctx.handle != NULL, receiver_runtime.starting,
                             milliseconds());
    return respond(h, m, "{\"returnValue\":true}");
}

static bool source_mode(void *h, void *m, void *ctx) {
    (void)ctx;
    PlaybackJson *j = &playback_json;
    void *root = playback_parse_exact(j, payload(m));
    void *mode = playback_field(j, root, "mode");
    bool ok = source_mode_request(h, m, mode && j->type(mode) == 6 ? j->string(mode) : "");
    if (root)
        j->put(root);
    return ok;
}

static bool mqtt_topic(const char *suffix, const char *value, bool retain) {
    char topic[128];
    snprintf(topic, sizeof(topic), "hyperdrm/%s/%s", mqtt_config.id, suffix);
    return mqtt_publish(topic, value, retain);
}

#define MQTT_SWITCH "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"state_on\":\"ON\",\"state_off\":\"OFF\""
static const struct MqttEntity {
    const char *domain, *key, *name, *options;
    bool writable;
} mqtt_entities[] = {
    {"sensor", "activeSource", "Sending source", NULL, false},
    {"sensor", "displayedSource", "Displayed source", NULL, false},
    {"sensor", "inputPreview", "Recent input preview", NULL, false},
    {"sensor", "receiverState", "Receiver connection", NULL, false},
    {"sensor", "connectionTest", "Connection test", NULL, false},
    {"sensor", "error", "Last error", NULL, false},
    {"binary_sensor", "letterbox", "Black bars", "\"payload_on\":\"ON\",\"payload_off\":\"OFF\"", false},
    {"switch", "running", "Ambient lighting", MQTT_SWITCH, true},
    {"switch", "autostart", "Lighting autostart", MQTT_SWITCH, true},
    {"switch", "piccapHandover", "Automatic PicCap handover", MQTT_SWITCH, true},
    {"select", "sourceMode", "Source mode", "\"options\":[\"Auto\",\"HyperDRM\",\"PicCap\"]", true},
    {"select", "receiver", "Receiver type", "\"options\":[\"Hyperion\",\"HyperHDR\"]", true},
    {"select", "output", "Output transport", "\"options\":[\"json\",\"flatbuffer\"]", true},
    {"text", "host", "Receiver IP address", "\"min\":1,\"max\":45", true},
    {"number", "port", "Receiver JSON port", "\"min\":1,\"max\":65535,\"step\":1,\"mode\":\"box\"", true},
    {"number", "piccapPort", "PicCap FlatBuffer port", "\"min\":1,\"max\":65535,\"step\":1,\"mode\":\"box\"", true},
    {"number", "captureIntervalMs", "Capture interval", "\"min\":25,\"max\":1000,\"step\":25,\"mode\":\"box\",\"unit_of_measurement\":\"ms\"", true},
    {"number", "blackCutoff", "Black cutoff", "\"min\":0,\"max\":64,\"step\":1,\"mode\":\"box\"", true},
    {"select", "geometry", "Calibration geometry", "\"options\":[\"rim\",\"rectangles\"]", true},
    {"select", "profile", "Calibration profile", "\"options\":[\"auto\",\"g4\",\"average\",\"custom\"]", true},
    {"button", "test", "Test receiver connection", "\"payload_press\":\"PRESS\"", true},
    {"button", "clearToken", "Clear receiver API token", "\"payload_press\":\"PRESS\"", true}
};
#undef MQTT_SWITCH

static int mqtt_displayed_sources(char *out, size_t capacity) {
    const char *summary = strcmp(target.receiver, "HyperHDR") ? "Not supported" : "Unknown";
    unsigned count = display_known ? display_snapshot.count : 0;
    if (display_known) {
        DisplaySource first = DISPLAY_STOPPED;
        bool mixed = false;
        for (unsigned i = 0; i < count; i++) {
            DisplaySource source = display_snapshot.rows[i].source;
            if (source == DISPLAY_STOPPED)
                continue;
            if (first == DISPLAY_STOPPED)
                first = source;
            else if (first != source)
                mixed = true;
        }
        summary = mixed ? "Mixed" : display_source_name(first);
    }
    int n = snprintf(out, capacity, "\"displayedSource\":\"%s\",\"displayedInstances\":[", summary);
    for (unsigned i = 0; i < count; i++) {
        if (n < 0 || (size_t)n >= capacity)
            return -1;
        const DisplayInstance *row = &display_snapshot.rows[i];
        char name[2 * sizeof(row->name) + 1], origin[2 * sizeof(row->origin) + 1], priority[16] = "null";
        mqtt_quote(row->name, name);
        mqtt_quote(row->origin, origin);
        if (row->priority >= 0)
            snprintf(priority, sizeof(priority), "%d", row->priority);
        n += snprintf(out + n, capacity - (size_t)n,
                      "%s{\"id\":%u,\"name\":%s,\"source\":\"%s\",\"priority\":%s,\"origin\":%s}",
                      i ? "," : "", row->id, name, display_source_name(row->source), priority, origin);
    }
    if (n < 0 || (size_t)n + 3 >= capacity)
        return -1;
    memcpy(out + n, "]}", 3);
    return n + 2;
}

static bool mqtt_publish_state(uint64_t now) {
    char json[sizeof(mqtt_last_state)], error[515], user[259], test_error[257];
    mqtt_quote(last_error[0] ? last_error : piccap_error[0] ? piccap_error : "None", error);
    mqtt_quote(mqtt_config.user, user);
    mqtt_quote(connection_test.error, test_error);
    bool sending = capture_ctx.handle && capture_ctx.frames && capture_ctx.last_output_at &&
                   receiver_ready() &&
                   now >= capture_ctx.last_output_at && now - capture_ctx.last_output_at < 1500;
    const char *source = piccap_owned && capture_source == SOURCE_PICCAP && piccap_connected
                             ? "PicCap" : sending ? "HyperDRM" : "None";
    int n = snprintf(json, sizeof(json),
                     "{\"activeSource\":\"%s\",\"sourceMode\":\"%s\",\"receiverState\":\"%s\","
                     "\"error\":%s,\"connectionTest\":\"%s\",\"connectionTestError\":%s,\"letterbox\":\"%s\","
                     "\"running\":\"%s\",\"autostart\":\"%s\",\"piccapHandover\":\"%s\",\"inputPreview\":\"%s\","
                     "\"receiver\":\"%s\",\"host\":\"%s\",\"port\":%u,\"piccapPort\":%u,"
                     "\"captureIntervalMs\":%u,\"blackCutoff\":%u,\"output\":\"%s\",\"profile\":\"%s\",\"effectiveProfile\":\"%s\",\"geometry\":\"%s\","
                     "\"piccapInstalled\":%s,\"piccapConnected\":%s,\"tokenSet\":%s,\"mqtt\":{"
                     "\"enabled\":true,\"host\":\"%s\",\"port\":%u,\"username\":%s,\"passwordSet\":%s},"
                     "\"customProfile\":{\"model\":\"%s\",\"geometry\":\"%s\",\"points\":[",
                     source, source_modes[requested_source],
                     receiver_runtime.starting || capture_source == SOURCE_PICCAP_STARTING ? "Connecting" :
                     sending || !strcmp(source, "PicCap") ? "Connected" : retries.due ? "Retrying" : "Idle",
                     error, test_stage_name(), test_error, capture_ctx.handle && capture_ctx.letterbox_mode ? "ON" : "OFF",
                     !retries.paused && (capture_ctx.handle || receiver_runtime.starting || piccap_owned || retries.due) ? "ON" : "OFF",
                     boot_enabled() ? "ON" : "OFF", handover_enabled ? "ON" : "OFF", input_preview_status(),
                     target.receiver, target.host, target.port, target.piccap_port, target_capture_interval(&target), target.black_cutoff,
                     target_flatbuffer(&target) ? "flatbuffer" : "json",
                     calibration_key(calibration.kind), calibration_key(calibration_effective()),
                     calibration.rect_geometry ? "rectangles" : "rim",
                     piccap_installed() ? "true" : "false", piccap_connected ? "true" : "false",
                     target.token[0] ? "true" : "false", mqtt_config.host, mqtt_config.port, user,
                     mqtt_config.password[0] ? "true" : "false", calibration.model,
                     calibration.rect_geometry ? "rectangles" : "rim");
    if (n < 0 || (size_t)n >= sizeof(json) - 512)
        return false;
    for (unsigned i = 0; i < calibration.count; i++)
        n += snprintf(json + n, sizeof(json) - (size_t)n, "%s{\"position\":%.12g,\"region\":%u}",
                      i ? "," : "", calibration.points[i].position, calibration.points[i].region);
    n += snprintf(json + n, sizeof(json) - (size_t)n, "],\"rectangles\":[");
    for (unsigned i = 0; i < calibration.rect_count; i++) {
        const RegionRect *r = &calibration.rects[i];
        n += snprintf(json + n, sizeof(json) - (size_t)n,
                      "%s{\"region\":%u,\"x0\":%.12g,\"y0\":%.12g,\"x1\":%.12g,\"y1\":%.12g}",
                      i ? "," : "", r->region, r->x0, r->y0, r->x1, r->y1);
    }
    if (n < 0 || (size_t)n + 3 >= sizeof(json))
        return false;
    memcpy(json + n, "]},", 3);
    n += 3;
    if (mqtt_displayed_sources(json + n, sizeof(json) - (size_t)n) < 0)
        return false;
    if (!strcmp(mqtt_last_state, json))
        return true;
    if (!mqtt_topic("state", json, true))
        return false;
    strcpy(mqtt_last_state, json);
    return true;
}

static void mqtt_on_ready(void) {
    for (unsigned i = 0; i < sizeof(mqtt_entities) / sizeof(*mqtt_entities); i++) {
        const struct MqttEntity *e = &mqtt_entities[i];
        char topic[128], body[1200], state[512] = "", command[128] = "";
        if (strcmp(e->domain, "button"))
            snprintf(state, sizeof(state), ",\"state_topic\":\"hyperdrm/%s/state\","
                     "\"value_template\":\"{{ value_json.%s }}\"", mqtt_config.id, e->key);
        if (!strcmp(e->key, "displayedSource") || !strcmp(e->key, "connectionTest")) {
            bool displayed = !strcmp(e->key, "displayedSource");
            size_t used = strlen(state);
            snprintf(state + used, sizeof(state) - used,
                     ",\"json_attributes_topic\":\"hyperdrm/%s/state\","
                     "\"json_attributes_template\":\"{{ {'%s': value_json.%s} | tojson }}\"",
                     mqtt_config.id, displayed ? "instances" : "error",
                     displayed ? "displayedInstances" : "connectionTestError");
        }
        if (e->writable)
            snprintf(command, sizeof(command), ",\"command_topic\":\"hyperdrm/%s/set/%s\",\"retain\":false",
                     mqtt_config.id, e->key);
        snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config", e->domain, mqtt_config.id, e->key);
        snprintf(body, sizeof(body),
                 "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"availability_topic\":\"hyperdrm/%s/availability\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"HyperDRM\",\"sw_version\":\"%s\"}%s%s%s%s}",
                 e->name, mqtt_config.id, e->key, mqtt_config.id, mqtt_config.id, HYPERDRM_VERSION,
                 state, command, e->options ? "," : "", e->options ? e->options : "");
        if (!mqtt_publish(topic, body, true)) {
            mqtt_close(milliseconds(), "MQTT discovery queue is full");
            return;
        }
    }
    mqtt_last_state[0] = 0;
    mqtt_publish_state(milliseconds());
    mqtt_topic("availability", "online", true);
}

static void mqtt_target_command(const char *key, const char *value) {
    char quoted[515], body[1664];
    if (strlen(value) > 256)
        return;
    mqtt_quote(value, quoted);
    if (!strcmp(key, "clearToken") && strcmp(value, "PRESS"))
        return;
    const char *host = !strcmp(key, "host") ? quoted : NULL;
    const char *receiver = !strcmp(key, "receiver") ? quoted : NULL;
    bool number = !strcmp(key, "port") || !strcmp(key, "piccapPort") || !strcmp(key, "captureIntervalMs") ||
                  !strcmp(key, "blackCutoff");
    char digits[6];
    if (number) {
        size_t len = strlen(value);
        if (!len || len >= sizeof(digits) || strspn(value, "0123456789") != len)
            return;
        memcpy(digits, value, len + 1);
    }
    char address[95], type[33], extra[550] = "";
    mqtt_quote(target.host, address);
    mqtt_quote(target.receiver, type);
    if (!strcmp(key, "clearToken"))
        strcpy(extra, ",\"clearToken\":true");
    else if (!strcmp(key, "output")) {
        if (strcmp(value,"json") && (strcmp(value,"flatbuffer") || strcmp(target.receiver,"HyperHDR")))
            return;
        snprintf(extra, sizeof(extra), ",\"output\":%s", quoted);
    } else if (!strcmp(key, "token"))
        snprintf(extra, sizeof(extra), ",\"token\":%s", quoted);
    else if (number && strcmp(key, "port"))
        snprintf(extra, sizeof(extra), ",\"%.17s\":%.5s", key, digits);
    char port[16];
    snprintf(port, sizeof(port), "%u", target.port);
    int n = snprintf(body, sizeof(body), "{\"host\":%s,\"receiver\":%s,\"port\":%s%s}",
                     host ? host : address, receiver ? receiver : type,
                     !strcmp(key, "port") ? digits : port, extra);
    if (n > 0 && (size_t)n < sizeof(body))
        settings_request(NULL, NULL, body);
}

static void mqtt_on_command(const char *topic, const char *value) {
    char prefix[80], result[512] = "{\"returnValue\":false,\"errorText\":\"Invalid MQTT command or value\"}";
    snprintf(prefix, sizeof(prefix), "hyperdrm/%s/set/", mqtt_config.id);
    if (strncmp(prefix, topic, strlen(prefix)) || strchr(topic + strlen(prefix), '/'))
        return;
    const char *key = topic + strlen(prefix);
    bool on = !strcmp(value, "ON"), off = !strcmp(value, "OFF");
    const char *enabled = on ? "{\"enabled\":true}" : "{\"enabled\":false}";
    command_reply = result;
    if (!strcmp(key, "running")) {
        if (on && !retries.blocked) {
            if (retries.paused)
                retry_reset(&retries);
            start(NULL, NULL, NULL);
        } else if (off) {
            stop(NULL, NULL, NULL);
        }
    } else if (!strcmp(key, "autostart") && (on || off)) {
        autostart_request(NULL, NULL, enabled);
    } else if (!strcmp(key, "piccapHandover") && (on || off)) {
        handover_request(NULL, NULL, enabled);
    } else if (!strcmp(key, "sourceMode")) {
        source_mode_request(NULL, NULL, value);
    } else if (!strcmp(key, "receiverSettings")) {
        settings_request(NULL, NULL, value);
    } else if (!strcmp(key, "mqttSettings")) {
        mqtt_settings_request(NULL, NULL, value);
    } else if (!strcmp(key, "calibration")) {
        set_calibration_request(NULL, NULL, value);
    } else if (!strcmp(key, "profile") &&
               (!strcmp(value, "auto") || !strcmp(value, "g4") || !strcmp(value, "average") || !strcmp(value, "custom"))) {
        char body[64];
        snprintf(body, sizeof(body), "{\"profile\":\"%.7s\"}", value);
        set_calibration_request(NULL, NULL, body);
    } else if (!strcmp(key, "geometry") && (!strcmp(value, "rim") || !strcmp(value, "rectangles"))) {
        char body[72];
        snprintf(body, sizeof(body), "{\"profile\":\"%s\",\"geometry\":\"%.10s\"}",
                 calibration_key(calibration.kind), value);
        set_calibration_request(NULL, NULL, body);
    } else if (!strcmp(key, "test") && !strcmp(value, "PRESS")) {
        if (test_begin(&target, milliseconds()))
            strcpy(result, "{\"returnValue\":true}");
    } else if (!strcmp(key, "host") || !strcmp(key, "receiver") || !strcmp(key, "port") ||
               !strcmp(key, "piccapPort") || !strcmp(key, "captureIntervalMs") || !strcmp(key, "blackCutoff") ||
               !strcmp(key, "token") || !strcmp(key, "clearToken") || !strcmp(key, "output")) {
        mqtt_target_command(key, value);
    }
    command_reply = NULL;
    if (mqtt.stage == MQTT_READY) {
        mqtt_topic("result", result, false);
        mqtt_status_at = 0;
    }
}

static void mqtt_service_tick(uint64_t now) {
    mqtt_tick(now);
    if (mqtt.stage == MQTT_READY && now >= mqtt_status_at) {
        if (mqtt_publish_state(now))
            mqtt_status_at = now + 500;
        else
            mqtt_close(now, "MQTT state queue is full");
    }
}

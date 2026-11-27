#define main service_entry_point
#define TARGET_PREF "/tmp/hyperdrm-mqtt-test-target.json"
#include "service.c"
#undef main
#include <assert.h>

static unsigned discovery, states, results;
static char state_json[4096], result_json[512];

static void check_publications(int broker) {
    unsigned char data[32768];
    ssize_t n = recv(broker, data, sizeof(data), MSG_DONTWAIT);
    assert(n > 0);
    for (size_t start = 0; start < (size_t)n;) {
        unsigned flags = data[start++];
        assert(flags == 0x30 || flags == 0x31);
        size_t length = 0;
        for (unsigned shift = 0;; shift += 7) {
            assert(start < (size_t)n && shift <= 21);
            unsigned digit = data[start++];
            length |= (digit & 127) << shift;
            if (!(digit & 128))
                break;
        }
        assert(length >= 2 && start + length <= (size_t)n);
        size_t topic_len = ((unsigned)data[start] << 8) | data[start + 1];
        assert(topic_len < 128 && topic_len + 2 <= length);
        char topic[128], body[4096];
        size_t body_len = length - topic_len - 2;
        assert(body_len < sizeof(body));
        memcpy(topic, data + start + 2, topic_len);
        topic[topic_len] = 0;
        memcpy(body, data + start + topic_len + 2, body_len);
        body[body_len] = 0;
        assert(!strstr(body, "my-test-password") && !strstr(body, "my-receiver-token"));
        if (!strncmp(topic, "homeassistant/", 14)) {
            void *json = playback_parse_exact(&playback_json, body);
            assert(flags == 0x31 && json && playback_field(&playback_json, json, "unique_id"));
            if (strstr(topic, "/displayedSource/"))
                assert(playback_field(&playback_json, json, "json_attributes_topic") &&
                       strstr(body, "value_json.displayedInstances"));
            playback_json.put(json);
            discovery++;
        } else if (strstr(topic, "/state")) {
            assert(flags == 0x31);
            strcpy(state_json, body);
            states++;
        } else if (strstr(topic, "/result")) {
            assert(flags == 0x30);
            assert(body_len < sizeof(result_json));
            strcpy(result_json, body);
            results++;
        } else {
            assert(strstr(topic, "/availability") && flags == 0x31 && !strcmp(body, "online"));
        }
        start += length;
    }
}

static void send_interval(int broker, unsigned char flags, const char *value) {
    unsigned char frame[128];
    size_t n = mqtt_string(frame + 2, "hyperdrm/hdr-0123456789abcdef/set/captureIntervalMs");
    size_t length = strlen(value);
    memcpy(frame + 2 + n, value, length);
    frame[0] = flags;
    frame[1] = n + length;
    assert(send(broker, frame, n + length + 2, 0) == (ssize_t)(n + length + 2));
}

int main(void) {
    assert(playback_json_init(&playback_json));
    struct MqttConfig next = mqtt_config;
    assert(mqtt_config_parse("{\"enabled\":true,\"username\":\"user\",\"password\":\"my-test-password\"}", &next, false));
    struct MqttConfig invalid = next;
    assert(!mqtt_config_parse("{\"port\":null}", &invalid, false));
    mqtt_config = next;
    strcpy(mqtt_config.id, "hdr-0123456789abcdef");
    strcpy(target.token, "my-receiver-token");
    mqtt_connect_packet();
    assert(mqtt.out[0] == 0x10 && mqtt.out[9] == 0xe6);
    assert(memmem(mqtt.out, mqtt.out_len, "offline", 7) != NULL);
    mqtt.out_len = 0;

    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets) == 0);
    mqtt.fd = sockets[0];
    mqtt.stage = MQTT_CONNACK;
    uint64_t now = milliseconds();
    unsigned char ack[] = {0x20, 2, 0, 0};
    assert(send(sockets[1], ack, 1, 0) == 1);
    mqtt_service_tick(now);
    assert(mqtt.stage == MQTT_CONNACK);
    assert(send(sockets[1], ack + 1, 3, 0) == 3);
    mqtt_service_tick(now + 25);
    assert(mqtt.stage == MQTT_SUBACK && mqtt.out[0] == 0x82);
    mqtt_service_tick(now + 50);
    unsigned char subscription[128];
    assert(recv(sockets[1], subscription, sizeof(subscription), 0) > 0);
    assert(subscription[0] == 0x82);
    unsigned char suback[] = {0x90, 3, 0, 1, 0};
    assert(send(sockets[1], suback, sizeof(suback), 0) == sizeof(suback));
    mqtt_service_tick(now + 75);
    mqtt_service_tick(now + 100);
    check_publications(sockets[1]);
    assert(discovery == sizeof(mqtt_entities) / sizeof(*mqtt_entities) && states == 1);
    assert(strstr(state_json, "\"activeSource\":\"None\""));
    assert(strstr(state_json, "\"displayedSource\":\"Not supported\""));
    TargetConfig default_target = target;
    strcpy(target.receiver, "HyperHDR");
    char visible[2048];
    assert(mqtt_displayed_sources(visible, sizeof(visible)) > 0);
    assert(strstr(visible, "\"displayedSource\":\"Unknown\""));
    display_snapshot.rows[0] = (DisplayInstance){.id = 0, .source = DISPLAY_HYPERDRM,
                                                .priority = 149, .name = "TV"};
    display_snapshot.rows[1] = (DisplayInstance){.id = 3, .source = DISPLAY_OTHER,
                                                .priority = 60, .name = "Desk \"back\"", .origin = "Effect"};
    display_snapshot.count = 2;
    display_known = true;
    char json_visible[2050];
    assert(mqtt_displayed_sources(json_visible + 1, sizeof(json_visible) - 1) > 0);
    json_visible[0] = '{';
    void *visible_root = playback_parse_exact(&playback_json, json_visible);
    assert(playback_is(&playback_json, visible_root, "displayedSource", "Mixed"));
    void *instances = playback_field(&playback_json, visible_root, "displayedInstances");
    assert(instances && playback_json.length(instances) == 2);
    assert(playback_is(&playback_json, playback_json.at(instances, 1), "name", "Desk \"back\""));
    assert(playback_is(&playback_json, playback_json.at(instances, 1), "origin", "Effect"));
    playback_json.put(visible_root);
    display_reset();
    target = default_target;

    send_interval(sockets[1], 0x31, "100");
    mqtt_service_tick(now + 125);
    assert(target_capture_interval(&target) == 50 && !mqtt.out_len);
    send_interval(sockets[1], 0x30, "26");
    mqtt_service_tick(now + 150);
    mqtt_service_tick(now + 175);
    check_publications(sockets[1]);
    assert(results == 1 && strstr(result_json, "\"returnValue\":false"));
    assert(target_capture_interval(&target) == 50);
    send_interval(sockets[1], 0x30, "100");
    mqtt_service_tick(now + 200);
    mqtt_service_tick(now + 225);
    check_publications(sockets[1]);
    assert(results == 2 && states == 2 && strstr(result_json, "\"returnValue\":true"));
    assert(strstr(state_json, "\"captureIntervalMs\":100"));
    TargetConfig loaded = target;
    loaded.capture_interval = 50;
    assert(target_load(&playback_json, TARGET_PREF, &loaded));
    assert(loaded.capture_interval == 100);
    unlink(TARGET_PREF);

    close(sockets[1]);
    mqtt_service_tick(now + 250);
    assert(mqtt.stage == MQTT_DOWN && mqtt.retry == now + 5250);
    puts("PASS: MQTT handshake, discovery, private credentials, retained command rejection, validated settings round trip and disconnect");
}

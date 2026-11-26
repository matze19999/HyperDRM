/* Bounded, nonblocking MQTT 3.1.1 transport. Commands use QoS 0 and clean sessions. */
enum { MQTT_DOWN, MQTT_TCP, MQTT_CONNACK, MQTT_SUBACK, MQTT_READY };
static struct {
    int fd, stage;
    uint64_t retry, deadline, ping;
    size_t in_len, out_len;
    unsigned char in[8192], out[32768];
    const char *error;
} mqtt = {.fd = -1};

static void mqtt_close(uint64_t now, const char *error) {
    if (mqtt.fd >= 0)
        close(mqtt.fd);
    mqtt.fd = -1;
    mqtt.stage = MQTT_DOWN;
    mqtt.in_len = mqtt.out_len = 0;
    mqtt.retry = now + 5000;
    mqtt.error = error;
}

static bool mqtt_packet(unsigned char type, const void *prefix, size_t n,
                        const void *body, size_t length) {
    if (n + length + 5 > sizeof(mqtt.out) - mqtt.out_len)
        return false;
    unsigned char *p = mqtt.out + mqtt.out_len;
    *p++ = type;
    size_t remaining = n + length;
    do {
        *p++ = (remaining & 127) | (remaining >= 128 ? 128 : 0);
        remaining >>= 7;
    } while (remaining);
    if (n)
        memcpy(p, prefix, n);
    if (length)
        memcpy(p + n, body, length);
    mqtt.out_len = p - mqtt.out + n + length;
    return true;
}

static size_t mqtt_string(unsigned char *out, const char *s) {
    size_t n = strlen(s);
    out[0] = n >> 8;
    out[1] = n & 255;
    memcpy(out + 2, s, n);
    return n + 2;
}

static bool mqtt_publish(const char *topic, const char *data, bool retain) {
    unsigned char header[256];
    if (strlen(topic) > sizeof(header) - 2)
        return false;
    size_t n = mqtt_string(header, topic);
    return mqtt_packet(retain ? 0x31 : 0x30, header, n, data, strlen(data));
}

static void mqtt_connect_packet(void) {
    unsigned char data[640] = {0, 4, 'M', 'Q', 'T', 'T', 4, 0x26, 0, 20};
    if (mqtt_config.user[0])
        data[7] |= 0x80;
    if (mqtt_config.password[0])
        data[7] |= 0x40;
    size_t n = 10;
    n += mqtt_string(data + n, mqtt_config.id);
    char topic[96];
    snprintf(topic, sizeof(topic), "hyperdrm/%s/availability", mqtt_config.id);
    n += mqtt_string(data + n, topic);
    n += mqtt_string(data + n, "offline");
    if (mqtt_config.user[0])
        n += mqtt_string(data + n, mqtt_config.user);
    if (mqtt_config.password[0])
        n += mqtt_string(data + n, mqtt_config.password);
    mqtt_packet(0x10, data, n, NULL, 0);
}

static bool mqtt_message(unsigned char flags, const unsigned char *body, size_t n, uint64_t now) {
    if (mqtt.stage == MQTT_CONNACK) {
        if (flags != 0x20 || n != 2 || body[0] || body[1])
            return false;
        char topic[96];
        unsigned char request[100] = {0, 1};
        snprintf(topic, sizeof(topic), "hyperdrm/%s/set/+", mqtt_config.id);
        size_t size = 2 + mqtt_string(request + 2, topic);
        request[size++] = 0;
        mqtt_packet(0x82, request, size, NULL, 0);
        mqtt.stage = MQTT_SUBACK;
        return true;
    }
    if (mqtt.stage == MQTT_SUBACK) {
        if (flags != 0x90 || n != 3 || body[0] || body[1] != 1 || body[2] != 0)
            return false;
        mqtt.stage = MQTT_READY;
        mqtt.deadline = 0;
        mqtt.ping = now + 10000;
        mqtt.error = NULL;
        mqtt_on_ready();
        return true;
    }
    if (flags == 0xd0 && n == 0) {
        mqtt.deadline = 0;
        return true;
    }
    if ((flags & 0xf6) != 0x30 || n < 2)
        return false;
    size_t topic_len = ((unsigned)body[0] << 8) | body[1];
    if (!topic_len || topic_len > n - 2)
        return false;
    if (flags & 1)
        return true;
    char topic[128], value[4097];
    size_t value_len = n - 2 - topic_len;
    if (topic_len >= sizeof(topic) || value_len >= sizeof(value) ||
        memchr(body + 2, 0, n - 2))
        return true;
    memcpy(topic, body + 2, topic_len);
    topic[topic_len] = 0;
    memcpy(value, body + 2 + topic_len, value_len);
    value[value_len] = 0;
    mqtt_on_command(topic, value);
    return true;
}

static void mqtt_tick(uint64_t now) {
    if (!mqtt_config.enabled)
        return;
    if (mqtt.stage == MQTT_DOWN) {
        if (now < mqtt.retry)
            return;
        TargetConfig endpoint = target;
        strcpy(endpoint.host, mqtt_config.host);
        endpoint.port = mqtt_config.port;
        mqtt.fd = target_open(&endpoint);
        if (mqtt.fd < 0) {
            mqtt_close(now, "Cannot reach MQTT broker");
            return;
        }
        mqtt.stage = MQTT_TCP;
        mqtt.deadline = now + 5000;
    }
    if (mqtt.deadline && now >= mqtt.deadline) {
        mqtt_close(now, "MQTT broker did not reply");
        return;
    }
    if (mqtt.stage == MQTT_TCP) {
        struct pollfd p = {.fd = mqtt.fd, .events = POLLOUT};
        if (poll(&p, 1, 0) <= 0)
            return;
        int error;
        socklen_t len = sizeof(error);
        if (getsockopt(mqtt.fd, SOL_SOCKET, SO_ERROR, &error, &len) || error) {
            mqtt_close(now, "Cannot reach MQTT broker");
            return;
        }
        mqtt_connect_packet();
        mqtt.stage = MQTT_CONNACK;
    }
    if (mqtt.stage == MQTT_READY && !mqtt.deadline && now >= mqtt.ping) {
        if (mqtt_packet(0xc0, NULL, 0, NULL, 0)) {
            mqtt.ping = now + 10000;
            mqtt.deadline = now + 5000;
        }
    }
    if (mqtt.out_len) {
        ssize_t n = send(mqtt.fd, mqtt.out, mqtt.out_len, MSG_NOSIGNAL);
        if (n > 0) {
            mqtt.out_len -= (size_t)n;
            memmove(mqtt.out, mqtt.out + n, mqtt.out_len);
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            mqtt_close(now, "MQTT send failed");
            return;
        }
    }
    if (mqtt.in_len < sizeof(mqtt.in)) {
        ssize_t n = recv(mqtt.fd, mqtt.in + mqtt.in_len, sizeof(mqtt.in) - mqtt.in_len, 0);
        if (n > 0)
            mqtt.in_len += (size_t)n;
        else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            mqtt_close(now, "MQTT broker disconnected");
            return;
        }
    }
    for (unsigned count = 0; count < 8 && mqtt.in_len >= 2; count++) {
        size_t length = 0, pos = 1;
        unsigned char digit;
        do {
            if (pos == mqtt.in_len)
                return;
            digit = mqtt.in[pos];
            length |= (size_t)(digit & 127) << (7 * (pos - 1));
            pos++;
        } while ((digit & 128) && pos <= 4);
        if ((digit & 128) || length > sizeof(mqtt.in) - pos) {
            mqtt_close(now, "Invalid MQTT packet size");
            return;
        }
        if (length + pos > mqtt.in_len)
            return;
        if (!mqtt_message(mqtt.in[0], mqtt.in + pos, length, now)) {
            mqtt_close(now, "MQTT protocol error or broker rejected login/subscription");
            return;
        }
        if (mqtt.stage == MQTT_DOWN)
            return; /* A broker settings command can close this connection. */
        mqtt.in_len -= length + pos;
        memmove(mqtt.in, mqtt.in + pos + length, mqtt.in_len);
    }
}

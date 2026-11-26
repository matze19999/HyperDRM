#ifndef MQTT_PREF
#define MQTT_PREF "/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service/mqtt.json"
#endif
#define MQTT_HOOK BOOT_DIRECTORY "/61-hyperdrm-mqtt"
static struct MqttConfig {
    bool enabled;
    char host[46], user[129], password[257], id[24];
    unsigned port;
} mqtt_config = {.host = "127.0.0.1", .port = 1883};

static bool mqtt_make_id(char id[24]) {
    unsigned char bytes[8];
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    ssize_t n = read(fd, bytes, sizeof(bytes));
    close(fd);
    if (n != sizeof(bytes))
        return false;
    memcpy(id, "hdr-", 4);
    for (unsigned i = 0; i < sizeof(bytes); i++)
        snprintf(id + 4 + i * 2, 3, "%02x", bytes[i]);
    return true;
}

/* Callers allocate twice the maximum input length plus room for quotes and NUL. */
static void mqtt_quote(const char *s, char *out) {
    *out++ = '"';
    for (; *s; s++) {
        unsigned char c = *s;
        if (c < 32)
            continue;
        if (c == '"' || c == '\\')
            *out++ = '\\';
        *out++ = c;
    }
    *out++ = '"';
    *out = 0;
}

static bool mqtt_config_parse(const char *body, struct MqttConfig *cfg, bool from_file) {
    PlaybackJson *j = &playback_json;
    void *root = playback_parse_exact(j, body);
    if (!root)
        return false;
    bool valid = j->type(root) == 4;
    const char *keys[] = {"host", "username", "password", "id"};
    char *fields[] = {cfg->host, cfg->user, cfg->password, cfg->id};
    size_t sizes[] = {sizeof(cfg->host), sizeof(cfg->user), sizeof(cfg->password), sizeof(cfg->id)};
    for (unsigned i = 0; valid && i < (from_file ? 4u : 3u); i++) {
        void *v = NULL;
        if (!j->get(root, keys[i], &v))
            continue;
        const char *s = v && j->type(v) == 6 ? j->string(v) : NULL;
        if (!s || strlen(s) >= sizes[i] || strcspn(s, "\r\n\t") != strlen(s)) {
            valid = false;
            break;
        }
        for (const unsigned char *p = (const unsigned char *)s; *p; p++)
            if (*p < 32 || *p >= 127)
                valid = false;
        strcpy(fields[i], s);
    }
    void *v = NULL;
    if (valid && j->get(root, "enabled", &v)) {
        valid = v && j->type(v) == 1;
        if (valid)
            cfg->enabled = j->boolean(v);
    }
    if (valid && j->get(root, "port", &v)) {
        long port = v && j->type(v) == 3 ? strtol(j->string(v), NULL, 10) : 0;
        valid = port >= 1 && port <= 65535;
        if (valid)
            cfg->port = (unsigned)port;
    }
    j->put(root);
    unsigned char ip[16];
    return valid && (!cfg->password[0] || cfg->user[0]) &&
           (inet_pton(AF_INET, cfg->host, ip) == 1 || inet_pton(AF_INET6, cfg->host, ip) == 1) &&
           (!from_file || (strlen(cfg->id) == 20 && !strncmp(cfg->id, "hdr-", 4) &&
             strspn(cfg->id + 4, "0123456789abcdef") == 16));
}

static bool mqtt_config_save(const struct MqttConfig *cfg) {
    char user[259], password[515], body[1024];
    mqtt_quote(cfg->user, user);
    mqtt_quote(cfg->password, password);
    int n = snprintf(body, sizeof(body),
                     "{\"enabled\":%s,\"host\":\"%s\",\"port\":%u,\"username\":%s,"
                     "\"password\":%s,\"id\":\"%s\"}\n",
                     cfg->enabled ? "true" : "false", cfg->host, cfg->port, user, password, cfg->id);
    return n > 0 && (size_t)n < sizeof(body) &&
           atomic_replace(MQTT_PREF, body, (size_t)n) != SAVE_UNCHANGED;
}

static void mqtt_config_load(void) {
    int fd = open(MQTT_PREF, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return;
    char body[1025];
    ssize_t n = read(fd, body, sizeof(body) - 1);
    close(fd);
    if (n > 0 && n < (ssize_t)sizeof(body) - 1) {
        body[n] = 0;
        struct MqttConfig next = mqtt_config;
        if (mqtt_config_parse(body, &next, true))
            mqtt_config = next;
    }
}

/* Independently wake MQTT at boot even when ambient lighting autostart is off. */
static bool mqtt_hook(bool enable) {
    char path[sizeof(BOOT_SCRIPT)];
    ssize_t n = readlink(MQTT_HOOK, path, sizeof(path));
    if (n >= 0) {
        if (n != sizeof(BOOT_SCRIPT) - 1 || memcmp(path, BOOT_SCRIPT, (size_t)n))
            return false;
        return enable || unlink(MQTT_HOOK) == 0;
    }
    if (errno != ENOENT)
        return false;
    if (!enable)
        return true;
    return access(BOOT_SCRIPT, X_OK) == 0 &&
           (mkdir(BOOT_DIRECTORY, 0755) == 0 || errno == EEXIST) &&
           symlink(BOOT_SCRIPT, MQTT_HOOK) == 0;
}

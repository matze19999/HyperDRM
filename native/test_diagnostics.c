#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <sys/wait.h>
#include "target.h"
#include "layout.h"
#include "diagnostics.h"
#include "flatbuffer_output.h"
#include "connection_check.h"
static void fixture(PlaybackJson *json,int kind){
 int server=socket(AF_INET,SOCK_STREAM,0);assert(server>=0);
 struct sockaddr_in address={.sin_family=AF_INET,.sin_addr={htonl(INADDR_LOOPBACK)}};
 assert(bind(server,(void*)&address,sizeof(address))==0 && listen(server,1)==0);
 socklen_t length=sizeof(address);assert(getsockname(server,(void*)&address,&length)==0);
 TargetConfig config=target;config.port=ntohs(address.sin_port);if(kind==1)strcpy(config.token,"fixture-token");
 pid_t child=fork();assert(child>=0);
 if(!child){
  int fd=accept(server,NULL,NULL);if(fd<0)_exit(2);char request[512]={0};size_t used=0;
  while(used+1<sizeof(request)){ssize_t n=read(fd,request+used,sizeof(request)-used-1);if(n<=0)_exit(3);used+=(size_t)n;if(strchr(request,'\n'))break;}
  if(!strstr(request,kind==1?"authorize":"serverinfo") || strstr(request,"color") || strstr(request,"image"))_exit(4);
  const char *response=kind==1?"{\"success\":false}\n":kind==2?"{\"success\":true,\"info\":{\"leds\":[]}}\n":"{\"success\":true,\"info\":{\"leds\":[{\"hmin\":0,\"hmax\":1,\"vmin\":0,\"vmax\":0.1}]}}\n";
  if(write(fd,response,7)!=7)_exit(5);
  usleep(20000);
  if(write(fd,response+7,strlen(response)-7)!=(ssize_t)strlen(response)-7)_exit(6);
  close(fd);close(server);_exit(0);
 }
 assert(test_begin(&config,1000));assert(!test_begin(&config,1000));
 for(unsigned i=0;test_busy() && i<400;i++){test_tick(json,1000+i*10);usleep(1000);}
 assert(!test_busy());assert(connection_test.stage==(kind?TEST_FAILED:TEST_DONE));
 if(!kind)assert(connection_test.leds==1 && !connection_test.error[0]);
 if(kind==1)assert(strstr(connection_test.error,"authentication"));
 if(kind==2)assert(strstr(connection_test.error,"layout"));
 int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
 close(server);assert(test_begin(&config,10000));test_tick(json,10010);assert(connection_test.stage==TEST_FAILED);
}

static int listener(unsigned *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr = {htonl(INADDR_LOOPBACK)}};
    socklen_t length = sizeof(addr);
    assert(!bind(fd, (void *)&addr, sizeof(addr)) && !listen(fd, 1));
    assert(!getsockname(fd, (void *)&addr, &length));
    *port = ntohs(addr.sin_port);
    return fd;
}

static void json_exchange(int fd, const char *command, const char *response) {
    char request[512] = {0};
    for (size_t i = 0; i + 1 < sizeof(request); i++) {
        assert(read(fd, request + i, 1) == 1);
        if (request[i] == '\n')
            break;
    }
    assert(strstr(request, command));
    size_t len = strlen(response);
    assert(write(fd, response, len) == (ssize_t)len);
}

static void flatbuffer_fixture(PlaybackJson *json, unsigned scenario) {
    TargetConfig config = target;
    strcpy(config.receiver, "HyperHDR");
    strcpy(config.output, "flatbuffer");
    strcpy(config.token, "fixture-token");
    int listener_json = listener(&config.port), listener_fb = listener(&config.piccap_port);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        alarm(5);
        if (scenario == 6)
            close(listener_fb);
        int fd = accept(listener_json, NULL, NULL);
        assert(fd >= 0);
        json_exchange(fd, "\"token\":\"fixture-token\"", "{\"success\":true}\n");
        json_exchange(fd, "\"serverinfo\"",
                      "{\"success\":true,\"command\":\"serverinfo\",\"info\":{\"currentInstance\":0,"
                      "\"instance\":[{\"instance\":0,\"running\":true,\"friendly_name\":\"TV\"},"
                      "{\"instance\":3,\"running\":true,\"friendly_name\":\"Desk\"},"
                      "{\"instance\":5,\"running\":false}],"
                      "\"leds\":[{\"hmin\":0,\"hmax\":1,\"vmin\":0,\"vmax\":0.1}],"
                      "\"priorities\":[{\"priority\":149,\"active\":true,\"visible\":true,"
                      "\"componentId\":\"IMAGE\",\"origin\":\"HyperDRM@127.0.0.1\"}]}}\n");
        if (scenario == 4) {
            json_exchange(fd, "\"instance\":3", "{\"success\":false,\"command\":\"instance-switchTo\"}\n");
            close(fd);
            _exit(0);
        }
        json_exchange(fd, "\"instance\":3", "{\"success\":true,\"command\":\"instance-switchTo\"}\n");
        char response[512];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"command\":\"serverinfo\",\"info\":{\"currentInstance\":3,"
                 "\"priorities\":[{\"priority\":150,\"active\":true,\"visible\":true,"
                 "\"componentId\":\"FLATBUFSERVER\",\"origin\":\"webos@127.0.0.1\"},"
                 "{\"priority\":%u,\"active\":false,\"visible\":false}]}}\n",
                 scenario == 3 ? FB_DEFAULT_PRIORITY : FB_TEST_HIGH);
        json_exchange(fd, "\"serverinfo\"", response);
        close(fd);
        if (scenario == 3 || scenario == 5 || scenario == 6)
            _exit(0);
        fd = accept(listener_fb, NULL, NULL);
        assert(fd >= 0);
        uint8_t data[512], sent[512];
        FlatBuffer builder = {.buf = data, .capacity = sizeof(data)};
        size_t size = fb_encode_register(&builder, "HyperDRM", FB_TEST_HIGH - 1);
        assert(size < 255 && size + 4 <= sizeof(sent));
        for (size_t i = 0; i < size + 4; i++)
            assert(read(fd, sent + i, 1) == 1);
        assert(sent[0] == 0 && sent[1] == 0 && sent[2] == 0 && sent[3] == size);
        assert(!memcmp(sent + 4, data + sizeof(data) - size, size));
        if (scenario != 2) {
            uint8_t reply[] = {0,0,0,24, 16,0,0,0, 10,0,8,0, 0,0,0,0, 4,0,0,0,
                               12,0,0,0, (uint8_t)(scenario ? FB_TEST_HIGH : FB_TEST_HIGH - 1),0,0,0};
            assert(write(fd, reply, sizeof(reply)) == sizeof(reply));
        }
        assert(read(fd, sent, 1) == 0);
        close(fd);
        _exit(0);
    }
    if (scenario == 6)
        close(listener_fb);
    if (scenario == 5) {
        int status;
        close(listener_fb);
        assert(probe_begin_view(&display_probe, &config, 1000, &display_pending, false));
        for (unsigned i = 0; probe_busy(&display_probe) && i < 600; i++) {
            probe_tick(&display_probe, json, 1000 + i * 10);
            usleep(1000);
        }
        assert(display_probe.stage == TEST_DONE && display_pending.count == 3);
        assert(display_pending.rows[0].source == DISPLAY_HYPERDRM && display_pending.rows[0].priority == 149);
        assert(display_pending.rows[1].source == DISPLAY_PICCAP && display_pending.rows[1].priority == 150);
        assert(display_pending.rows[2].source == DISPLAY_STOPPED && display_pending.rows[2].priority == -1);
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        close(listener_json);
        display_reset();
        return;
    }
    assert(test_begin(&config, 1000) && !test_begin(&config, 1000));
    for (unsigned i = 0; test_busy() && i < 1200; i++) {
        test_tick(json, 1000 + i * 10);
        usleep(1000);
    }
    assert(connection_test.stage == (scenario ? TEST_FAILED : TEST_DONE));
    if (!scenario)
        assert(connection_test.leds == 1 && !connection_test.error[0]);
    else if (scenario == 1)
        assert(strstr(connection_test.error, "confirm"));
    else if (scenario == 2)
        assert(strstr(connection_test.error, "registration timed out"));
    else if (scenario == 3)
        assert(strstr(connection_test.error, "No safe"));
    else if (scenario == 4)
        assert(strstr(connection_test.error, "refused the instance"));
    else
        assert(strstr(connection_test.error, "connection refused"));
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (scenario != 6) {
        struct pollfd fd = {.fd = listener_fb, .events = POLLIN};
        assert(poll(&fd, 1, 0) == 0);
        close(listener_fb);
    }
    close(listener_json);
}

int main(void){
 (void)target_save;(void)target_load;(void)target_open;(void)test_stage_name;
 (void)fb_out_frame;(void)fb_out_stop;(void)display_tick;
 PlaybackJson json={0};assert(playback_json_init(&json));
 fixture(&json,0);fixture(&json,1);fixture(&json,2);
 int peers[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,peers)==0);
 connection_test.fd=peers[0];connection_test.stage=TEST_LAYOUT;connection_test.started=20000;connection_test.deadline=23000;
 test_tick(&json,23000);assert(connection_test.stage==TEST_FAILED && strstr(connection_test.error,"timed out"));close(peers[1]);
 for (unsigned scenario = 0; scenario < 7; scenario++)
     flatbuffer_fixture(&json, scenario);
 test_close();
 dlclose(json.lib);
 puts("PASS: JSON diagnostics, FlatBuffer registration, safe priorities, timeouts and per-instance source detection");return 0;
}

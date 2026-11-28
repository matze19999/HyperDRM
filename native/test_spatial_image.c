#include <assert.h>
#include <string.h>
#include "dark_color.h"
#include "spatial_image.h"

static const unsigned char *pixel(const unsigned char *plane, double x, double y) {
    return &plane[3 * ((unsigned)(y * SPATIAL_HEIGHT) * SPATIAL_WIDTH + (unsigned)(x * SPATIAL_WIDTH))];
}

static void test_g4_overlap(void) {
    /* Recorded SDR field on the upper left side. Its signal appears in the
     * neighbouring top region and in several overlapping left regions. */
    unsigned char measured[16][3] = {
        {26,26,26}, {26,26,26}, {26,26,26}, {29,29,27}, {36,34,29}, {25,25,25},
        {56,50,35}, {71,62,42}, {26,26,26}, {26,26,26}, {26,26,26}, {25,25,25},
        {25,25,25}, {25,25,25}, {24,24,24}, {47,42,32},
    };
    unsigned char plane[SPATIAL_PIXELS * 3];
    assert(spatial_g4_overlap(measured, plane, 16));
    assert(pixel(plane, .02, .25)[0] > 40);
    assert(pixel(plane, .25, .02)[0] <= 17);
    assert(pixel(plane, .02, .45)[0] <= 17);
    assert(pixel(plane, .02, .75)[0] <= 17);

    /* Another model reports the same scene through a different set of
     * histogram indices. Its saved response must drive the renderer. */
    EdgeResponse response;
    unsigned char own_regions[16][3];
    for (unsigned r = 0; r < 16; ++r) {
        unsigned slot = (r + 5) % 16;
        memcpy(own_regions[slot], measured[r], 3);
        for (unsigned i = 0; i < EDGE_RESPONSE_ROWS; ++i)
            response[i][slot] = g4_response[i][r];
    }
    assert(spatial_edge_response(own_regions, plane, 16, &response, 1));
    assert(pixel(plane, .02, .25)[0] > 40 && pixel(plane, .25, .02)[0] <= 17);
    char *raw = spatial_rgb_command_format(plane, 149, 1500, "");
    assert(raw && strstr(raw, "\"format\":\"\""));
    free(raw);
    memcpy(response, g4_response, sizeof(response));
    assert(spatial_edge_response(measured, plane, 16, &response, 2));
    assert(pixel(plane, .02, .25)[0] > 40);

    /* Idealized simultaneous accents: green on the lower left side and blue
     * along the bottom. The generated plane must preserve both locations. */
    unsigned char mixed[16][3] = {{0}};
    for (unsigned r = 0; r < 16; r++) {
        mixed[r][1] = (g4_response[3 * EDGE_SAMPLES + 5][r] * 100 + 500) / 1000;
        mixed[r][2] = (g4_response[2 * EDGE_SAMPLES + 1][r] * 80 + 500) / 1000;
    }
    assert(spatial_g4_overlap(mixed, plane, 0));
    assert(pixel(plane, .02, .75)[1] > 90 && pixel(plane, .02, .75)[2] < 5);
    assert(pixel(plane, .25, .98)[2] > 70 && pixel(plane, .25, .98)[1] < 5);
    assert(pixel(plane, .25, .02)[1] == 0 && pixel(plane, .25, .02)[2] == 0);

    for (unsigned r = 0; r < 16; ++r)
        for (unsigned c = 0; c < 3; ++c)
            mixed[r][c] = (g4_response[EDGE_SAMPLES + 1][r] * 50 +
                           g4_response[EDGE_SAMPLES + 5][r] * 50 + 500) / 1000;
    assert(spatial_g4_overlap(mixed, plane, 0));
    assert(pixel(plane, .98, .25)[0] > 44 && pixel(plane, .98, .75)[0] > 44);
    assert(pixel(plane, .98, .50)[0] < 3 && pixel(plane, .75, .02)[0] < 3);

    /* G4 reports practically the same histogram from either lower right
     * edge. A three-sided strip must still light up next to the corner. */
    for (unsigned r = 0; r < 16; ++r)
        for (unsigned c = 0; c < 3; ++c)
            mixed[r][c] = (g4_response[3 * EDGE_SAMPLES - 1][r] * 80 + 500) / 1000;
    assert(spatial_g4_overlap(mixed, plane, 0));
    assert(pixel(plane, .98, .9)[0] > 60);
    assert(pixel(plane, .98, .50)[0] < 3 && pixel(plane, .75, .02)[0] < 3);

    memset(mixed, 0, sizeof(mixed));
    assert(!spatial_g4_overlap(mixed, plane, 16));
    memset(mixed, 64, sizeof(mixed));
    assert(!spatial_g4_overlap(mixed, plane, 16));
    memset(mixed, 0, sizeof(mixed));
    mixed[2][0] = 90; /* A strong signal unsupported by the measured zones. */
    assert(spatial_g4_overlap(mixed, plane, 16) == false);
}
int main(void){
 (void)layout_from_json;
 unsigned char regions[16][3];for(unsigned i=0;i<16;i++){regions[i][0]=11;regions[i][1]=22;regions[i][2]=33;}
 char *command=spatial_image_command(regions,149);assert(command);
 assert(strstr(command,"\"command\":\"image\"") && strstr(command,"\"priority\":149") && strstr(command,"\"format\":\"rgb\""));
 const char *encoded=strstr(command,"\"imagedata\":\"")+13;
 for(unsigned i=0;i<SPATIAL_PIXELS;i++)assert(!memcmp(encoded+i*4,"CxYh",4));
 assert(!strcmp(encoded+SPATIAL_PIXELS*4,"\"}\n"));free(command);
 for(unsigned i=0;i<16;i++){regions[i][0]=i*16;regions[i][1]=255-i*16;regions[i][2]=i*8;}
 command=spatial_image_command(regions,149);encoded=strstr(command,"\"imagedata\":\"")+13;
 assert(memcmp(encoded,encoded+(SPATIAL_WIDTH-1)*4,4));
 assert(memcmp(encoded,encoded+(SPATIAL_PIXELS-SPATIAL_WIDTH)*4,4));free(command);
 calibration=(Calibration){.kind=CAL_CUSTOM,.count=2,.points={{0,2},{2,10}}};calibration_generation++;
 command=spatial_image_command(regions,149);encoded=strstr(command,"\"imagedata\":\"")+13;
 const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 unsigned char expected[3],actual[3];LedBlend custom=layout_blend(.5/SPATIAL_WIDTH,.5/SPATIAL_HEIGHT);
 blend_color(regions[custom.a],regions[custom.b],custom.weight,expected);
 unsigned v[4];for(unsigned i=0;i<4;i++)v[i]=(unsigned)(strchr(alphabet,encoded[i])-alphabet);
 actual[0]=(v[0]<<2)|(v[1]>>4);actual[1]=(v[1]<<4)|(v[2]>>2);actual[2]=(v[2]<<6)|v[3];assert(!memcmp(expected,actual,3));free(command);
 test_g4_overlap();
 puts("PASS: image command, exact neutral RGB base64, spatial edge differences and cached geometry");return 0;
}

#include <assert.h>
#include <stdio.h>
#include "dark_color.h"
int main(void) {
    for (unsigned v = 0; v <= 255; v++) {
        unsigned char gray[3] = {v, v, v};
        dark_color_filter(gray, 16);
        unsigned expected = v <= 16 ? 0 : v >= 32 ? v : (v * (v - 16) + 8) / 16;
        for (unsigned c = 0; c < 3; c++) assert(gray[c] == expected);
    }
    unsigned char residual[3] = {4, 8, 2};
    dark_color_filter(residual, 16);
    assert(!residual[0] && !residual[1] && !residual[2]);
    unsigned char measured[3] = {13, 14, 7};
    dark_color_filter(measured, 16);
    assert(!measured[0] && !measured[1] && !measured[2]);
    unsigned char transition[3] = {12, 24, 8};
    dark_color_filter(transition, 16);
    assert(transition[0] == 6 && transition[1] == 12 && transition[2] == 4);
    unsigned char saturated[3] = {0, 32, 0};
    dark_color_filter(saturated, 16);
    assert(saturated[0] == 0 && saturated[1] == 32 && saturated[2] == 0);
    unsigned char disabled[3] = {4, 8, 2};
    dark_color_filter(disabled, 0);
    assert(disabled[0] == 4 && disabled[1] == 8 && disabled[2] == 2);
    dark_color_filter(disabled, 8);
    assert(disabled[0] == 0 && disabled[1] == 0 && disabled[2] == 0);
    unsigned char higher[3] = {32, 96, 64};
    dark_color_filter(higher, 64);
    assert(higher[0] == 16 && higher[1] == 48 && higher[2] == 32);
    puts("PASS: near-black suppression, gradual transition, unchanged brighter colors");
}

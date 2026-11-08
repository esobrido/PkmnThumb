#include "ow/fade.h"

/* Rounded channel blend towards the maximum of an n-bit field. Splitting RGB565
 * by hand keeps this to shifts and one multiply per channel; it runs 16 384
 * times a frame for 20 frames per door, so it has to stay cheap. */
static int blend(int c, int max, int level) {
    return c + ((max - c) * level + OW_FADE_STEPS / 2) / OW_FADE_STEPS;
}

void ow_fade_apply(uint16_t *fb, int pixels, int level) {
    if (!fb || level <= 0) return;
    if (level > OW_FADE_STEPS) level = OW_FADE_STEPS;
    if (level == OW_FADE_STEPS) {
        for (int i = 0; i < pixels; i++) fb[i] = 0xFFFF;
        return;
    }
    for (int i = 0; i < pixels; i++) {
        uint16_t p = fb[i];
        int r = blend((p >> 11) & 0x1F, 31, level);
        int g = blend((p >> 5) & 0x3F, 63, level);
        int b = blend(p & 0x1F, 31, level);
        fb[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

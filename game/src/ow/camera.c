#include "ow/camera.h"

void ow_camera_follow(OwCamera *cam, const OwMap *m, int px, int py, int screen) {
    int max_x = m->w * OW_BLOCK_PX - screen;
    int max_y = m->h * OW_BLOCK_PX - screen;

    cam->x = px + OW_STEP_PX / 2 - screen / 2;
    cam->y = py + OW_STEP_PX / 2 - screen / 2;

    if (cam->x > max_x) cam->x = max_x;
    if (cam->y > max_y) cam->y = max_y;
    if (cam->x < 0) cam->x = 0;
    if (cam->y < 0) cam->y = 0;
}

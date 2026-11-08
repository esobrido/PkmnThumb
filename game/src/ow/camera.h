/*
 * ow/camera.h — the scrolling window.
 *
 * Pure logic. The camera centres on the player and clamps to the map, so a map
 * smaller than the screen shows from its origin instead of revealing void.
 */
#ifndef OW_CAMERA_H
#define OW_CAMERA_H

#include "ow/map.h"

typedef struct { int x, y; } OwCamera;

/* Centre `cam` on the player and clamp it inside the map.
 * `screen` is the visible size in pixels (128 on the Thumby Color). */
void ow_camera_follow(OwCamera *cam, const OwMap *m, int px, int py, int screen);

#endif

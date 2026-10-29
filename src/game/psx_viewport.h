/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Original display coordinates follow Tekken3Remake HUD research; see NOTICE.md. */
#ifndef T3_PSX_VIEWPORT_H
#define T3_PSX_VIEWPORT_H

/* Map the original mode-0 displayed rectangle (0,20,368,448) into the
 * Saturn's 320x224 image. Original GTE, particle and 2D coordinates share
 * this framebuffer; scene submission uses a separate centered origin. */
static inline int t3_psx_viewport_x(int x) {
    int scaled=x*20;
    return (scaled+(scaled<0?-11:11))/23;
}
static inline int t3_psx_viewport_y(int y) { return (y-20)/2; }

#endif

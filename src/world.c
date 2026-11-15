/* world.c - hall and room scenery (spec 07 part A sections 2 and 3) */
#include "game.h"

static Aseq *hallset(void) { return aseq_load(200); }

void edge_strips(void) {
    Aseq *h = hallset();
    ui_stamp(h, 58, 6, 34, ST_BACK);
    ui_stamp(h, 59, 499, 34, ST_BACK);
}

/* Draw the hot-spot cels of `area` that overlap the view (scroll = world column at screen column 0). */
static void draw_area_spots(int area, int scroll) {
    if (area < 0 || area > 9) return;
    Aseq *h = hallset();
    Rect clip = {9, 34, 494, 250};
    for (int s = 0; s < 4; s++) {
        Hot *ht = &hot[area][s];
        if (ht->type == -1) continue;
        int wx = area * 38 + ht->col;
        int sx = 9 + 13 * (wx - scroll);
        int cel = ht->cel, y = ht->y, mirror = 0;
        const Cel *c;
        switch (ht->type) {
        case 1:
            c = aseq_cel(h, 134);
            if (c) {
                canvas_blit_cel(g_eng.back, c, sx, 101, 0, &clip);
                canvas_blit_cel(g_eng.work, c, sx, 101, 0, &clip);
            }
            y = 76;
            break;
        case 6:
            cel = 123; y = 54;
            mirror = (area == 0 || area == 5);
            break;
        default: break;
        }
        c = aseq_cel(h, cel);
        if (!c) continue;
        canvas_blit_cel(g_eng.back, c, sx, y, mirror, &clip);
        canvas_blit_cel(g_eng.work, c, sx, y, mirror, &clip);
    }
}

void hall_draw_full(int scroll) {
    Aseq *h = hallset();
    Rect clip = {9, 34, 494, 250};
    ui_fill(9, 35, 502, 67, C_BROWN, 1);
    for (int c = 0; c < 38; c++) {
        int wc = ((scroll + c) % 38 + 38) % 38;
        ui_stamp(h, 137 + wc, 9 + 13 * c, 58, ST_BACK);
    }
    int a0 = scroll / 38;
    draw_area_spots(a0, scroll);
    if (scroll % 38) draw_area_spots(a0 + 1, scroll);
    (void)clip;
    edge_strips();
}

void room_draw(int area) {
    ui_stamp_picture(1001 + (area - 10), 10, 36, ST_BACK);
    ui_stamp_picture(1022, 80, 92, ST_BACK);
}

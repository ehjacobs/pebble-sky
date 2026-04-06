/**
 * Sky-Pebble — Analog watchface with GMT and Annual Calendar complications for Pebble Round 2
 *
 * Features:
 *   - Round 260×260 color display (gabbro platform)
 *   - Blue dial with clean polished edge
 *   - Polished baton hour markers
 *   - Skeletonized hour and minute hands with luminous strips
 *   - 24-hour GMT ring (off-center rotating annulus)
 *   - Month indicator ring (12 apertures, current month = red)
 *   - Date window at 3 o'clock with lens
 *   - Earth icon with ZULU label at 12 o'clock
 */

#include <pebble.h>

// ============================================================================
// LAYOUT CONSTANTS (gabbro: 260×260 round)
// ============================================================================

#define DIAL_RADIUS        126
#define MONTH_RING_R       119
#define TICK_OUTER_R       124   // aligned with month window outer edge
#define TICK_MIN_INNER_R   116   // minute ticks (8px long)
#define TICK_HALF_INNER_R  120   // half-minute ticks (4px long)
#define MARKER_OUTER_R     110
#define MARKER_INNER_R      83

// GMT ring (off-center 24-hour annulus, shifted below dial center)
#define GMT_DISC_OFFSET_Y   24
#define GMT_RING_OUTER      75
#define GMT_RING_INNER      51
#define GMT_NUM_R           63

// Hand dimensions
#define HOUR_HAND_LEN       80
#define HOUR_HAND_WIDTH      7
#define HOUR_HAND_TAIL      15
#define MIN_HAND_LEN       119
#define MIN_HAND_WIDTH       7
#define MIN_HAND_TAIL       20
#define SEC_HAND_LEN       110
#define SEC_HAND_TAIL       25

// GMT disc bitmap center (154×154 image)
#define GMT_BITMAP_CENTER   77

// Number of GPath sections per skeletonized hand
#define HAND_PARTS 5

// ============================================================================
// GLOBALS
// ============================================================================

static Window *s_main_window;
static Layer  *s_canvas_layer;

static GPoint s_center;

static GBitmap *s_gmt_bitmap;

// Battery display state (shown on wrist tap)
static bool s_show_battery = false;
static AppTimer *s_battery_timer = NULL;

// Seconds hand auto-disable (activates on tap, disables after timeout)
#define SECONDS_TIMEOUT_MS 30000
static bool s_seconds_active = true;
static AppTimer *s_seconds_timer = NULL;

// Bluetooth connection state
static bool s_bt_connected = true;

// Pre-allocated paths: static dial elements (computed once in window_load)
static GPath *s_month_paths[12];
static GPoint s_month_pts[12][4];

static GPath *s_marker_paths[12];   // NULL at positions 0 and 3
static GPoint s_marker_pts[12][4];

static GPath *s_brand_paths[2];
static GPoint s_brand_pts[2][4];

static GPath *s_gmt_tri_path;
static GPoint s_gmt_tri_pts[3];
static GPath *s_gmt_tri_inner_path;
static GPoint s_gmt_tri_inner_pts[3];

// Pre-allocated paths: clock hands (origin-relative, rotated at draw time)
static GPath *s_hour_hand[HAND_PARTS];
static GPoint s_hour_pts[HAND_PARTS][4];
static GPath *s_min_hand[HAND_PARTS];
static GPoint s_min_pts[HAND_PARTS][4];

// ============================================================================
// DRAWING HELPERS
// ============================================================================

static int16_t trig_round(int32_t val) {
    if (val > 0) return (int16_t)((val + TRIG_MAX_RATIO / 2) / TRIG_MAX_RATIO);
    if (val < 0) return (int16_t)((val - TRIG_MAX_RATIO / 2) / TRIG_MAX_RATIO);
    return 0;
}

static GPoint point_on_circle(GPoint center, int radius, int32_t angle) {
    return (GPoint) {
        .x = center.x + trig_round(sin_lookup(angle) * (int32_t)radius),
        .y = center.y - trig_round(cos_lookup(angle) * (int32_t)radius)
    };
}

// ============================================================================
// PATH PRE-ALLOCATION
// ============================================================================

static void create_hand_paths(GPath **paths, GPoint pts[][4],
                              int hand_len, int hand_width,
                              int hand_tail, int lume_width) {
    int tip_w = hand_width - 2;
    int total = hand_tail + hand_len;
    int cut_start = 18;
    int cut_end = hand_len / 2 - 3;
    int hw_cs = hand_width - 2 * (cut_start + hand_tail) / total;
    int hw_ce = hand_width - 2 * (cut_end + hand_tail) / total;
    int rail_cs = hw_cs - lume_width;
    int rail_ce = hw_ce - lume_width;

    // 0: Base section
    pts[0][0] = GPoint(-hw_cs, -cut_start);
    pts[0][1] = GPoint(hw_cs, -cut_start);
    pts[0][2] = GPoint(hand_width, hand_tail);
    pts[0][3] = GPoint(-hand_width, hand_tail);
    paths[0] = gpath_create(&(GPathInfo){ .num_points = 4, .points = pts[0] });

    // 1: Left rail (extended 3px each end to overlap adjacent sections)
    pts[1][0] = GPoint(-hw_ce, -(cut_end + 3));
    pts[1][1] = GPoint(-hw_ce + rail_ce, -(cut_end + 3));
    pts[1][2] = GPoint(-hw_cs + rail_cs, -(cut_start - 3));
    pts[1][3] = GPoint(-hw_cs, -(cut_start - 3));
    paths[1] = gpath_create(&(GPathInfo){ .num_points = 4, .points = pts[1] });

    // 2: Right rail (extended 3px each end to overlap adjacent sections)
    pts[2][0] = GPoint(hw_ce - rail_ce, -(cut_end + 3));
    pts[2][1] = GPoint(hw_ce, -(cut_end + 3));
    pts[2][2] = GPoint(hw_cs, -(cut_start - 3));
    pts[2][3] = GPoint(hw_cs - rail_cs, -(cut_start - 3));
    paths[2] = gpath_create(&(GPathInfo){ .num_points = 4, .points = pts[2] });

    // 3: Outer section
    pts[3][0] = GPoint(-tip_w, -hand_len);
    pts[3][1] = GPoint(tip_w, -hand_len);
    pts[3][2] = GPoint(hw_ce, -cut_end);
    pts[3][3] = GPoint(-hw_ce, -cut_end);
    paths[3] = gpath_create(&(GPathInfo){ .num_points = 4, .points = pts[3] });

    // 4: Full outline (base to tip)
    pts[4][0] = GPoint(-tip_w, -hand_len);
    pts[4][1] = GPoint(tip_w, -hand_len);
    pts[4][2] = GPoint(hand_width, hand_tail);
    pts[4][3] = GPoint(-hand_width, hand_tail);
    paths[4] = gpath_create(&(GPathInfo){ .num_points = 4, .points = pts[4] });
}

static void create_static_paths(void) {
    // Month indicators
    int32_t m_rout = MONTH_RING_R + 5;
    int32_t m_rin = MONTH_RING_R - 5;
    int32_t m_hw = 5;
    for (int i = 0; i < 12; i++) {
        int hour = (i + 1) % 12;
        int32_t angle = (hour * TRIG_MAX_ANGLE) / 12;
        int32_t sa = sin_lookup(angle);
        int32_t ca = cos_lookup(angle);
        s_month_pts[i][0] = GPoint(s_center.x + trig_round(sa * m_rout + ca * m_hw),
                                   s_center.y - trig_round(ca * m_rout - sa * m_hw));
        s_month_pts[i][1] = GPoint(s_center.x + trig_round(sa * m_rout - ca * m_hw),
                                   s_center.y - trig_round(ca * m_rout + sa * m_hw));
        s_month_pts[i][2] = GPoint(s_center.x + trig_round(sa * m_rin - ca * m_hw),
                                   s_center.y - trig_round(ca * m_rin + sa * m_hw));
        s_month_pts[i][3] = GPoint(s_center.x + trig_round(sa * m_rin + ca * m_hw),
                                   s_center.y - trig_round(ca * m_rin - sa * m_hw));
        s_month_paths[i] = gpath_create(&(GPathInfo){ .num_points = 4, .points = s_month_pts[i] });
    }

    // Hour markers (skip 12 and 3 o'clock)
    int32_t h_hw = 5;
    int32_t h_rout = MARKER_OUTER_R;
    int32_t h_rin = MARKER_INNER_R;
    for (int i = 0; i < 12; i++) {
        if (i == 0 || i == 3) {
            s_marker_paths[i] = NULL;
            continue;
        }
        int32_t angle = (i * TRIG_MAX_ANGLE) / 12;
        int32_t sa = sin_lookup(angle);
        int32_t ca = cos_lookup(angle);
        s_marker_pts[i][0] = GPoint(s_center.x + trig_round(sa * h_rout + ca * h_hw),
                                    s_center.y - trig_round(ca * h_rout - sa * h_hw));
        s_marker_pts[i][1] = GPoint(s_center.x + trig_round(sa * h_rout - ca * h_hw),
                                    s_center.y - trig_round(ca * h_rout + sa * h_hw));
        s_marker_pts[i][2] = GPoint(s_center.x + trig_round(sa * h_rin - ca * h_hw),
                                    s_center.y - trig_round(ca * h_rin + sa * h_hw));
        s_marker_pts[i][3] = GPoint(s_center.x + trig_round(sa * h_rin + ca * h_hw),
                                    s_center.y - trig_round(ca * h_rin - sa * h_hw));
        s_marker_paths[i] = gpath_create(&(GPathInfo){ .num_points = 4, .points = s_marker_pts[i] });
    }

    // "Made in" marks (unreadable, decorative rectangles flanking 6 o'clock)
    int32_t bk_out = TICK_HALF_INNER_R - 1;
    int32_t bk_in = TICK_MIN_INNER_R;
    int32_t bk_hw = 10;
    int bk_pos[] = { 64, 56 };
    for (int m = 0; m < 2; m++) {
        int32_t angle = (bk_pos[m] * TRIG_MAX_ANGLE) / 120;
        int32_t sa = sin_lookup(angle);
        int32_t ca = cos_lookup(angle);
        s_brand_pts[m][0] = GPoint(s_center.x + trig_round(sa * bk_out + ca * bk_hw),
                                   s_center.y - trig_round(ca * bk_out - sa * bk_hw));
        s_brand_pts[m][1] = GPoint(s_center.x + trig_round(sa * bk_out - ca * bk_hw),
                                   s_center.y - trig_round(ca * bk_out + sa * bk_hw));
        s_brand_pts[m][2] = GPoint(s_center.x + trig_round(sa * bk_in - ca * bk_hw),
                                   s_center.y - trig_round(ca * bk_in + sa * bk_hw));
        s_brand_pts[m][3] = GPoint(s_center.x + trig_round(sa * bk_in + ca * bk_hw),
                                   s_center.y - trig_round(ca * bk_in - sa * bk_hw));
        s_brand_paths[m] = gpath_create(&(GPathInfo){ .num_points = 4, .points = s_brand_pts[m] });
    }

    // GMT red/white triangle pointer
    GPoint dc = GPoint(s_center.x, s_center.y + GMT_DISC_OFFSET_Y);
    s_gmt_tri_pts[0] = GPoint(dc.x, dc.y - GMT_RING_OUTER - 1);
    s_gmt_tri_pts[1] = GPoint(dc.x - 8, dc.y - GMT_RING_OUTER - 16);
    s_gmt_tri_pts[2] = GPoint(dc.x + 8, dc.y - GMT_RING_OUTER - 16);
    s_gmt_tri_path = gpath_create(&(GPathInfo){ .num_points = 3, .points = s_gmt_tri_pts });

    s_gmt_tri_inner_pts[0] = GPoint(dc.x, dc.y - GMT_RING_OUTER - 4);
    s_gmt_tri_inner_pts[1] = GPoint(dc.x - 5, dc.y - GMT_RING_OUTER - 14);
    s_gmt_tri_inner_pts[2] = GPoint(dc.x + 5, dc.y - GMT_RING_OUTER - 14);
    s_gmt_tri_inner_path = gpath_create(&(GPathInfo){ .num_points = 3, .points = s_gmt_tri_inner_pts });
}

static void destroy_all_paths(void) {
    for (int i = 0; i < 12; i++) {
        if (s_month_paths[i]) { gpath_destroy(s_month_paths[i]); s_month_paths[i] = NULL; }
        if (s_marker_paths[i]) { gpath_destroy(s_marker_paths[i]); s_marker_paths[i] = NULL; }
    }
    for (int i = 0; i < 2; i++) {
        if (s_brand_paths[i]) { gpath_destroy(s_brand_paths[i]); s_brand_paths[i] = NULL; }
    }
    if (s_gmt_tri_path) { gpath_destroy(s_gmt_tri_path); s_gmt_tri_path = NULL; }
    if (s_gmt_tri_inner_path) { gpath_destroy(s_gmt_tri_inner_path); s_gmt_tri_inner_path = NULL; }
    for (int i = 0; i < HAND_PARTS; i++) {
        if (s_hour_hand[i]) { gpath_destroy(s_hour_hand[i]); s_hour_hand[i] = NULL; }
        if (s_min_hand[i]) { gpath_destroy(s_min_hand[i]); s_min_hand[i] = NULL; }
    }
}

// ============================================================================
// DRAW: Dial edge
// ============================================================================

static void draw_dial_edge(GContext *ctx) {
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, s_center, DIAL_RADIUS + 1);

    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_circle(ctx, s_center, DIAL_RADIUS);
}

// ============================================================================
// DRAW: Minute track
// ============================================================================

static void draw_minute_track(GContext *ctx) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);

    // 120 positions = every 30 seconds
    for (int i = 0; i < 120; i++) {
        int pos = i % 10;
        if (pos == 0) continue;               // hour marker position
        if (pos == 1 || pos == 9) continue;    // too close to hour marker

        int32_t angle = (i * TRIG_MAX_ANGLE) / 120;
        int inner_r;

        if (i == 56 || i == 64) {
            inner_r = TICK_HALF_INNER_R;       // shortened for SFCA MADE marks
        } else if (pos % 2 == 0) {
            inner_r = TICK_MIN_INNER_R;        // minute tick (longer)
        } else {
            inner_r = TICK_HALF_INNER_R;       // half-minute tick (shorter)
        }

        GPoint inner = point_on_circle(s_center, inner_r, angle);
        GPoint outer = point_on_circle(s_center, TICK_OUTER_R, angle);
        graphics_draw_line(ctx, inner, outer);
    }
}

// ============================================================================
// DRAW: Month indicator ring
// ============================================================================

static void draw_month_indicators(GContext *ctx, int current_month) {
    for (int i = 0; i < 12; i++) {
        graphics_context_set_fill_color(ctx,
            (i == current_month) ? GColorRed : GColorWhite);
        gpath_draw_filled(ctx, s_month_paths[i]);
        graphics_context_set_stroke_color(ctx, GColorPictonBlue);
        graphics_context_set_stroke_width(ctx, 1);
        gpath_draw_outline(ctx, s_month_paths[i]);
    }
}

// ============================================================================
// DRAW: Hour markers (baton indices)
// ============================================================================

static void draw_hour_markers(GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorWhite);

    for (int i = 0; i < 12; i++) {
        if (!s_marker_paths[i]) continue;
        gpath_draw_filled(ctx, s_marker_paths[i]);
        graphics_context_set_stroke_color(ctx, GColorLightGray);
        graphics_context_set_stroke_width(ctx, 1);
        gpath_draw_outline(ctx, s_marker_paths[i]);
    }
}

// ============================================================================
// DRAW: Earth icon + ZULU at 12 o'clock
// ============================================================================

static void draw_earth_icon(GContext *ctx) {
    int earth_y = s_center.y - MARKER_OUTER_R + 7;
    GPoint earth = GPoint(s_center.x, earth_y);
    int r = 8;

    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, earth, r);

    // Equator
    graphics_draw_line(ctx, GPoint(earth.x - r, earth.y),
                            GPoint(earth.x + r, earth.y));
    // Prime meridian
    graphics_draw_line(ctx, GPoint(earth.x, earth.y - r),
                            GPoint(earth.x, earth.y + r));
    // Tropics
    graphics_draw_line(ctx, GPoint(earth.x - r + 1, earth.y - 4),
                            GPoint(earth.x + r - 1, earth.y - 4));
    graphics_draw_line(ctx, GPoint(earth.x - r + 1, earth.y + 4),
                            GPoint(earth.x + r - 1, earth.y + 4));
    // Meridians
    graphics_draw_line(ctx, GPoint(earth.x - 4, earth.y - r + 1),
                            GPoint(earth.x - 4, earth.y + r - 1));
    graphics_draw_line(ctx, GPoint(earth.x + 4, earth.y - r + 1),
                            GPoint(earth.x + 4, earth.y + r - 1));

    // "ZULU" label
    GRect utc_rect = GRect(s_center.x - 20, earth_y + r + 1, 40, 18);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "ZULU",
        fonts_get_system_font(FONT_KEY_GOTHIC_18),
        utc_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: 24-hour GMT ring (off-center annulus)
// ============================================================================

static void draw_gmt_ring(GContext *ctx, int hour_24, int minutes, int seconds) {
    GPoint disc_center = GPoint(s_center.x, s_center.y + GMT_DISC_OFFSET_Y);

    GRect ring_rect = GRect(disc_center.x - GMT_RING_OUTER,
                            disc_center.y - GMT_RING_OUTER,
                            GMT_RING_OUTER * 2,
                            GMT_RING_OUTER * 2);

    int ring_thickness = GMT_RING_OUTER - GMT_RING_INNER;

    int32_t hour_angle = ((hour_24 * TRIG_MAX_ANGLE) / 24) +
                         ((minutes * TRIG_MAX_ANGLE) / 24 / 60) +
                         ((seconds * TRIG_MAX_ANGLE) / 24 / 3600);

    // Navy ring fill
    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    graphics_fill_radial(ctx, ring_rect, GOvalScaleModeFitCircle,
                         ring_thickness, 0, TRIG_MAX_ANGLE);

    // Ring borders
    graphics_context_set_stroke_color(ctx, GColorPictonBlue);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, disc_center, GMT_RING_OUTER);
    graphics_draw_circle(ctx, disc_center, GMT_RING_INNER);

    // Rotated disc face (numbers + ticks from bitmap)
    if (s_gmt_bitmap) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        int32_t rotation = TRIG_MAX_ANGLE - hour_angle;
        graphics_draw_rotated_bitmap(ctx, s_gmt_bitmap,
            GPoint(GMT_BITMAP_CENTER, GMT_BITMAP_CENTER),
            rotation,
            disc_center);
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    }

    // Red/white triangle pointer (pre-allocated paths)
    graphics_context_set_fill_color(ctx, GColorRed);
    gpath_draw_filled(ctx, s_gmt_tri_path);
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, s_gmt_tri_inner_path);
}

// ============================================================================
// DRAW: Date window at 3 o'clock (also shows battery % on tap)
// ============================================================================

static void draw_date_window(GContext *ctx, int mday) {
    int cx = s_center.x + (MARKER_INNER_R + MARKER_OUTER_R) / 2 - 2;
    int cy = s_center.y;
    int date_w = 26;
    int date_h = 18;

    // White inner rectangle with light blue border
    GRect date_rect = GRect(cx - date_w / 2, cy - date_h / 2, date_w, date_h);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, date_rect, 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorPictonBlue);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, date_rect);

    if (s_show_battery) {
        BatteryChargeState bat = battery_state_service_peek();
        int pct = (int)bat.charge_percent;
        char bat_buf[4];
        snprintf(bat_buf, sizeof(bat_buf), "%d", pct);

        GColor color;
        if (pct >= 40) {
            color = GColorGreen;
        } else if (pct >= 20) {
            color = GColorChromeYellow;
        } else {
            color = GColorRed;
        }

        graphics_context_set_text_color(ctx, color);
        GRect text_rect = GRect(cx - date_w / 2, cy - 14, date_w, 18);
        graphics_draw_text(ctx, bat_buf,
            fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            text_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    } else if (!s_bt_connected) {
        // Draw Bluetooth rune in red (disconnected indicator)
        graphics_context_set_stroke_color(ctx, GColorRed);
        graphics_context_set_stroke_width(ctx, 1);
        int h = 6, w = 3;
        // Vertical line
        graphics_draw_line(ctx, GPoint(cx, cy - h), GPoint(cx, cy + h));
        // Lower-left to top
        graphics_draw_line(ctx, GPoint(cx - w, cy + h / 2), GPoint(cx, cy - h));
        // Top to lower-right
        graphics_draw_line(ctx, GPoint(cx, cy - h), GPoint(cx + w, cy + h / 2));
        // Upper-left to bottom
        graphics_draw_line(ctx, GPoint(cx - w, cy - h / 2), GPoint(cx, cy + h));
        // Bottom to upper-right
        graphics_draw_line(ctx, GPoint(cx, cy + h), GPoint(cx + w, cy - h / 2));
    } else {
        char date_buf[4];
        snprintf(date_buf, sizeof(date_buf), "%d", mday);
        graphics_context_set_text_color(ctx, GColorBlack);
        GRect text_rect = GRect(cx - 14, cy - 17, 28, 24);
        graphics_draw_text(ctx, date_buf,
            fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
            text_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
}

static void draw_date_lens(GContext *ctx) {
    int cx = s_center.x + (MARKER_INNER_R + MARKER_OUTER_R) / 2 - 2;
    int cy = s_center.y;
    int pad = 5;
    int lens_w = 26 + pad * 2;
    int lens_h = 18 + pad * 2;
    int corner_r = lens_h / 2;

    GRect lens_rect = GRect(cx - lens_w / 2, cy - lens_h / 2, lens_w, lens_h);
    graphics_context_set_stroke_color(ctx, GColorPictonBlue);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_round_rect(ctx, lens_rect, corner_r);
}

// ============================================================================
// DRAW: Brand text
// ============================================================================

static void draw_brand_text(GContext *ctx) {
    graphics_context_set_text_color(ctx, GColorWhite);

    // "SKY-PEBBLE" below center
    int text_y = s_center.y + 18;
    GRect brand_rect = GRect(s_center.x - 50, text_y, 100, 18);
    graphics_draw_text(ctx, "SKY-PEBBLE",
        fonts_get_system_font(FONT_KEY_GOTHIC_18),
        brand_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);

    // "SFCA MADE" at bottom — gibberish marks replacing shortened ticks
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int m = 0; m < 2; m++) {
        gpath_draw_filled(ctx, s_brand_paths[m]);
    }
}

// ============================================================================
// DRAW: Clock hands
// ============================================================================

static void draw_hand(GContext *ctx, GPath **paths, int32_t angle,
                      int lume_width, int hand_len) {
    int lume_start_r = hand_len / 2 + 3;

    // Rotate and position all pre-allocated sections
    for (int i = 0; i < HAND_PARTS; i++) {
        gpath_rotate_to(paths[i], angle);
        gpath_move_to(paths[i], s_center);
    }

    // Fill sections: base, left rail, right rail, outer
    graphics_context_set_fill_color(ctx, GColorLightGray);
    for (int i = 0; i < HAND_PARTS - 1; i++) {
        gpath_draw_filled(ctx, paths[i]);
    }

    // Full hand outline — continuous dark grey edge from base to tip
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, paths[4]);

    // Luminous strip on outer section
    GPoint ls = point_on_circle(s_center, lume_start_r, angle);
    GPoint le = point_on_circle(s_center, hand_len - 6, angle);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, lume_width);
    graphics_draw_line(ctx, ls, le);
}

static void draw_hands(GContext *ctx, struct tm *t) {
    int32_t hour_angle = ((t->tm_hour % 12) * TRIG_MAX_ANGLE / 12) +
                         (t->tm_min * TRIG_MAX_ANGLE / 12 / 60) +
                         (t->tm_sec * TRIG_MAX_ANGLE / 12 / 3600);
    int32_t min_angle = (t->tm_min * TRIG_MAX_ANGLE / 60) +
                         (t->tm_sec * TRIG_MAX_ANGLE / 60 / 60);
    // Draw order: hour (bottom), minute, seconds (top)
    draw_hand(ctx, s_hour_hand, hour_angle, 3, HOUR_HAND_LEN);
    draw_hand(ctx, s_min_hand, min_angle, 3, MIN_HAND_LEN);

    if (s_seconds_active) {
        int32_t sec_angle = (t->tm_sec * TRIG_MAX_ANGLE / 60);

        // Seconds hand — tapered from 3px at center to 2px at tip
        GPoint sec_tip = point_on_circle(s_center, SEC_HAND_LEN, sec_angle);
        GPoint sec_mid = point_on_circle(s_center, SEC_HAND_LEN / 2, sec_angle);
        GPoint sec_tail = point_on_circle(s_center, SEC_HAND_TAIL,
                                          sec_angle + TRIG_MAX_ANGLE / 2);

        // Thick inner portion (tail to midpoint)
        graphics_context_set_stroke_color(ctx, GColorLightGray);
        graphics_context_set_stroke_width(ctx, 3);
        graphics_draw_line(ctx, sec_tail, sec_mid);

        // Slightly thinner outer portion (midpoint to tip)
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_line(ctx, sec_mid, sec_tip);

        // Center pivot — outline leaves gaps where the seconds hand passes through
        graphics_context_set_fill_color(ctx, GColorLightGray);
        graphics_fill_circle(ctx, s_center, 7);
        graphics_context_set_stroke_color(ctx, GColorDarkGray);
        graphics_context_set_stroke_width(ctx, 1);
        int32_t gap = TRIG_MAX_ANGLE * 15 / 360;
        GRect pivot_rect = GRect(s_center.x - 7, s_center.y - 7, 15, 15);
        graphics_draw_arc(ctx, pivot_rect, GOvalScaleModeFitCircle,
                          sec_angle + gap, sec_angle + TRIG_MAX_ANGLE / 2 - gap);
        graphics_draw_arc(ctx, pivot_rect, GOvalScaleModeFitCircle,
                          sec_angle + TRIG_MAX_ANGLE / 2 + gap,
                          sec_angle + TRIG_MAX_ANGLE - gap);
        graphics_context_set_fill_color(ctx, GColorDarkGray);
        graphics_fill_circle(ctx, s_center, 2);
    } else {
        // Plain pivot (no seconds hand)
        graphics_context_set_fill_color(ctx, GColorLightGray);
        graphics_fill_circle(ctx, s_center, 7);
        graphics_context_set_stroke_color(ctx, GColorDarkGray);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_circle(ctx, s_center, 7);
        graphics_context_set_fill_color(ctx, GColorDarkGray);
        graphics_fill_circle(ctx, s_center, 2);
    }
}

// ============================================================================
// BATTERY TAP HANDLER
// ============================================================================

static void battery_timer_callback(void *data) {
    s_show_battery = false;
    s_battery_timer = NULL;
    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

static void show_battery(void) {
    s_show_battery = true;
    if (s_battery_timer) {
        app_timer_reschedule(s_battery_timer, 3000);
    } else {
        s_battery_timer = app_timer_register(3000, battery_timer_callback, NULL);
    }
    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

// ============================================================================
// SECONDS HAND AUTO-DISABLE
// ============================================================================

static void tick_handler(struct tm *tick_time, TimeUnits units_changed);

static void seconds_timeout_callback(void *data) {
    s_seconds_active = false;
    s_seconds_timer = NULL;
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

static void activate_seconds(void) {
    if (!s_seconds_active) {
        s_seconds_active = true;
        tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
        if (s_canvas_layer) {
            layer_mark_dirty(s_canvas_layer);
        }
    }
    if (s_seconds_timer) {
        app_timer_reschedule(s_seconds_timer, SECONDS_TIMEOUT_MS);
    } else {
        s_seconds_timer = app_timer_register(SECONDS_TIMEOUT_MS,
                                             seconds_timeout_callback, NULL);
    }
}

// ============================================================================
// BLUETOOTH CONNECTION
// ============================================================================

static void bluetooth_callback(bool connected) {
    s_bt_connected = connected;
    if (!connected) {
        vibes_double_pulse();
    }
    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
    if (num_samples < 2) return;

    // Find first non-vibrating sample to seed min/max
    uint32_t start = 0;
    while (start < num_samples && data[start].did_vibrate) start++;
    if (start >= num_samples) return;

    int16_t x_min = data[start].x, x_max = data[start].x;
    int16_t y_min = data[start].y, y_max = data[start].y;
    int16_t z_min = data[start].z, z_max = data[start].z;
    bool tap_detected = false;
    uint32_t last_valid = start;

    for (uint32_t i = start + 1; i < num_samples; i++) {
        if (data[i].did_vibrate) continue;

        if (data[i].x < x_min) x_min = data[i].x;
        if (data[i].x > x_max) x_max = data[i].x;
        if (data[i].y < y_min) y_min = data[i].y;
        if (data[i].y > y_max) y_max = data[i].y;
        if (data[i].z < z_min) z_min = data[i].z;
        if (data[i].z > z_max) z_max = data[i].z;

        // Detect tap: sharp delta between consecutive valid samples
        if (!tap_detected) {
            int16_t dx = data[i].x - data[last_valid].x;
            int16_t dy = data[i].y - data[last_valid].y;
            int16_t dz = data[i].z - data[last_valid].z;
            if (dx > 300 || dx < -300 ||
                dy > 300 || dy < -300 ||
                dz > 300 || dz < -300) {
                tap_detected = true;
            }
        }

        last_valid = i;
    }

    // Motion: any axis range > 50 milli-g across the batch
    if ((x_max - x_min > 50) ||
        (y_max - y_min > 50) ||
        (z_max - z_min > 50)) {
        activate_seconds();
    }

    // Sharp tap: show battery level
    if (tap_detected) {
        show_battery();
    }
}

// ============================================================================
// MAIN CANVAS UPDATE
// ============================================================================

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    // Enable antialiasing for crisp edges on color display
    graphics_context_set_antialiased(ctx, true);

    // Black background
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // Blue dial
    graphics_context_set_fill_color(ctx, GColorDukeBlue);
    graphics_fill_circle(ctx, s_center, DIAL_RADIUS);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return;

    // Copy local time before gmtime() overwrites the shared buffer
    struct tm local = *t;

    // UTC time for the 24-hour GMT ring
    struct tm *utc = gmtime(&now);

    draw_dial_edge(ctx);
    draw_minute_track(ctx);
    draw_month_indicators(ctx, local.tm_mon);
    draw_hour_markers(ctx);
    draw_earth_icon(ctx);
    draw_gmt_ring(ctx, utc->tm_hour, utc->tm_min, utc->tm_sec);
    draw_brand_text(ctx);
    draw_date_window(ctx, local.tm_mday);
    draw_hands(ctx, &local);
    draw_date_lens(ctx);
}

// ============================================================================
// TIME HANDLER
// ============================================================================

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

// ============================================================================
// WINDOW HANDLERS
// ============================================================================

static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_center = GPoint(bounds.size.w / 2, bounds.size.h / 2);

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    s_gmt_bitmap = gbitmap_create_with_resource(RESOURCE_ID_GMT_DISC);

    // Pre-allocate all GPath objects
    create_static_paths();
    create_hand_paths(s_hour_hand, s_hour_pts,
                      HOUR_HAND_LEN, HOUR_HAND_WIDTH, HOUR_HAND_TAIL, 3);
    create_hand_paths(s_min_hand, s_min_pts,
                      MIN_HAND_LEN, MIN_HAND_WIDTH, MIN_HAND_TAIL, 3);

    // Signal Quick View awareness (no-op — we let the overlay render on top)
    UnobstructedAreaHandlers ua_handlers = { 0 };
    unobstructed_area_service_subscribe(ua_handlers, NULL);
}

static void main_window_unload(Window *window) {
    unobstructed_area_service_unsubscribe();
    destroy_all_paths();
    if (s_gmt_bitmap) { gbitmap_destroy(s_gmt_bitmap); s_gmt_bitmap = NULL; }
    if (s_canvas_layer) { layer_destroy(s_canvas_layer); s_canvas_layer = NULL; }
}

// ============================================================================
// APP LIFECYCLE
// ============================================================================

static void init(void) {
    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = bluetooth_callback
    });
    s_bt_connected = connection_service_peek_pebble_app_connection();
    window_stack_push(s_main_window, true);
    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    accel_data_service_subscribe(25, accel_data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
    activate_seconds();  // Start auto-disable timer
}

static void deinit(void) {
    connection_service_unsubscribe();
    accel_data_service_unsubscribe();
    if (s_battery_timer) { app_timer_cancel(s_battery_timer); s_battery_timer = NULL; }
    if (s_seconds_timer) { app_timer_cancel(s_seconds_timer); s_seconds_timer = NULL; }
    tick_timer_service_unsubscribe();
    if (s_main_window) { window_destroy(s_main_window); s_main_window = NULL; }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}

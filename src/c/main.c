/**
 * Sky GMT — Analog watchface with GMT complications for Pebble Time 2
 *
 * Features:
 *   - Analog dial with rectangular baton hour markers
 *   - Skeletonized hour and minute hands
 *   - 24-hour subdial with red triangle pointer
 *   - Month indicator ring (12 apertures, current month = red)
 *   - Date window at 3 o'clock
 *   - Fluted bezel texture
 *   - Earth icon with UTC label at 12 o'clock
 */

#include <pebble.h>

// ============================================================================
// LAYOUT CONSTANTS
// ============================================================================

#define DIAL_RADIUS        90
#define BEZEL_OUTER        96
#define BEZEL_INNER        89
#define MONTH_RING_R       84
#define MINUTE_TRACK_OUTER 82
#define MINUTE_TRACK_INNER 80
#define MARKER_OUTER_R     78
#define MARKER_INNER_R     69
#define MARKER_INNER_R_QTR 64

// 24-hour subdial
#define SUBDIAL_OFFSET_Y  (-34)
#define SUBDIAL_RADIUS     22
#define SUBDIAL_NUM_R      16

// Hand dimensions
#define HOUR_HAND_LEN      52
#define HOUR_HAND_WIDTH     6
#define HOUR_HAND_TAIL     12
#define MIN_HAND_LEN       74
#define MIN_HAND_WIDTH      4
#define MIN_HAND_TAIL      14

// Date window
#define DATE_WIN_W         22
#define DATE_WIN_H         16

// ============================================================================
// GLOBALS
// ============================================================================

static Window *s_main_window;
static Layer  *s_canvas_layer;

static GPoint s_center;
static int    s_screen_w;
static int    s_screen_h;

static GPath *s_hour_path;
static GPath *s_min_path;

static GPoint s_hour_pts[6];
static GPoint s_min_pts[6];

static GPathInfo s_hour_info = { .num_points = 6, .points = s_hour_pts };
static GPathInfo s_min_info  = { .num_points = 6, .points = s_min_pts };

// ============================================================================
// HAND GEOMETRY — skeletonized baton shape
// ============================================================================

static void init_hand_points(GPoint *pts, int length, int width, int tail) {
    pts[0] = GPoint(0, -length);
    pts[1] = GPoint(width, -length + 10);
    pts[2] = GPoint(width, tail);
    pts[3] = GPoint(0, tail + 4);
    pts[4] = GPoint(-width, tail);
    pts[5] = GPoint(-width, -length + 10);
}

// ============================================================================
// DRAWING HELPERS
// ============================================================================

static GPoint point_on_circle(GPoint center, int radius, int32_t angle) {
    return (GPoint) {
        .x = center.x + (int16_t)((sin_lookup(angle) * (int32_t)radius) / TRIG_MAX_RATIO),
        .y = center.y - (int16_t)((cos_lookup(angle) * (int32_t)radius) / TRIG_MAX_RATIO)
    };
}

// ============================================================================
// DRAW: Fluted bezel
// ============================================================================

static void draw_bezel(GContext *ctx) {
    // Outer ring border
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, s_center, BEZEL_OUTER);

    // Fluting lines
    for (int i = 0; i < 120; i++) {
        int32_t angle = (i * TRIG_MAX_ANGLE) / 120;
        GPoint inner = point_on_circle(s_center, BEZEL_INNER, angle);
        GPoint outer = point_on_circle(s_center, BEZEL_OUTER, angle);

        graphics_context_set_stroke_color(ctx, (i % 2 == 0) ? GColorLightGray : GColorDarkGray);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_line(ctx, inner, outer);
    }

    // Inner bezel ring
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, s_center, BEZEL_INNER);
}

// ============================================================================
// DRAW: Minute track (fine tick marks between hours)
// ============================================================================

static void draw_minute_track(GContext *ctx) {
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < 60; i++) {
        if (i % 5 == 0) continue;
        int32_t angle = (i * TRIG_MAX_ANGLE) / 60;
        GPoint inner = point_on_circle(s_center, MINUTE_TRACK_INNER, angle);
        GPoint outer = point_on_circle(s_center, MINUTE_TRACK_OUTER, angle);
        graphics_draw_line(ctx, inner, outer);
    }
}

// ============================================================================
// DRAW: Month indicator ring
// ============================================================================

static void draw_month_indicators(GContext *ctx, int current_month) {
    for (int i = 0; i < 12; i++) {
        int hour = (i + 1) % 12;
        int32_t angle = (hour * TRIG_MAX_ANGLE) / 12;
        GPoint pos = point_on_circle(s_center, MONTH_RING_R, angle);

        // Small 3x3 apertures
        GRect sq = GRect(pos.x - 1, pos.y - 1, 3, 3);

        if (i == current_month) {
            graphics_context_set_fill_color(ctx, GColorRed);
        } else {
            graphics_context_set_fill_color(ctx, GColorWhite);
        }
        graphics_fill_rect(ctx, sq, 0, GCornerNone);
    }
}

// ============================================================================
// DRAW: Hour markers (baton indices)
// ============================================================================

static void draw_hour_markers(GContext *ctx) {
    for (int i = 0; i < 12; i++) {
        // Skip 12 o'clock (earth icon) and 3 o'clock (date window)
        if (i == 0 || i == 3) continue;

        int32_t angle = (i * TRIG_MAX_ANGLE) / 12;
        bool is_quarter = (i % 3 == 0);
        int inner_r = is_quarter ? MARKER_INNER_R_QTR : MARKER_INNER_R;

        GPoint inner = point_on_circle(s_center, inner_r, angle);
        GPoint outer = point_on_circle(s_center, MARKER_OUTER_R, angle);

        // Thick white baton
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_context_set_stroke_width(ctx, is_quarter ? 5 : 3);
        graphics_draw_line(ctx, inner, outer);
    }
}

// ============================================================================
// DRAW: Earth icon + UTC at 12 o'clock
// ============================================================================

static void draw_earth_icon(GContext *ctx) {
    // Position in the marker zone at 12 o'clock
    int earth_y = s_center.y - MARKER_OUTER_R + 3;
    GPoint earth = GPoint(s_center.x, earth_y);
    int r = 4;

    // Globe outline
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, earth, r);

    // Equator
    graphics_draw_line(ctx, GPoint(earth.x - r, earth.y),
                            GPoint(earth.x + r, earth.y));

    // Prime meridian
    graphics_draw_line(ctx, GPoint(earth.x, earth.y - r),
                            GPoint(earth.x, earth.y + r));

    // "UTC" label below
    GRect utc_rect = GRect(s_center.x - 12, earth_y + r + 1, 24, 12);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "UTC",
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        utc_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: 24-hour subdial
// ============================================================================

static void draw_24h_subdial(GContext *ctx, int hour_24, int minutes) {
    GPoint sub_center = GPoint(s_center.x, s_center.y + SUBDIAL_OFFSET_Y);

    // Subdial background
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_circle(ctx, sub_center, SUBDIAL_RADIUS);

    // Border ring
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, sub_center, SUBDIAL_RADIUS);

    // Current hour rotation
    int32_t hour_angle = ((hour_24 * TRIG_MAX_ANGLE) / 24) +
                         ((minutes * TRIG_MAX_ANGLE) / 24 / 60);

    // Tick marks and cardinal numbers
    for (int h = 0; h < 24; h++) {
        int32_t tick_angle = (h * TRIG_MAX_ANGLE / 24) - hour_angle;

        GPoint inner_pt = point_on_circle(sub_center, SUBDIAL_RADIUS - 5, tick_angle);
        GPoint outer_pt = point_on_circle(sub_center, SUBDIAL_RADIUS - 1, tick_angle);

        bool is_day = (h >= 6 && h < 18);
        graphics_context_set_stroke_color(ctx, is_day ? GColorWhite : GColorLightGray);
        graphics_context_set_stroke_width(ctx, (h % 6 == 0) ? 2 : 1);
        graphics_draw_line(ctx, inner_pt, outer_pt);

        if (h % 6 == 0) {
            GPoint num_pt = point_on_circle(sub_center, SUBDIAL_NUM_R - 2, tick_angle);
            char buf[3];
            snprintf(buf, sizeof(buf), "%d", (h == 0) ? 24 : h);

            GRect text_box = GRect(num_pt.x - 8, num_pt.y - 7, 16, 14);
            graphics_context_set_text_color(ctx, GColorWhite);
            graphics_draw_text(ctx, buf,
                fonts_get_system_font(FONT_KEY_GOTHIC_14),
                text_box, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        }
    }

    // Red inverted triangle pointer — tight to subdial edge
    GPoint tri_pts[] = {
        GPoint(sub_center.x, sub_center.y - SUBDIAL_RADIUS + 3),
        GPoint(sub_center.x - 3, sub_center.y - SUBDIAL_RADIUS - 2),
        GPoint(sub_center.x + 3, sub_center.y - SUBDIAL_RADIUS - 2)
    };
    graphics_context_set_fill_color(ctx, GColorRed);
    GPathInfo tri_info = { .num_points = 3, .points = tri_pts };
    GPath *tri_path = gpath_create(&tri_info);
    gpath_draw_filled(ctx, tri_path);
    gpath_destroy(tri_path);
}

// ============================================================================
// DRAW: Date window at 3 o'clock
// ============================================================================

static void draw_date_window(GContext *ctx, int mday) {
    // Center in the marker zone at 3 o'clock
    int date_x = s_center.x + (MARKER_INNER_R + MARKER_OUTER_R) / 2 - DATE_WIN_W / 2;
    int date_y = s_center.y - DATE_WIN_H / 2;
    GRect win = GRect(date_x, date_y, DATE_WIN_W, DATE_WIN_H);

    // White background
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, win, 1, GCornersAll);

    // Border
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, win);

    // Date number
    char date_buf[4];
    snprintf(date_buf, sizeof(date_buf), "%d", mday);
    graphics_context_set_text_color(ctx, GColorBlack);
    GRect text_rect = GRect(date_x, date_y - 2, DATE_WIN_W, DATE_WIN_H);
    graphics_draw_text(ctx, date_buf,
        fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
        text_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: Brand text
// ============================================================================

static void draw_brand_text(GContext *ctx) {
    GPoint sub_center = GPoint(s_center.x, s_center.y + SUBDIAL_OFFSET_Y);
    int text_y = sub_center.y + SUBDIAL_RADIUS + 2;

    GRect brand_rect = GRect(s_center.x - 40, text_y, 80, 14);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SKY GMT",
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        brand_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: Clock hands
// ============================================================================

static void draw_hand(GContext *ctx, GPath *path, int32_t angle, GColor fill, int slit_width) {
    gpath_rotate_to(path, angle);
    gpath_move_to(path, s_center);

    // Outer fill
    graphics_context_set_fill_color(ctx, fill);
    gpath_draw_filled(ctx, path);

    // Outline
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, path);

    // Skeletonized center slit
    if (slit_width > 0) {
        int slit_len = (slit_width == HOUR_HAND_WIDTH) ? HOUR_HAND_LEN - 18 : MIN_HAND_LEN - 18;
        GPoint slit_end = point_on_circle(s_center, slit_len, angle);
        GPoint slit_start = point_on_circle(s_center, 14, angle);
        graphics_context_set_stroke_color(ctx, GColorDarkGray);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_line(ctx, slit_start, slit_end);
    }
}

static void draw_hands(GContext *ctx, struct tm *t) {
    int32_t hour_angle = ((t->tm_hour % 12) * TRIG_MAX_ANGLE / 12) +
                         (t->tm_min * TRIG_MAX_ANGLE / 12 / 60);
    int32_t min_angle = (t->tm_min * TRIG_MAX_ANGLE / 60);

    // Minute hand first (under hour hand)
    draw_hand(ctx, s_min_path, min_angle, GColorWhite, MIN_HAND_WIDTH);
    draw_hand(ctx, s_hour_path, hour_angle, GColorWhite, HOUR_HAND_WIDTH);

    // Center pinion
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, s_center, 5);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, s_center, 2);
}

// ============================================================================
// MAIN CANVAS UPDATE
// ============================================================================

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    // Background
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // Dial background circle
    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    graphics_fill_circle(ctx, s_center, DIAL_RADIUS);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return;

    // Draw layers back to front
    draw_bezel(ctx);
    draw_minute_track(ctx);
    draw_month_indicators(ctx, t->tm_mon);
    draw_hour_markers(ctx);
    draw_24h_subdial(ctx, t->tm_hour, t->tm_min);
    draw_earth_icon(ctx);
    draw_brand_text(ctx);
    draw_date_window(ctx, t->tm_mday);
    draw_hands(ctx, t);
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

    s_screen_w = bounds.size.w;
    s_screen_h = bounds.size.h;
    s_center = GPoint(s_screen_w / 2, s_screen_h / 2);

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    init_hand_points(s_hour_pts, HOUR_HAND_LEN, HOUR_HAND_WIDTH, HOUR_HAND_TAIL);
    init_hand_points(s_min_pts, MIN_HAND_LEN, MIN_HAND_WIDTH, MIN_HAND_TAIL);
    s_hour_path = gpath_create(&s_hour_info);
    s_min_path  = gpath_create(&s_min_info);
}

static void main_window_unload(Window *window) {
    if (s_hour_path) { gpath_destroy(s_hour_path); s_hour_path = NULL; }
    if (s_min_path)  { gpath_destroy(s_min_path);  s_min_path = NULL; }
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
    window_stack_push(s_main_window, true);
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    if (s_main_window) { window_destroy(s_main_window); s_main_window = NULL; }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}

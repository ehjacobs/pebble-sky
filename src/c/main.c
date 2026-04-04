/**
 * Sky GMT — Analog watchface with GMT complications for Pebble Time 2
 *
 * Features:
 *   - Analog dial with rectangular baton hour markers
 *   - Skeletonized hour and minute hands
 *   - 24-hour subdial (upper dial) with red triangle pointer
 *   - Month indicator ring (12 apertures, current month = red)
 *   - Date window at 3 o'clock
 *   - Fluted bezel texture
 */

#include <pebble.h>

// ============================================================================
// LAYOUT CONSTANTS — all derived from screen bounds at runtime
// ============================================================================

// Dial geometry (relative to center)
#define DIAL_RADIUS        90
#define BEZEL_OUTER        96
#define BEZEL_INNER        88
#define MONTH_RING_R       83
#define MARKER_OUTER_R     78
#define MARKER_INNER_R     68
#define MARKER_INNER_R_QTR 64   // Longer markers at 12/3/6/9

// 24-hour subdial
#define SUBDIAL_OFFSET_Y  (-38)  // Above center
#define SUBDIAL_RADIUS     22
#define SUBDIAL_NUM_R      16    // Where numbers sit

// Hand dimensions
#define HOUR_HAND_LEN      52
#define HOUR_HAND_WIDTH      5
#define HOUR_HAND_TAIL      12
#define MIN_HAND_LEN        72
#define MIN_HAND_WIDTH       4
#define MIN_HAND_TAIL       14

// Date window
#define DATE_WIN_W          22
#define DATE_WIN_H          16

// ============================================================================
// GLOBALS
// ============================================================================

static Window *s_main_window;
static Layer  *s_canvas_layer;

static GPoint s_center;
static int    s_screen_w;
static int    s_screen_h;

// Pre-allocated hand paths
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
    // 6-point elongated hexagon: tip -> right shoulder -> right base -> tail -> left base -> left shoulder
    pts[0] = GPoint(0, -length);               // tip
    pts[1] = GPoint(width, -length + 10);      // right shoulder
    pts[2] = GPoint(width, tail);              // right base
    pts[3] = GPoint(0, tail + 4);             // tail point
    pts[4] = GPoint(-width, tail);            // left base
    pts[5] = GPoint(-width, -length + 10);    // left shoulder
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
    // Draw radial fluting lines around the outer ring
    for (int i = 0; i < 120; i++) {
        int32_t angle = (i * TRIG_MAX_ANGLE) / 120;
        GPoint inner = point_on_circle(s_center, BEZEL_INNER, angle);
        GPoint outer = point_on_circle(s_center, BEZEL_OUTER, angle);

        // Alternate between light and dark to simulate fluting
        if (i % 2 == 0) {
            graphics_context_set_stroke_color(ctx, GColorLightGray);
        } else {
            graphics_context_set_stroke_color(ctx, GColorDarkGray);
        }
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_line(ctx, inner, outer);
    }
}

// ============================================================================
// DRAW: Month indicator ring
// ============================================================================

static void draw_month_indicators(GContext *ctx, int current_month) {
    // 12 small squares around the dial, one per hour position
    // current_month: 0=Jan at 1 o'clock, 1=Feb at 2 o'clock, ... 11=Dec at 12 o'clock
    for (int i = 0; i < 12; i++) {
        // Hour positions: 12 o'clock = index 0 in angle, but month mapping:
        // Jan=1 o'clock, Feb=2, ..., Dec=12 o'clock
        // So month i maps to hour (i+1), and hour h has angle h*30 degrees
        int hour = (i + 1) % 12;  // Dec (i=11) -> hour 0 (12 o'clock position)
        int32_t angle = (hour * TRIG_MAX_ANGLE) / 12;

        GPoint pos = point_on_circle(s_center, MONTH_RING_R, angle);

        // Small 5x5 square
        GRect sq = GRect(pos.x - 2, pos.y - 2, 5, 5);

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
    graphics_context_set_stroke_color(ctx, GColorWhite);

    for (int i = 0; i < 12; i++) {
        int32_t angle = (i * TRIG_MAX_ANGLE) / 12;

        // Quarter hours get longer, wider markers
        bool is_quarter = (i % 3 == 0);
        int inner_r = is_quarter ? MARKER_INNER_R_QTR : MARKER_INNER_R;
        int outer_r = MARKER_OUTER_R;

        GPoint inner = point_on_circle(s_center, inner_r, angle);
        GPoint outer = point_on_circle(s_center, outer_r, angle);

        graphics_context_set_stroke_width(ctx, is_quarter ? 4 : 2);
        graphics_draw_line(ctx, inner, outer);
    }
}

// ============================================================================
// DRAW: 24-hour subdial
// ============================================================================

static void draw_24h_subdial(GContext *ctx, int hour_24, int minutes) {
    GPoint sub_center = GPoint(s_center.x, s_center.y + SUBDIAL_OFFSET_Y);

    // Subdial background circle
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_circle(ctx, sub_center, SUBDIAL_RADIUS);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, sub_center, SUBDIAL_RADIUS);

    // Day/night shading: hours 6-18 are "day" (lighter), rest is "night" (darker)
    // Draw the night half as a darker arc
    // The disc rotates so that current hour points to the red triangle at top (12 o'clock of subdial)
    // Rotation angle: current hour maps to top
    int32_t hour_angle = ((hour_24 * TRIG_MAX_ANGLE) / 24) +
                         ((minutes * TRIG_MAX_ANGLE) / 24 / 60);

    // Draw hour tick marks on the subdial
    for (int h = 0; h < 24; h++) {
        // Each hour's position, offset by current time rotation
        // Hour h should appear at angle = (h * 360/24) - hour_angle (since disc rotates)
        int32_t tick_angle = (h * TRIG_MAX_ANGLE / 24) - hour_angle;

        GPoint inner_pt = point_on_circle(sub_center, SUBDIAL_RADIUS - 6, tick_angle);
        GPoint outer_pt = point_on_circle(sub_center, SUBDIAL_RADIUS - 1, tick_angle);

        // Day hours in white, night in dim gray
        if (h >= 6 && h < 18) {
            graphics_context_set_stroke_color(ctx, GColorWhite);
        } else {
            graphics_context_set_stroke_color(ctx, GColorLightGray);
        }
        graphics_context_set_stroke_width(ctx, (h % 6 == 0) ? 2 : 1);
        graphics_draw_line(ctx, inner_pt, outer_pt);

        // Draw select numbers (6, 12, 18, 24)
        if (h % 6 == 0) {
            GPoint num_pt = point_on_circle(sub_center, SUBDIAL_NUM_R - 2, tick_angle);
            char buf[3];
            int display_h = (h == 0) ? 24 : h;
            snprintf(buf, sizeof(buf), "%d", display_h);

            GRect text_box = GRect(num_pt.x - 8, num_pt.y - 6, 16, 12);
            graphics_context_set_text_color(ctx, GColorWhite);
            graphics_draw_text(ctx, buf,
                fonts_get_system_font(FONT_KEY_GOTHIC_14),
                text_box, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        }
    }

    // Red inverted triangle pointer at 12 o'clock of subdial
    GPoint tri_top = GPoint(sub_center.x, sub_center.y - SUBDIAL_RADIUS - 2);
    GPoint tri_left = GPoint(sub_center.x - 4, sub_center.y - SUBDIAL_RADIUS - 8);
    GPoint tri_right = GPoint(sub_center.x + 4, sub_center.y - SUBDIAL_RADIUS - 8);

    graphics_context_set_fill_color(ctx, GColorRed);
    GPoint tri_pts[] = { tri_top, tri_left, tri_right };
    GPathInfo tri_info = { .num_points = 3, .points = tri_pts };
    GPath *tri_path = gpath_create(&tri_info);
    gpath_draw_filled(ctx, tri_path);
    gpath_destroy(tri_path);
}

// ============================================================================
// DRAW: Date window at 3 o'clock
// ============================================================================

static void draw_date_window(GContext *ctx, int mday) {
    // Position: 3 o'clock, inside the hour markers
    int date_x = s_center.x + MARKER_INNER_R - 8;
    int date_y = s_center.y - DATE_WIN_H / 2;

    GRect win = GRect(date_x, date_y, DATE_WIN_W, DATE_WIN_H);

    // White background
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, win, 1, GCornersAll);

    // Black border
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_rect(ctx, win);

    // Date number
    char date_buf[4];
    snprintf(date_buf, sizeof(date_buf), "%d", mday);
    graphics_context_set_text_color(ctx, GColorBlack);
    GRect text_rect = GRect(date_x, date_y - 1, DATE_WIN_W, DATE_WIN_H);
    graphics_draw_text(ctx, date_buf,
        fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
        text_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: Brand text
// ============================================================================

static void draw_brand_text(GContext *ctx) {
    // Brand text below the subdial
    GPoint sub_center = GPoint(s_center.x, s_center.y + SUBDIAL_OFFSET_Y);
    int text_y = sub_center.y + SUBDIAL_RADIUS + 4;

    GRect brand_rect = GRect(s_center.x - 50, text_y, 100, 16);
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

    // Skeletonized slit — draw a thin line in background color down the center
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
    // Hour hand angle (includes minute interpolation)
    int32_t hour_angle = ((t->tm_hour % 12) * TRIG_MAX_ANGLE / 12) +
                         (t->tm_min * TRIG_MAX_ANGLE / 12 / 60);

    // Minute hand angle
    int32_t min_angle = (t->tm_min * TRIG_MAX_ANGLE / 60);

    // Draw minute hand first (under hour hand)
    draw_hand(ctx, s_min_path, min_angle, GColorWhite, MIN_HAND_WIDTH);

    // Draw hour hand on top
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

    // Get current time
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return;

    // Draw layers back to front
    draw_bezel(ctx);
    draw_month_indicators(ctx, t->tm_mon);  // tm_mon is 0-11
    draw_hour_markers(ctx);
    draw_24h_subdial(ctx, t->tm_hour, t->tm_min);
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

    // Canvas
    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    // Initialize hand paths
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

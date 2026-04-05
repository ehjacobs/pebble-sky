/**
 * Sky GMT — Analog watchface with GMT complications for Pebble Time 2
 *
 * Features:
 *   - Analog dial with rectangular baton hour markers
 *   - Skeletonized hour and minute hands
 *   - 24-hour GMT ring (off-center annulus with rotating numbers)
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
#define MARKER_INNER_R     66
#define MARKER_INNER_R_QTR 60

// GMT ring (off-center 24-hour annulus, shifted below dial center)
#define GMT_DISC_OFFSET_Y   17     // Disc center below dial center
#define GMT_RING_OUTER      51     // Outer radius of ring
#define GMT_RING_INNER      35     // Inner radius (donut hole)
#define GMT_NUM_R           43     // Radius for number placement

// Hand dimensions
#define HOUR_HAND_LEN      50
#define HOUR_HAND_WIDTH     8
#define HOUR_HAND_TAIL     12
#define MIN_HAND_LEN       72
#define MIN_HAND_WIDTH      5
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
static GBitmap *s_gmt_bitmap;

static GPoint s_hour_pts[6];
static GPoint s_min_pts[6];

static GPathInfo s_hour_info = { .num_points = 6, .points = s_hour_pts };
static GPathInfo s_min_info  = { .num_points = 6, .points = s_min_pts };

// ============================================================================
// HAND GEOMETRY — flat-tipped baton with luminous center strip
// ============================================================================

static void init_hand_points(GPoint *pts, int length, int width, int tail) {
    // Flat-tipped baton: polished metal look with slight shoulder taper
    pts[0] = GPoint(-width + 1, -length);       // tip left
    pts[1] = GPoint(width - 1, -length);        // tip right
    pts[2] = GPoint(width, -length + 5);        // right shoulder
    pts[3] = GPoint(width, tail);               // right base
    pts[4] = GPoint(-width, tail);              // left base
    pts[5] = GPoint(-width, -length + 5);       // left shoulder
}

// ============================================================================
// DRAWING HELPERS
// ============================================================================

static int16_t trig_round(int32_t val) {
    // Round to nearest instead of truncating toward zero
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
// DRAW: Fluted bezel
// ============================================================================

static void draw_bezel(GContext *ctx) {
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, s_center, BEZEL_OUTER);

    for (int i = 0; i < 120; i++) {
        int32_t angle = (i * TRIG_MAX_ANGLE) / 120;
        GPoint inner = point_on_circle(s_center, BEZEL_INNER, angle);
        GPoint outer = point_on_circle(s_center, BEZEL_OUTER, angle);

        graphics_context_set_stroke_color(ctx, (i % 2 == 0) ? GColorLightGray : GColorDarkGray);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_line(ctx, inner, outer);
    }

    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, s_center, BEZEL_INNER);
}

// ============================================================================
// DRAW: Minute track
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
    // Rotated rectangles aligned radially, same width as hour batons
    GPoint rect_pts[4];
    GPathInfo rect_info = { .num_points = 4, .points = rect_pts };

    for (int i = 0; i < 12; i++) {
        int hour = (i + 1) % 12;
        int32_t angle = (hour * TRIG_MAX_ANGLE) / 12;

        GPoint inner = point_on_circle(s_center, MONTH_RING_R - 4, angle);
        GPoint outer = point_on_circle(s_center, MONTH_RING_R + 4, angle);

        // Perpendicular offset for square apertures (~8px wide)
        int32_t perp_angle = angle + TRIG_MAX_ANGLE / 4;
        int16_t dx = (int16_t)((sin_lookup(perp_angle) * 4) / TRIG_MAX_RATIO);
        int16_t dy = (int16_t)(-(cos_lookup(perp_angle) * 4) / TRIG_MAX_RATIO);

        rect_pts[0] = GPoint(outer.x + dx, outer.y + dy);
        rect_pts[1] = GPoint(outer.x - dx, outer.y - dy);
        rect_pts[2] = GPoint(inner.x - dx, inner.y - dy);
        rect_pts[3] = GPoint(inner.x + dx, inner.y + dy);

        if (i == current_month) {
            graphics_context_set_fill_color(ctx, GColorRed);
        } else {
            graphics_context_set_fill_color(ctx, GColorWhite);
        }

        GPath *rect_path = gpath_create(&rect_info);
        gpath_draw_filled(ctx, rect_path);
        gpath_destroy(rect_path);
    }
}

// ============================================================================
// DRAW: Hour markers (baton indices)
// ============================================================================

static void draw_hour_markers(GContext *ctx) {
    GPoint rect_pts[4];
    GPathInfo rect_info = { .num_points = 4, .points = rect_pts };

    for (int i = 0; i < 12; i++) {
        // Skip 12 (earth icon) and 3 (date window)
        if (i == 0 || i == 3) continue;

        int32_t angle = (i * TRIG_MAX_ANGLE) / 12;

        GPoint inner = point_on_circle(s_center, MARKER_INNER_R, angle);
        GPoint outer = point_on_circle(s_center, MARKER_OUTER_R, angle);

        // Perpendicular offset for ~4px wide baton (round instead of truncate)
        int32_t perp_angle = angle + TRIG_MAX_ANGLE / 4;
        int32_t dx_raw = sin_lookup(perp_angle) * 2;
        int32_t dy_raw = -cos_lookup(perp_angle) * 2;
        int16_t dx = (int16_t)((dx_raw + (dx_raw > 0 ? TRIG_MAX_RATIO/2 : -TRIG_MAX_RATIO/2)) / TRIG_MAX_RATIO);
        int16_t dy = (int16_t)((dy_raw + (dy_raw > 0 ? TRIG_MAX_RATIO/2 : -TRIG_MAX_RATIO/2)) / TRIG_MAX_RATIO);

        rect_pts[0] = GPoint(outer.x + dx, outer.y + dy);
        rect_pts[1] = GPoint(outer.x - dx, outer.y - dy);
        rect_pts[2] = GPoint(inner.x - dx, inner.y - dy);
        rect_pts[3] = GPoint(inner.x + dx, inner.y + dy);

        graphics_context_set_fill_color(ctx, GColorWhite);
        GPath *rect_path = gpath_create(&rect_info);
        gpath_draw_filled(ctx, rect_path);
        gpath_destroy(rect_path);
    }
}

// ============================================================================
// DRAW: Earth icon + UTC at 12 o'clock
// ============================================================================

static void draw_earth_icon(GContext *ctx) {
    int earth_y = s_center.y - MARKER_OUTER_R + 5;
    GPoint earth = GPoint(s_center.x, earth_y);
    int r = 6;

    // Globe outline
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, earth, r);

    // Equator
    graphics_draw_line(ctx, GPoint(earth.x - r, earth.y),
                            GPoint(earth.x + r, earth.y));

    // Prime meridian (slightly elliptical — offset lines for curvature)
    graphics_draw_line(ctx, GPoint(earth.x, earth.y - r),
                            GPoint(earth.x, earth.y + r));

    // Latitude lines (tropics) for globe effect
    graphics_draw_line(ctx, GPoint(earth.x - r + 1, earth.y - 3),
                            GPoint(earth.x + r - 1, earth.y - 3));
    graphics_draw_line(ctx, GPoint(earth.x - r + 1, earth.y + 3),
                            GPoint(earth.x + r - 1, earth.y + 3));

    // Curved meridian offsets to suggest 3D
    graphics_draw_line(ctx, GPoint(earth.x - 3, earth.y - r + 1),
                            GPoint(earth.x - 3, earth.y + r - 1));
    graphics_draw_line(ctx, GPoint(earth.x + 3, earth.y - r + 1),
                            GPoint(earth.x + 3, earth.y + r - 1));

    // "ZULU" label below (shifted down)
    GRect utc_rect = GRect(s_center.x - 16, earth_y + r + 0, 32, 14);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "ZULU",
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        utc_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: 24-hour GMT ring (off-center annulus)
// ============================================================================

static void draw_gmt_ring(GContext *ctx, int hour_24, int minutes) {
    GPoint disc_center = GPoint(s_center.x, s_center.y + GMT_DISC_OFFSET_Y);

    GRect ring_rect = GRect(disc_center.x - GMT_RING_OUTER,
                            disc_center.y - GMT_RING_OUTER,
                            GMT_RING_OUTER * 2,
                            GMT_RING_OUTER * 2);

    int ring_thickness = GMT_RING_OUTER - GMT_RING_INNER;

    // Hour rotation angle
    int32_t hour_angle = ((hour_24 * TRIG_MAX_ANGLE) / 24) +
                         ((minutes * TRIG_MAX_ANGLE) / 24 / 60);

    // Fill the full ring in dark navy
    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    graphics_fill_radial(ctx, ring_rect, GOvalScaleModeFitCircle,
                         ring_thickness, 0, TRIG_MAX_ANGLE);

    // Ring borders
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, disc_center, GMT_RING_OUTER);
    graphics_draw_circle(ctx, disc_center, GMT_RING_INNER);

    // Draw rotated disc face (numbers + ticks from pre-rendered bitmap)
    if (s_gmt_bitmap) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        int32_t rotation = TRIG_MAX_ANGLE - hour_angle;
        graphics_draw_rotated_bitmap(ctx, s_gmt_bitmap,
            GPoint(54, 54),    // bitmap center
            rotation,
            disc_center);
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    }

    // Red inverted triangle with white interior, above the ring
    GPoint tri_pts[] = {
        GPoint(disc_center.x, disc_center.y - GMT_RING_OUTER - 1),
        GPoint(disc_center.x - 7, disc_center.y - GMT_RING_OUTER - 14),
        GPoint(disc_center.x + 7, disc_center.y - GMT_RING_OUTER - 14)
    };
    graphics_context_set_fill_color(ctx, GColorRed);
    GPathInfo tri_info = { .num_points = 3, .points = tri_pts };
    GPath *tri_path = gpath_create(&tri_info);
    gpath_draw_filled(ctx, tri_path);
    gpath_destroy(tri_path);

    // White interior
    GPoint inner_pts[] = {
        GPoint(disc_center.x, disc_center.y - GMT_RING_OUTER - 4),
        GPoint(disc_center.x - 4, disc_center.y - GMT_RING_OUTER - 10),
        GPoint(disc_center.x + 4, disc_center.y - GMT_RING_OUTER - 10)
    };
    graphics_context_set_fill_color(ctx, GColorWhite);
    GPathInfo inner_info = { .num_points = 3, .points = inner_pts };
    GPath *inner_path = gpath_create(&inner_info);
    gpath_draw_filled(ctx, inner_path);
    gpath_destroy(inner_path);
}

// ============================================================================
// DRAW: Date window at 3 o'clock
// ============================================================================

static void draw_date_window(GContext *ctx, int mday) {
    // Cyclops center at 3 o'clock
    int cx = s_center.x + (MARKER_INNER_R + MARKER_OUTER_R) / 2;
    int cy = s_center.y;
    int date_w = 20;  // white date rectangle
    int date_h = 14;
    int pad = 4;      // grey lens padding around date rect
    int lens_w = date_w + pad * 2;
    int lens_h = date_h + pad * 2;
    int corner_r = lens_h / 2;  // fully round left/right ends

    GRect lens_rect = GRect(cx - lens_w / 2, cy - lens_h / 2, lens_w, lens_h);

    // Grey cyclops lens (capsule shape: round left/right, flat top/bottom)
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, lens_rect, corner_r, GCornersAll);

    // Lens border
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_round_rect(ctx, lens_rect, corner_r);

    // White date rectangle inside the lens
    GRect date_rect = GRect(cx - date_w / 2, cy - date_h / 2, date_w, date_h);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, date_rect, 0, GCornerNone);

    // Date number
    char date_buf[4];
    snprintf(date_buf, sizeof(date_buf), "%d", mday);
    graphics_context_set_text_color(ctx, GColorBlack);
    GRect text_rect = GRect(cx - 10, cy - 12, 20, 18);
    graphics_draw_text(ctx, date_buf,
        fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
        text_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: Brand text
// ============================================================================

static void draw_brand_text(GContext *ctx) {
    // Just below center, inside the GMT ring hole
    int text_y = s_center.y + 14;
    GRect brand_rect = GRect(s_center.x - 20, text_y, 40, 14);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SKY",
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        brand_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: Clock hands
// ============================================================================

static void draw_hand(GContext *ctx, GPath *path, int32_t angle, int lume_width, int hand_len) {
    gpath_rotate_to(path, angle);
    gpath_move_to(path, s_center);

    // Polished metal body
    graphics_context_set_fill_color(ctx, GColorLightGray);
    gpath_draw_filled(ctx, path);

    // Black outline
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, path);

    // Luminous center strip (white)
    GPoint lume_end = point_on_circle(s_center, hand_len - 6, angle);
    GPoint lume_start = point_on_circle(s_center, 10, angle);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, lume_width);
    graphics_draw_line(ctx, lume_start, lume_end);
}

static void draw_hands(GContext *ctx, struct tm *t) {
    int32_t hour_angle = ((t->tm_hour % 12) * TRIG_MAX_ANGLE / 12) +
                         (t->tm_min * TRIG_MAX_ANGLE / 12 / 60);
    int32_t min_angle = (t->tm_min * TRIG_MAX_ANGLE / 60);

    draw_hand(ctx, s_min_path, min_angle, 2, MIN_HAND_LEN);
    draw_hand(ctx, s_hour_path, hour_angle, 4, HOUR_HAND_LEN);

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

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // Lighter blue dial
    graphics_context_set_fill_color(ctx, GColorDukeBlue);
    graphics_fill_circle(ctx, s_center, DIAL_RADIUS);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return;

    draw_hour_markers(ctx);
    draw_gmt_ring(ctx, t->tm_hour, t->tm_min);
    draw_bezel(ctx);
    draw_minute_track(ctx);
    draw_month_indicators(ctx, t->tm_mon);
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
    s_gmt_bitmap = gbitmap_create_with_resource(RESOURCE_ID_GMT_DISC);
}

static void main_window_unload(Window *window) {
    if (s_gmt_bitmap) { gbitmap_destroy(s_gmt_bitmap); s_gmt_bitmap = NULL; }
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

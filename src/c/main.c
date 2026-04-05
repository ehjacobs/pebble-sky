/**
 * Sky GMT — Analog watchface with GMT complications for Pebble Round 2
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
#define MARKER_OUTER_R     109
#define MARKER_INNER_R      83

// GMT ring (off-center 24-hour annulus, shifted below dial center)
#define GMT_DISC_OFFSET_Y   26
#define GMT_RING_OUTER      73
#define GMT_RING_INNER      49
#define GMT_NUM_R           61

// Hand dimensions
#define HOUR_HAND_LEN       80
#define HOUR_HAND_WIDTH     11
#define HOUR_HAND_TAIL      17
#define MIN_HAND_LEN       119
#define MIN_HAND_WIDTH       7
#define MIN_HAND_TAIL       20
#define SEC_HAND_LEN       110
#define SEC_HAND_TAIL       25

// GMT disc bitmap center (150×150 image)
#define GMT_BITMAP_CENTER   75

// ============================================================================
// GLOBALS
// ============================================================================

static Window *s_main_window;
static Layer  *s_canvas_layer;

static GPoint s_center;

static GPath *s_hour_path;
static GPath *s_min_path;
static GBitmap *s_gmt_bitmap;

static GPoint s_hour_pts[4];
static GPoint s_min_pts[4];

static GPathInfo s_hour_info = { .num_points = 4, .points = s_hour_pts };
static GPathInfo s_min_info  = { .num_points = 4, .points = s_min_pts };

// ============================================================================
// HAND GEOMETRY — gradually tapered baton (Sky-Dweller style)
// ============================================================================

static void init_hand_points(GPoint *pts, int length, int width, int tail) {
    int tip_w = width - 2;  // subtle taper: slightly narrower at tip
    pts[0] = GPoint(-tip_w, -length);      // tip left
    pts[1] = GPoint( tip_w, -length);      // tip right
    pts[2] = GPoint( width,  tail);        // base right (full width)
    pts[3] = GPoint(-width,  tail);        // base left
}

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
// DRAW: Dial edge (clean polished ring replacing fluted bezel)
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
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);

    // 120 positions = every 30 seconds
    for (int i = 0; i < 120; i++) {
        int pos = i % 10;
        if (pos == 0) continue;               // hour marker position
        if (pos == 1 || pos == 9) continue;    // too close to hour marker

        int32_t angle = (i * TRIG_MAX_ANGLE) / 120;
        int inner_r;

        if (pos % 2 == 0) {
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
    int32_t r_out = MONTH_RING_R + 5;
    int32_t r_in = MONTH_RING_R - 5;
    int32_t hw = 5;

    for (int i = 0; i < 12; i++) {
        int hour = (i + 1) % 12;
        int32_t angle = (hour * TRIG_MAX_ANGLE) / 12;
        int32_t sa = sin_lookup(angle);
        int32_t ca = cos_lookup(angle);

        GPoint pts[4];
        pts[0] = GPoint(s_center.x + trig_round(sa * r_out + ca * hw),
                        s_center.y - trig_round(ca * r_out - sa * hw));
        pts[1] = GPoint(s_center.x + trig_round(sa * r_out - ca * hw),
                        s_center.y - trig_round(ca * r_out + sa * hw));
        pts[2] = GPoint(s_center.x + trig_round(sa * r_in - ca * hw),
                        s_center.y - trig_round(ca * r_in + sa * hw));
        pts[3] = GPoint(s_center.x + trig_round(sa * r_in + ca * hw),
                        s_center.y - trig_round(ca * r_in - sa * hw));

        graphics_context_set_fill_color(ctx,
            (i == current_month) ? GColorRed : GColorWhite);

        GPathInfo info = { .num_points = 4, .points = pts };
        GPath *path = gpath_create(&info);
        gpath_draw_filled(ctx, path);
        gpath_destroy(path);
    }
}

// ============================================================================
// DRAW: Hour markers (baton indices)
// ============================================================================

static void draw_hour_markers(GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorWhite);

    for (int i = 0; i < 12; i++) {
        if (i == 0 || i == 3) continue;

        int32_t angle = (i * TRIG_MAX_ANGLE) / 12;
        int32_t sa = sin_lookup(angle);
        int32_t ca = cos_lookup(angle);
        int32_t hw = 5;
        int32_t r_out = MARKER_OUTER_R;
        int32_t r_in = MARKER_INNER_R;

        // Compute each vertex in one trig_round to minimize rounding error
        GPoint pts[4];
        pts[0] = GPoint(s_center.x + trig_round(sa * r_out + ca * hw),
                        s_center.y - trig_round(ca * r_out - sa * hw));
        pts[1] = GPoint(s_center.x + trig_round(sa * r_out - ca * hw),
                        s_center.y - trig_round(ca * r_out + sa * hw));
        pts[2] = GPoint(s_center.x + trig_round(sa * r_in - ca * hw),
                        s_center.y - trig_round(ca * r_in + sa * hw));
        pts[3] = GPoint(s_center.x + trig_round(sa * r_in + ca * hw),
                        s_center.y - trig_round(ca * r_in - sa * hw));

        GPathInfo info = { .num_points = 4, .points = pts };
        GPath *path = gpath_create(&info);
        gpath_draw_filled(ctx, path);
        gpath_destroy(path);
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

static void draw_gmt_ring(GContext *ctx, int hour_24, int minutes) {
    GPoint disc_center = GPoint(s_center.x, s_center.y + GMT_DISC_OFFSET_Y);

    GRect ring_rect = GRect(disc_center.x - GMT_RING_OUTER,
                            disc_center.y - GMT_RING_OUTER,
                            GMT_RING_OUTER * 2,
                            GMT_RING_OUTER * 2);

    int ring_thickness = GMT_RING_OUTER - GMT_RING_INNER;

    int32_t hour_angle = ((hour_24 * TRIG_MAX_ANGLE) / 24) +
                         ((minutes * TRIG_MAX_ANGLE) / 24 / 60);

    // Navy ring fill
    graphics_context_set_fill_color(ctx, GColorOxfordBlue);
    graphics_fill_radial(ctx, ring_rect, GOvalScaleModeFitCircle,
                         ring_thickness, 0, TRIG_MAX_ANGLE);

    // Ring borders
    graphics_context_set_stroke_color(ctx, GColorLightGray);
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

    // Red inverted triangle pointer above the ring
    GPoint tri_pts[] = {
        GPoint(disc_center.x, disc_center.y - GMT_RING_OUTER - 1),
        GPoint(disc_center.x - 10, disc_center.y - GMT_RING_OUTER - 22),
        GPoint(disc_center.x + 10, disc_center.y - GMT_RING_OUTER - 22)
    };
    graphics_context_set_fill_color(ctx, GColorRed);
    GPathInfo tri_info = { .num_points = 3, .points = tri_pts };
    GPath *tri_path = gpath_create(&tri_info);
    gpath_draw_filled(ctx, tri_path);
    gpath_destroy(tri_path);

    // White inner triangle
    GPoint inner_pts[] = {
        GPoint(disc_center.x, disc_center.y - GMT_RING_OUTER - 5),
        GPoint(disc_center.x - 6, disc_center.y - GMT_RING_OUTER - 16),
        GPoint(disc_center.x + 6, disc_center.y - GMT_RING_OUTER - 16)
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
    int cx = s_center.x + (MARKER_INNER_R + MARKER_OUTER_R) / 2 - 2;
    int cy = s_center.y;
    int date_w = 26;
    int date_h = 18;
    int pad = 5;
    int lens_w = date_w + pad * 2;
    int lens_h = date_h + pad * 2;
    int corner_r = lens_h / 2;

    GRect lens_rect = GRect(cx - lens_w / 2, cy - lens_h / 2, lens_w, lens_h);

    // Grey lens
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, lens_rect, corner_r, GCornersAll);

    // Lens border
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_round_rect(ctx, lens_rect, corner_r);

    // White date rectangle
    GRect date_rect = GRect(cx - date_w / 2, cy - date_h / 2, date_w, date_h);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, date_rect, 0, GCornerNone);

    // Date number
    char date_buf[4];
    snprintf(date_buf, sizeof(date_buf), "%d", mday);
    graphics_context_set_text_color(ctx, GColorBlack);
    GRect text_rect = GRect(cx - 14, cy - 17, 28, 24);
    graphics_draw_text(ctx, date_buf,
        fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
        text_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: Brand text
// ============================================================================

static void draw_brand_text(GContext *ctx) {
    int text_y = s_center.y + 18;
    GRect brand_rect = GRect(s_center.x - 25, text_y, 50, 18);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SKY",
        fonts_get_system_font(FONT_KEY_GOTHIC_18),
        brand_rect, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: Clock hands
// ============================================================================

static void draw_hand(GContext *ctx, GPath *path, int32_t angle,
                      int lume_width, int hand_len) {
    gpath_rotate_to(path, angle);
    gpath_move_to(path, s_center);

    // Polished metal body
    graphics_context_set_fill_color(ctx, GColorLightGray);
    gpath_draw_filled(ctx, path);

    // Black outline
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, path);

    // Skeleton cutout — rectangular opening showing dial through hand
    GPoint cut_start = point_on_circle(s_center, 18, angle);
    GPoint cut_end = point_on_circle(s_center, hand_len / 2 - 3, angle);
    graphics_context_set_stroke_color(ctx, GColorDukeBlue);
    graphics_context_set_stroke_width(ctx, lume_width);
    graphics_draw_line(ctx, cut_start, cut_end);

    // Luminous strip — outer portion only (grey bridge visible between)
    GPoint lume_start = point_on_circle(s_center, hand_len / 2 + 3, angle);
    GPoint lume_end = point_on_circle(s_center, hand_len - 6, angle);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, lume_width);
    graphics_draw_line(ctx, lume_start, lume_end);
}

static void draw_hands(GContext *ctx, struct tm *t) {
    int32_t hour_angle = ((t->tm_hour % 12) * TRIG_MAX_ANGLE / 12) +
                         (t->tm_min * TRIG_MAX_ANGLE / 12 / 60);
    int32_t min_angle = (t->tm_min * TRIG_MAX_ANGLE / 60);
    int32_t sec_angle = (t->tm_sec * TRIG_MAX_ANGLE / 60);

    // Draw order: hour (bottom), minute, seconds (top)
    draw_hand(ctx, s_hour_path, hour_angle, 5, HOUR_HAND_LEN);
    draw_hand(ctx, s_min_path, min_angle, 3, MIN_HAND_LEN);

    // Seconds hand — tapered from 3px at center to 1px at tip
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

    // Center pivot
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_circle(ctx, s_center, 7);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, s_center, 7);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, s_center, 2);
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

    draw_dial_edge(ctx);
    draw_minute_track(ctx);
    draw_month_indicators(ctx, t->tm_mon);
    draw_hour_markers(ctx);
    draw_earth_icon(ctx);
    draw_gmt_ring(ctx, t->tm_hour, t->tm_min);
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

    s_center = GPoint(bounds.size.w / 2, bounds.size.h / 2);

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
    if (s_hour_path)  { gpath_destroy(s_hour_path);    s_hour_path = NULL; }
    if (s_min_path)   { gpath_destroy(s_min_path);     s_min_path = NULL; }
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
    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
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

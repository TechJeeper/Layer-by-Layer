#include "face.h"
#include "digits.h"

#define FRAME_MS 100
#define DROP_MS 800
#define COLON_GAP 6

typedef enum {
  STATE_PRINTING = 0,
  STATE_DROPPING,
} FaceState;

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;

static FaceState s_state;
static int s_hour;
static int s_minute;
static int s_second;
static int s_phase;          // 0..9 sub-second steps (100 ms)
static int s_drop_ms;
static int s_drop_hour;
static int s_drop_minute;
static int s_cell;           // pixel size of one glyph cell
static int s_digit_w;
static int s_digit_h;
static int s_gap;

static GColor color_bg(void) {
#if defined(PBL_COLOR)
  return GColorBlack;
#else
  return GColorBlack;
#endif
}

static GColor color_filament(void) {
#if defined(PBL_COLOR)
  return GColorCyan;
#else
  return GColorWhite;
#endif
}

static GColor color_nozzle(void) {
#if defined(PBL_COLOR)
  return GColorOrange;
#else
  return GColorWhite;
#endif
}

static GColor color_bed(void) {
#if defined(PBL_COLOR)
  return GColorDarkGray;
#else
  return GColorBlack;
#endif
}

static GColor color_rail(void) {
#if defined(PBL_COLOR)
  return GColorLightGray;
#else
  return GColorWhite;
#endif
}

static GColor color_dim(void) {
#if defined(PBL_COLOR)
  return GColorDarkGray;
#else
  return GColorWhite;
#endif
}

static void layout_metrics(GRect bounds) {
  // Scale glyphs so four digits + colon fit with side margins.
  int usable = bounds.size.w - 16;
  int cell = usable / (4 * DIGIT_COLS + 3);
  if (cell < 2) {
    cell = 2;
  }
  if (cell > 5) {
    cell = 5;
  }
  // Prefer taller digits on tall screens (Time 2 / Duo).
  if (bounds.size.h >= 200 && cell < 4) {
    cell = 4;
  }
  s_cell = cell;
  s_digit_w = DIGIT_COLS * cell;
  s_digit_h = DIGIT_ROWS * cell;
  s_gap = cell;
}

static int time_block_width(void) {
  return 4 * s_digit_w + 3 * s_gap + COLON_GAP;
}

static void digits_from_time(int hour, int minute, int out[4]) {
  if (!clock_is_24h_style()) {
    hour = hour % 12;
    if (hour == 0) {
      hour = 12;
    }
  }
  out[0] = hour / 10;
  out[1] = hour % 10;
  out[2] = minute / 10;
  out[3] = minute % 10;
}

// Progress through the current minute in permille: 0 at :00, 1000 near :59.9
static int print_progress_pm(void) {
  int pm = (s_second * 1000 + s_phase * 100) / 60;
  if (pm < 0) {
    return 0;
  }
  if (pm > 1000) {
    return 1000;
  }
  return pm;
}

static void draw_glyph_rows(GContext *ctx, GPoint origin, int digit, int rows_from_bottom,
                            int y_offset, GColor color) {
  if (digit < 0 || digit > 9 || rows_from_bottom <= 0) {
    return;
  }
  graphics_context_set_fill_color(ctx, color);
  int max_rows = rows_from_bottom;
  if (max_rows > DIGIT_ROWS) {
    max_rows = DIGIT_ROWS;
  }
  // Glyph row 0 is the top of the digit; we print from the bottom up.
  for (int gr = DIGIT_ROWS - max_rows; gr < DIGIT_ROWS; gr++) {
    uint8_t bits = DIGIT_GLYPHS[digit][gr];
    for (int gc = 0; gc < DIGIT_COLS; gc++) {
      if (bits & (1 << (DIGIT_COLS - 1 - gc))) {
        int x = origin.x + gc * s_cell;
        int y = origin.y + gr * s_cell + y_offset;
        graphics_fill_rect(ctx, GRect(x, y, s_cell, s_cell), 0, GCornerNone);
      }
    }
  }
}

static void draw_colon(GContext *ctx, GPoint origin, int rows_from_bottom, int y_offset, GColor color) {
  // Two block dots, clipped to the same printed height as the digits.
  int printed_px = rows_from_bottom * s_cell;
  int cut = origin.y + y_offset + s_digit_h - printed_px;

  graphics_context_set_fill_color(ctx, color);
  GRect dots[2] = {
    GRect(origin.x, origin.y + 2 * s_cell + y_offset, s_cell, s_cell),
    GRect(origin.x, origin.y + 4 * s_cell + y_offset, s_cell, s_cell),
  };
  for (int i = 0; i < 2; i++) {
    if (dots[i].origin.y + dots[i].size.h > cut) {
      int y0 = dots[i].origin.y;
      if (y0 < cut) {
        int trim = cut - y0;
        dots[i].origin.y += trim;
        dots[i].size.h -= trim;
      }
      if (dots[i].size.h > 0) {
        graphics_fill_rect(ctx, dots[i], 0, GCornerNone);
      }
    }
  }
}

static void draw_time_block(GContext *ctx, GPoint origin, int hour, int minute,
                            int rows_from_bottom, int y_offset, GColor color) {
  int d[4];
  digits_from_time(hour, minute, d);
  int x = origin.x;
  for (int i = 0; i < 4; i++) {
    if (i == 2) {
      draw_colon(ctx, GPoint(x, origin.y), rows_from_bottom, y_offset, color);
      x += s_cell + COLON_GAP;
    }
    draw_glyph_rows(ctx, GPoint(x, origin.y), d[i], rows_from_bottom, y_offset, color);
    x += s_digit_w + s_gap;
  }
}

static void draw_bed(GContext *ctx, GRect bed) {
  graphics_context_set_fill_color(ctx, color_bed());
  graphics_fill_rect(ctx, bed, 2, GCornersAll);

  graphics_context_set_stroke_color(ctx, color_rail());
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, bed);

  // Corner clips
  graphics_context_set_fill_color(ctx, color_rail());
  int c = 3;
  graphics_fill_rect(ctx, GRect(bed.origin.x - 1, bed.origin.y - 1, c, c), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bed.origin.x + bed.size.w - c + 1, bed.origin.y - 1, c, c), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bed.origin.x - 1, bed.origin.y + bed.size.h - c + 1, c, c), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bed.origin.x + bed.size.w - c + 1, bed.origin.y + bed.size.h - c + 1, c, c), 0, GCornerNone);

#if defined(PBL_COLOR)
  // Heater glow under the plate
  graphics_context_set_fill_color(ctx, GColorDarkCandyAppleRed);
  graphics_fill_rect(ctx,
      GRect(bed.origin.x + 4, bed.origin.y + bed.size.h - 1, bed.size.w - 8, 2), 0, GCornerNone);
#endif
}

static void draw_nozzle(GContext *ctx, GPoint tip) {
  // tip is the extrusion point (bottom center of the nozzle).
  GColor body = color_nozzle();
  graphics_context_set_fill_color(ctx, body);

  // Brass tip (triangle pointing down)
  for (int i = 0; i < 5; i++) {
    int half = i / 2;
    graphics_fill_rect(ctx, GRect(tip.x - half, tip.y - 5 + i, half * 2 + 1, 1), 0, GCornerNone);
  }
  // Heat block
  graphics_fill_rect(ctx, GRect(tip.x - 4, tip.y - 11, 9, 5), 0, GCornerNone);

#if defined(PBL_COLOR)
  graphics_context_set_fill_color(ctx, GColorLightGray);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
#endif
  // Heat sink fins
  for (int i = 0; i < 3; i++) {
    graphics_fill_rect(ctx, GRect(tip.x - 5, tip.y - 18 + i * 2, 11, 1), 0, GCornerNone);
  }
  // Filament path
  graphics_fill_rect(ctx, GRect(tip.x, tip.y - 22, 1, 4), 0, GCornerNone);
}

static void draw_frame(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  layout_metrics(bounds);

  graphics_context_set_fill_color(ctx, color_bg());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int block_w = time_block_width();
  int bed_w = block_w + 16;
  int bed_h = 10;
  int bed_x = (bounds.size.w - bed_w) / 2;
  int bed_y = bounds.size.h / 2 + s_digit_h / 2 + 4;
  if (bed_y + bed_h + 24 > bounds.size.h) {
    bed_y = bounds.size.h - bed_h - 28;
  }
  GRect bed = GRect(bed_x, bed_y, bed_w, bed_h);

  int digit_x = bed_x + (bed_w - block_w) / 2;
  int digit_y = bed_y - s_digit_h - 2;

  draw_bed(ctx, bed);

  // Vertical gantry rails
  graphics_context_set_stroke_color(ctx, color_dim());
  graphics_draw_line(ctx, GPoint(bed_x, 8), GPoint(bed_x, bed_y));
  graphics_draw_line(ctx, GPoint(bed_x + bed_w - 1, 8), GPoint(bed_x + bed_w - 1, bed_y));

  int progress_pm;
  int rows;
  int y_off = 0;
  int hour = s_hour;
  int minute = s_minute;
  bool show_nozzle = true;

  if (s_state == STATE_DROPPING) {
    progress_pm = 1000;
    rows = DIGIT_ROWS;
    hour = s_drop_hour;
    minute = s_drop_minute;
    // Ease-in fall off the bed (t^2 in integer math)
    int t = s_drop_ms;
    if (t > DROP_MS) {
      t = DROP_MS;
    }
    y_off = (t * t / DROP_MS) * (bounds.size.h - digit_y) / DROP_MS;
    show_nozzle = false;
  } else {
    progress_pm = print_progress_pm();
    rows = progress_pm * DIGIT_ROWS / 1000;
    if (rows > DIGIT_ROWS) {
      rows = DIGIT_ROWS;
    }
  }

  draw_time_block(ctx, GPoint(digit_x, digit_y), hour, minute, rows, y_off, color_filament());

  if (show_nozzle && rows < DIGIT_ROWS) {
    // Current layer being laid: `rows` complete layers from the bottom; nozzle on the next.
    int layer = rows;
    int row_pm = progress_pm * DIGIT_ROWS - rows * 1000; // 0..999 within the layer
    if (row_pm < 0) {
      row_pm = 0;
    }
    if (row_pm > 1000) {
      row_pm = 1000;
    }

    bool ltr = (layer % 2) == 0;
    int travel = block_w;
    int nx = ltr
      ? digit_x + row_pm * travel / 1000
      : digit_x + travel - row_pm * travel / 1000;

    // Tip sits on the layer being printed (glyph rows counted from the top).
    int glyph_row_from_top = DIGIT_ROWS - 1 - layer;
    int ny = digit_y + glyph_row_from_top * s_cell;
    draw_nozzle(ctx, GPoint(nx, ny));

    // Small extruded bead under the tip
    graphics_context_set_fill_color(ctx, color_filament());
    int bh = s_cell > 1 ? s_cell / 2 : 1;
    graphics_fill_rect(ctx, GRect(nx - 1, ny, 3, bh), 0, GCornerNone);
  } else if (show_nozzle) {
    // Finished print: park the nozzle at the top-right of the build.
    draw_nozzle(ctx, GPoint(digit_x + block_w, digit_y));
  }

  // Caption under the bed
  graphics_context_set_text_color(ctx, color_dim());
  char caption[32];
  if (s_state == STATE_DROPPING) {
    snprintf(caption, sizeof(caption), "removing part");
  } else {
    int pct = (progress_pm + 5) / 10;
    snprintf(caption, sizeof(caption), "layer %d/%d  %d%%", rows, DIGIT_ROWS, pct);
  }
  GRect cap = GRect(0, bed_y + bed_h + 4, bounds.size.w, 18);
  graphics_draw_text(ctx, caption, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     cap, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void schedule_frame(void);

static void on_frame(void *context) {
  s_timer = NULL;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int hour = t->tm_hour;
  int minute = t->tm_min;
  int second = t->tm_sec;

  if (s_state == STATE_DROPPING) {
    s_drop_ms += FRAME_MS;
    if (s_drop_ms >= DROP_MS) {
      s_state = STATE_PRINTING;
      s_hour = hour;
      s_minute = minute;
      s_second = second;
      s_phase = 0;
    }
  } else if (minute != s_minute || hour != s_hour) {
    s_state = STATE_DROPPING;
    s_drop_ms = 0;
    s_drop_hour = s_hour;
    s_drop_minute = s_minute;
    s_hour = hour;
    s_minute = minute;
    s_second = second;
    s_phase = 0;
  } else {
    if (second != s_second) {
      s_second = second;
      s_phase = 0;
    } else {
      s_phase++;
      if (s_phase > 9) {
        s_phase = 9;
      }
    }
  }

  layer_mark_dirty(s_layer);
  schedule_frame();
}

static void schedule_frame(void) {
  if (!s_timer) {
    s_timer = app_timer_register(FRAME_MS, on_frame, NULL);
  }
}

void face_tick(void) {
  // Minute tick from the system: keep wall-clock fields fresh.
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (s_state == STATE_PRINTING) {
    if (t->tm_min != s_minute || t->tm_hour != s_hour) {
      s_state = STATE_DROPPING;
      s_drop_ms = 0;
      s_drop_hour = s_hour;
      s_drop_minute = s_minute;
    }
    s_hour = t->tm_hour;
    s_minute = t->tm_min;
    s_second = t->tm_sec;
    s_phase = 0;
  }
  layer_mark_dirty(s_layer);
}

void face_init(Window *window) {
  s_window = window;
  window_set_background_color(window, color_bg());

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  s_state = STATE_PRINTING;
  s_hour = t->tm_hour;
  s_minute = t->tm_min;
  s_second = t->tm_sec;
  s_phase = 0;
  s_drop_ms = 0;

  s_layer = layer_create(layer_get_bounds(window_get_root_layer(window)));
  layer_set_update_proc(s_layer, draw_frame);
  layer_add_child(window_get_root_layer(window), s_layer);

  schedule_frame();
}

void face_deinit(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  if (s_layer) {
    layer_destroy(s_layer);
    s_layer = NULL;
  }
}

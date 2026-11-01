#include "face.h"
#include "digits.h"

#define FRAME_MS 100
#define DROP_MS 800
#define MARGIN_X 3
#define NOZZLE_ROOM 28
#define BED_H 14
#define CAPTION_H 20
#define BOTTOM_PAD 2

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
static int s_colon;

static GColor color_bg(void) {
#if defined(PBL_COLOR)
  return GColorOxfordBlue;
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

static GColor color_filament_hot(void) {
#if defined(PBL_COLOR)
  return GColorWhite;
#else
  return GColorWhite;
#endif
}

static GColor color_heat(void) {
#if defined(PBL_COLOR)
  return GColorRed;
#else
  return GColorWhite;
#endif
}

static GColor color_brass(void) {
#if defined(PBL_COLOR)
  return GColorChromeYellow;
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

static GColor color_bed_surface(void) {
#if defined(PBL_COLOR)
  return GColorLightGray;
#else
  return GColorWhite;
#endif
}

static GColor color_rail(void) {
#if defined(PBL_COLOR)
  return GColorPictonBlue;
#else
  return GColorWhite;
#endif
}

static GColor color_crossbar(void) {
#if defined(PBL_COLOR)
  return GColorBlueMoon;
#else
  return GColorWhite;
#endif
}

static GColor color_caption(void) {
#if defined(PBL_COLOR)
  return GColorMintGreen;
#else
  return GColorWhite;
#endif
}

// Width of HH:MM block for a given cell/gap/colon size.
static int block_width_for(int cell, int gap, int colon) {
  return 4 * DIGIT_COLS * cell + 3 * gap + colon;
}

static void layout_metrics(GRect bounds) {
  int usable_w = bounds.size.w - MARGIN_X * 2;
  int usable_h = bounds.size.h - NOZZLE_ROOM - BED_H - CAPTION_H - BOTTOM_PAD;

  // Pick the largest cell that fits both axes.
  int cell = 2;
  for (int c = 12; c >= 2; c--) {
    int gap = (c + 1) / 2;
    if (gap < 2) {
      gap = 2;
    }
    int colon = c;
    if (block_width_for(c, gap, colon) > usable_w) {
      continue;
    }
    if (DIGIT_ROWS * c > usable_h) {
      continue;
    }
    cell = c;
    break;
  }

  s_cell = cell;
  s_gap = (cell + 1) / 2;
  if (s_gap < 2) {
    s_gap = 2;
  }
  s_colon = cell;
  s_digit_w = DIGIT_COLS * cell;
  s_digit_h = DIGIT_ROWS * cell;
}

static int time_block_width(void) {
  return block_width_for(s_cell, s_gap, s_colon);
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
      x += s_colon + s_gap;
    }
    draw_glyph_rows(ctx, GPoint(x, origin.y), d[i], rows_from_bottom, y_offset, color);
    x += s_digit_w + s_gap;
  }
}

static void draw_bed(GContext *ctx, GRect bed) {
  // Heater plate body
  graphics_context_set_fill_color(ctx, color_bed());
  graphics_fill_rect(ctx, bed, 2, GCornersAll);

  // Print surface strip
  graphics_context_set_fill_color(ctx, color_bed_surface());
  graphics_fill_rect(ctx,
      GRect(bed.origin.x + 2, bed.origin.y + 2, bed.size.w - 4, bed.size.h / 2), 0, GCornerNone);

  graphics_context_set_stroke_color(ctx, color_rail());
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, bed);

  // Orange corner clips
  graphics_context_set_fill_color(ctx, color_brass());
  int c = 4;
  graphics_fill_rect(ctx, GRect(bed.origin.x - 1, bed.origin.y - 1, c, c), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bed.origin.x + bed.size.w - c + 1, bed.origin.y - 1, c, c), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bed.origin.x - 1, bed.origin.y + bed.size.h - c + 1, c, c), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(bed.origin.x + bed.size.w - c + 1, bed.origin.y + bed.size.h - c + 1, c, c), 0, GCornerNone);

  // Heater glow under the plate
  graphics_context_set_fill_color(ctx, color_heat());
  graphics_fill_rect(ctx,
      GRect(bed.origin.x + 6, bed.origin.y + bed.size.h - 2, bed.size.w - 12, 3), 0, GCornerNone);
}

static void draw_nozzle(GContext *ctx, GPoint tip) {
  // tip is the extrusion point (bottom center). Scales with digit cell size.
  int s = s_cell >= 6 ? 2 : 1;
  int tip_h = 5 * s;
  int block_w = 9 * s;
  int block_h = 5 * s;
  int fin_w = 11 * s;
  int fin_n = 3;

  // Brass tip (triangle pointing down)
  graphics_context_set_fill_color(ctx, color_brass());
  for (int i = 0; i < tip_h; i++) {
    int half = (i * (block_w / 2)) / tip_h;
    graphics_fill_rect(ctx, GRect(tip.x - half, tip.y - tip_h + i, half * 2 + 1, 1), 0, GCornerNone);
  }

  // Red heat block
  graphics_context_set_fill_color(ctx, color_heat());
  graphics_fill_rect(ctx,
      GRect(tip.x - block_w / 2, tip.y - tip_h - block_h, block_w, block_h), 0, GCornerNone);

  // Silver heat-sink fins
  graphics_context_set_fill_color(ctx, color_bed_surface());
  int fin_y0 = tip.y - tip_h - block_h - fin_n * 2 * s;
  for (int i = 0; i < fin_n; i++) {
    graphics_fill_rect(ctx,
        GRect(tip.x - fin_w / 2, fin_y0 + i * 2 * s, fin_w, s), 0, GCornerNone);
  }

  // Cyan filament path into the hotend
  graphics_context_set_fill_color(ctx, color_filament());
  graphics_fill_rect(ctx, GRect(tip.x - s / 2, fin_y0 - 5 * s, s > 0 ? s : 1, 5 * s), 0, GCornerNone);
}

static void draw_gantry(GContext *ctx, GRect bed, int top_y) {
  int rail_w = s_cell >= 6 ? 3 : 2;

  // Vertical rails
  graphics_context_set_fill_color(ctx, color_rail());
  graphics_fill_rect(ctx, GRect(bed.origin.x, top_y, rail_w, bed.origin.y - top_y), 0, GCornerNone);
  graphics_fill_rect(ctx,
      GRect(bed.origin.x + bed.size.w - rail_w, top_y, rail_w, bed.origin.y - top_y), 0, GCornerNone);

  // Top crossbar
  graphics_context_set_fill_color(ctx, color_crossbar());
  graphics_fill_rect(ctx, GRect(bed.origin.x, top_y, bed.size.w, rail_w + 1), 0, GCornerNone);

  // Endstop / LED accents
  graphics_context_set_fill_color(ctx, color_filament());
  graphics_fill_rect(ctx, GRect(bed.origin.x + rail_w + 2, top_y + 1, 3, 3), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, color_heat());
  graphics_fill_rect(ctx,
      GRect(bed.origin.x + bed.size.w - rail_w - 5, top_y + 1, 3, 3), 0, GCornerNone);
}

static void draw_frame(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  layout_metrics(bounds);

  graphics_context_set_fill_color(ctx, color_bg());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int block_w = time_block_width();
  int bed_w = bounds.size.w - MARGIN_X * 2;
  if (bed_w < block_w + 8) {
    bed_w = block_w + 8;
  }
  int bed_h = BED_H;
  int bed_x = (bounds.size.w - bed_w) / 2;

  // Pack the printer toward the vertical center, using the large digit block.
  int assembly_h = NOZZLE_ROOM + s_digit_h + 2 + bed_h + CAPTION_H;
  int assembly_top = (bounds.size.h - assembly_h) / 2;
  if (assembly_top < 2) {
    assembly_top = 2;
  }
  int bed_y = assembly_top + NOZZLE_ROOM + s_digit_h + 2;
  GRect bed = GRect(bed_x, bed_y, bed_w, bed_h);

  int digit_x = bed_x + (bed_w - block_w) / 2;
  int digit_y = bed_y - s_digit_h - 2;
  int gantry_top = assembly_top;

  draw_gantry(ctx, bed, gantry_top);
  draw_bed(ctx, bed);

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

    // Hot filament bead under the tip
    graphics_context_set_fill_color(ctx, color_filament_hot());
    int bh = s_cell > 2 ? s_cell / 2 : 1;
    int bw = s_cell > 2 ? s_cell : 3;
    graphics_fill_rect(ctx, GRect(nx - bw / 2, ny, bw, bh), 0, GCornerNone);
  } else if (show_nozzle) {
    // Finished print: park the nozzle at the top-right of the build.
    draw_nozzle(ctx, GPoint(digit_x + block_w, digit_y));
  }

  // Caption under the bed
  graphics_context_set_text_color(ctx, color_caption());
  char caption[32];
  if (s_state == STATE_DROPPING) {
    snprintf(caption, sizeof(caption), "removing part");
  } else {
    int pct = (progress_pm + 5) / 10;
    snprintf(caption, sizeof(caption), "layer %d/%d  %d%%", rows, DIGIT_ROWS, pct);
  }
  GRect cap = GRect(0, bed_y + bed_h + 2, bounds.size.w, CAPTION_H);
  graphics_draw_text(ctx, caption, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
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

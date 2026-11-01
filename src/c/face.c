#include "face.h"
#include "digits.h"
#include "settings.h"

#define FRAME_MS 100
#define DROP_MS 800
#define MARGIN_X 3
#define BED_H 14
#define CAPTION_H 20
#define DATE_H 18
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

static GColor color_bg(void) { return settings_color_bg(); }
static GColor color_filament(void) { return settings_color_filament(); }
static GColor color_heat(void) { return settings_color_heat(); }
static GColor color_brass(void) { return settings_color_brass(); }
static GColor color_bed(void) { return settings_color_bed(); }
static GColor color_bed_surface(void) { return settings_color_bed_surface(); }
static GColor color_rail(void) { return settings_color_rail(); }
static GColor color_crossbar(void) { return settings_color_crossbar(); }
static GColor color_caption(void) { return settings_color_caption(); }
static GColor color_date(void) { return settings_color_date(); }

// Width of HH:MM block for a given cell/gap/colon size.
static int block_width_for(int cell, int gap, int colon) {
  return 4 * DIGIT_COLS * cell + 3 * gap + colon;
}

static int nozzle_scale(void) {
  return s_cell >= 6 ? 2 : 1;
}

static int nozzle_stack_h(void) {
  int s = nozzle_scale();
  // tip + heat block + fins + filament lead-in
  return 5 * s + 5 * s + 6 * s + 5 * s;
}

static int footer_h(void) {
  const FaceSettings *s = settings_get();
  return s->show_progress ? CAPTION_H : 4;
}

static int header_h(void) {
  const FaceSettings *s = settings_get();
  return s->show_date ? DATE_H : 0;
}

static void layout_metrics(GRect bounds) {
  int usable_w = bounds.size.w - MARGIN_X * 2;
  int chrome = header_h() + footer_h() + BOTTOM_PAD;

  // Pick the largest cell that fits both axes.
  int cell = 2;
  for (int c = 12; c >= 2; c--) {
    int gap = (c + 1) / 2;
    if (gap < 2) {
      gap = 2;
    }
    int colon = c;
    int nroom = (c >= 6) ? 44 : 28;
    int hfit = bounds.size.h - nroom - BED_H - chrome;
    if (block_width_for(c, gap, colon) > usable_w) {
      continue;
    }
    if (DIGIT_ROWS * c > hfit) {
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
  if (settings_use_12h()) {
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

static void format_date(char *buf, size_t n, struct tm *t) {
  static const char *DOW[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static const char *MON[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  switch (settings_get()->date_format) {
    case DATE_FMT_MD:
      snprintf(buf, n, "%d/%d", t->tm_mon + 1, t->tm_mday);
      break;
    case DATE_FMT_ISO:
      snprintf(buf, n, "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
      break;
    case DATE_FMT_DOW_DM:
    default:
      snprintf(buf, n, "%s %d %s", DOW[t->tm_wday], t->tm_mday, MON[t->tm_mon]);
      break;
  }
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

// Clip a cell rect to the already-extruded side of the nozzle on the active layer.
static bool clip_to_frontier(GRect *cell, int frontier_x, bool ltr, bool partial_row) {
  if (!partial_row) {
    return true;
  }
  if (ltr) {
    // Printed region is everything left of the nozzle.
    if (cell->origin.x >= frontier_x) {
      return false;
    }
    int right = cell->origin.x + cell->size.w;
    if (right > frontier_x) {
      cell->size.w = frontier_x - cell->origin.x;
    }
  } else {
    // Printed region is everything right of the nozzle.
    int right = cell->origin.x + cell->size.w;
    if (right <= frontier_x) {
      return false;
    }
    if (cell->origin.x < frontier_x) {
      cell->size.w = right - frontier_x;
      cell->origin.x = frontier_x;
    }
  }
  return cell->size.w > 0;
}

static void draw_glyph_rows(GContext *ctx, GPoint origin, int digit, int complete_rows,
                            int active_gr, int frontier_x, bool ltr, int y_offset, GColor color) {
  if (digit < 0 || digit > 9) {
    return;
  }
  graphics_context_set_fill_color(ctx, color);

  for (int gr = 0; gr < DIGIT_ROWS; gr++) {
    int from_bottom = DIGIT_ROWS - 1 - gr;
    bool done = from_bottom < complete_rows;
    bool active = (gr == active_gr) && complete_rows < DIGIT_ROWS;
    if (!done && !active) {
      continue;
    }
    uint8_t bits = DIGIT_GLYPHS[digit][gr];
    for (int gc = 0; gc < DIGIT_COLS; gc++) {
      if (!(bits & (1 << (DIGIT_COLS - 1 - gc)))) {
        continue;
      }
      GRect cell = GRect(origin.x + gc * s_cell, origin.y + gr * s_cell + y_offset, s_cell, s_cell);
      if (!clip_to_frontier(&cell, frontier_x, ltr, active)) {
        continue;
      }
      graphics_fill_rect(ctx, cell, 0, GCornerNone);
    }
  }
}

static void draw_colon(GContext *ctx, GPoint origin, int complete_rows, int active_gr,
                       int frontier_x, bool ltr, int y_offset, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  const int dot_gr[2] = { 2, 4 };
  for (int i = 0; i < 2; i++) {
    int gr = dot_gr[i];
    int from_bottom = DIGIT_ROWS - 1 - gr;
    bool done = from_bottom < complete_rows;
    bool active = (gr == active_gr) && complete_rows < DIGIT_ROWS;
    if (!done && !active) {
      continue;
    }
    GRect cell = GRect(origin.x, origin.y + gr * s_cell + y_offset, s_cell, s_cell);
    if (!clip_to_frontier(&cell, frontier_x, ltr, active)) {
      continue;
    }
    graphics_fill_rect(ctx, cell, 0, GCornerNone);
  }
}

static void draw_time_block(GContext *ctx, GPoint origin, int hour, int minute,
                            int complete_rows, int active_gr, int frontier_x, bool ltr,
                            int y_offset, GColor color) {
  int d[4];
  digits_from_time(hour, minute, d);
  int x = origin.x;
  for (int i = 0; i < 4; i++) {
    if (i == 2) {
      draw_colon(ctx, GPoint(x, origin.y), complete_rows, active_gr, frontier_x, ltr, y_offset, color);
      x += s_colon + s_gap;
    }
    draw_glyph_rows(ctx, GPoint(x, origin.y), d[i], complete_rows, active_gr,
                    frontier_x, ltr, y_offset, color);
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

static int rail_thickness(void) {
  return s_cell >= 6 ? 3 : 2;
}

static void draw_nozzle(GContext *ctx, GPoint tip) {
  // tip is the extrusion point (bottom center). Cone tapers DOWN to the tip.
  int s = nozzle_scale();
  int tip_h = 5 * s;
  int block_w = 9 * s;
  int block_h = 5 * s;
  int fin_w = 11 * s;
  int fin_n = 3;

  // Brass tip: wide under the heat block, point at tip.y
  graphics_context_set_fill_color(ctx, color_brass());
  for (int i = 0; i < tip_h; i++) {
    int half = ((tip_h - 1 - i) * (block_w / 2)) / (tip_h > 1 ? tip_h - 1 : 1);
    graphics_fill_rect(ctx, GRect(tip.x - half, tip.y - tip_h + i, half * 2 + 1, 1), 0, GCornerNone);
  }

  // Red heat block above the cone
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

  // Short filament stub into the carriage (gantry draws the rest)
  graphics_context_set_fill_color(ctx, color_filament());
  int lead = s > 0 ? s : 1;
  graphics_fill_rect(ctx, GRect(tip.x - lead / 2, fin_y0 - 2 * s, lead, 2 * s + 1), 0, GCornerNone);
}

static void draw_frame_rails(GContext *ctx, GRect bed, int top_y) {
  int rail_w = rail_thickness();

  // Vertical Z rails
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

// Moving X gantry beam + toolhead carriage at the nozzle's Z height.
static void draw_z_gantry(GContext *ctx, GRect bed, GPoint tip) {
  int rail_w = rail_thickness();
  int s = nozzle_scale();
  int beam_h = s_cell >= 6 ? 4 : 3;
  int beam_y = tip.y - nozzle_stack_h();
  if (beam_y < 0) {
    beam_y = 0;
  }

  int inner_x = bed.origin.x + rail_w;
  int inner_w = bed.size.w - 2 * rail_w;

  // Horizontal X beam spanning the Z rails
  graphics_context_set_fill_color(ctx, color_crossbar());
  graphics_fill_rect(ctx, GRect(inner_x, beam_y, inner_w, beam_h), 0, GCornerNone);
#if defined(PBL_COLOR)
  graphics_context_set_fill_color(ctx, color_rail());
  graphics_fill_rect(ctx, GRect(inner_x, beam_y, inner_w, 1), 0, GCornerNone);
#endif

  // Bearing blocks sliding on the Z rails
  int zblock_h = beam_h + 6;
  int zblock_y = beam_y - 2;
  graphics_context_set_fill_color(ctx, color_bed_surface());
  graphics_fill_rect(ctx, GRect(bed.origin.x - 1, zblock_y, rail_w + 4, zblock_h), 0, GCornerNone);
  graphics_fill_rect(ctx,
      GRect(bed.origin.x + bed.size.w - rail_w - 3, zblock_y, rail_w + 4, zblock_h), 0, GCornerNone);
#if defined(PBL_COLOR)
  graphics_context_set_fill_color(ctx, color_rail());
  graphics_fill_rect(ctx, GRect(bed.origin.x, zblock_y + 2, rail_w, zblock_h - 4), 0, GCornerNone);
  graphics_fill_rect(ctx,
      GRect(bed.origin.x + bed.size.w - rail_w, zblock_y + 2, rail_w, zblock_h - 4), 0, GCornerNone);
#endif

  // Toolhead carriage on the X beam
  int cw = 13 * s;
  int carriage_x = tip.x - cw / 2;
  graphics_context_set_fill_color(ctx, color_rail());
  graphics_fill_rect(ctx, GRect(carriage_x, beam_y - 2, cw, beam_h + 4), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, color_bed_surface());
  graphics_fill_rect(ctx, GRect(carriage_x + 2, beam_y, cw - 4, beam_h), 0, GCornerNone);

  // Filament Bowden stub above the carriage
  graphics_context_set_fill_color(ctx, color_filament());
  int lead = s > 0 ? s : 1;
  graphics_fill_rect(ctx, GRect(tip.x - lead / 2, beam_y - 5 * s, lead, 5 * s), 0, GCornerNone);
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

  const FaceSettings *cfg = settings_get();
  int head = header_h();
  int foot = footer_h();
  int nozzle_room = nozzle_stack_h() + 6;
  int assembly_h = head + nozzle_room + s_digit_h + 2 + bed_h + foot;
  int assembly_top = (bounds.size.h - assembly_h) / 2;
  if (assembly_top < 2) {
    assembly_top = 2;
  }
  int gantry_top = assembly_top + head;
  int bed_y = gantry_top + nozzle_room + s_digit_h + 2;
  GRect bed = GRect(bed_x, bed_y, bed_w, bed_h);

  int digit_x = bed_x + (bed_w - block_w) / 2;
  int digit_y = bed_y - s_digit_h - 2;

  if (cfg->show_date) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char datebuf[32];
    format_date(datebuf, sizeof(datebuf), t);
    graphics_context_set_text_color(ctx, color_date());
    graphics_draw_text(ctx, datebuf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(0, assembly_top - 2, bounds.size.w, DATE_H),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  draw_frame_rails(ctx, bed, gantry_top);
  draw_bed(ctx, bed);

  int progress_pm;
  int complete_rows;
  int y_off = 0;
  int hour = s_hour;
  int minute = s_minute;
  bool show_nozzle = true;
  bool ltr = true;
  int frontier_x = digit_x + block_w;
  int active_gr = -1;
  int nx = digit_x + block_w;
  int ny = digit_y;

  if (s_state == STATE_DROPPING && cfg->drop_anim) {
    progress_pm = 1000;
    complete_rows = DIGIT_ROWS;
    hour = s_drop_hour;
    minute = s_drop_minute;
    int t = s_drop_ms;
    if (t > DROP_MS) {
      t = DROP_MS;
    }
    y_off = (t * t / DROP_MS) * (bounds.size.h - digit_y) / DROP_MS;
    show_nozzle = false;
  } else {
    if (s_state == STATE_DROPPING) {
      // Drop disabled: snap straight to the new minute.
      s_state = STATE_PRINTING;
    }
    progress_pm = print_progress_pm();
    complete_rows = progress_pm * DIGIT_ROWS / 1000;
    if (complete_rows > DIGIT_ROWS) {
      complete_rows = DIGIT_ROWS;
    }

    if (complete_rows < DIGIT_ROWS) {
      int row_pm = progress_pm * DIGIT_ROWS - complete_rows * 1000;
      if (row_pm < 0) {
        row_pm = 0;
      }
      if (row_pm > 1000) {
        row_pm = 1000;
      }
      ltr = (complete_rows % 2) == 0;
      nx = ltr
        ? digit_x + row_pm * block_w / 1000
        : digit_x + block_w - row_pm * block_w / 1000;
      frontier_x = nx;
      // Active glyph row counted from the top.
      active_gr = DIGIT_ROWS - 1 - complete_rows;
      ny = digit_y + active_gr * s_cell;
    }
  }

  draw_time_block(ctx, GPoint(digit_x, digit_y), hour, minute,
                  complete_rows, active_gr, frontier_x, ltr, y_off, color_filament());

  if (show_nozzle) {
    GPoint tip = (complete_rows < DIGIT_ROWS)
      ? GPoint(nx, ny)
      : GPoint(digit_x + block_w, digit_y);
    draw_z_gantry(ctx, bed, tip);
    draw_nozzle(ctx, tip);
  }

  if (cfg->show_progress) {
    graphics_context_set_text_color(ctx, color_caption());
    char caption[32];
    if (s_state == STATE_DROPPING && cfg->drop_anim) {
      snprintf(caption, sizeof(caption), "removing part");
    } else {
      int pct = (progress_pm + 5) / 10;
      int layer_shown = complete_rows < DIGIT_ROWS ? complete_rows + 1 : DIGIT_ROWS;
      snprintf(caption, sizeof(caption), "layer %d/%d  %d%%", layer_shown, DIGIT_ROWS, pct);
    }
    GRect cap = GRect(0, bed_y + bed_h + 2, bounds.size.w, CAPTION_H);
    graphics_draw_text(ctx, caption, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       cap, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
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
    if (settings_get()->drop_anim) {
      s_state = STATE_DROPPING;
      s_drop_ms = 0;
      s_drop_hour = s_hour;
      s_drop_minute = s_minute;
    } else {
      s_state = STATE_PRINTING;
    }
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
      if (settings_get()->drop_anim) {
        s_state = STATE_DROPPING;
        s_drop_ms = 0;
        s_drop_hour = s_hour;
        s_drop_minute = s_minute;
      }
    }
    s_hour = t->tm_hour;
    s_minute = t->tm_min;
    s_second = t->tm_sec;
    s_phase = 0;
  }
  layer_mark_dirty(s_layer);
}

void face_settings_changed(void) {
  if (s_window) {
    window_set_background_color(s_window, color_bg());
  }
  if (s_layer) {
    layer_mark_dirty(s_layer);
  }
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

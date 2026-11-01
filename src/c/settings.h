#pragma once
#include <pebble.h>

enum {
  TIME_FMT_SYSTEM = 0,
  TIME_FMT_12 = 1,
  TIME_FMT_24 = 2,
};

enum {
  DATE_FMT_DOW_DM = 0,  // Sun 1 Nov
  DATE_FMT_MD = 1,      // 11/1
  DATE_FMT_ISO = 2,     // 2026-11-01
};

enum {
  THEME_CLASSIC = 0,
  THEME_FOREST = 1,
  THEME_EMBER = 2,
  THEME_VIOLET = 3,
  THEME_MONO = 4,
  THEME_COUNT
};

typedef struct {
  uint8_t time_format;
  bool show_date;
  uint8_t date_format;
  bool show_progress;
  uint8_t theme;
  bool drop_anim;
} FaceSettings;

const FaceSettings *settings_get(void);
void settings_load(void);
void settings_save(void);
void settings_apply_tuple(Tuple *t);
bool settings_use_12h(void);
GColor settings_color_bg(void);
GColor settings_color_filament(void);
GColor settings_color_heat(void);
GColor settings_color_brass(void);
GColor settings_color_bed(void);
GColor settings_color_bed_surface(void);
GColor settings_color_rail(void);
GColor settings_color_crossbar(void);
GColor settings_color_caption(void);
GColor settings_color_date(void);

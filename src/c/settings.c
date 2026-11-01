#include "settings.h"

// Declared in build/src/message_keys.auto.c (header generation can lag a clean build).
extern uint32_t MESSAGE_KEY_TimeFormat;
extern uint32_t MESSAGE_KEY_ShowDate;
extern uint32_t MESSAGE_KEY_DateFormat;
extern uint32_t MESSAGE_KEY_ShowProgress;
extern uint32_t MESSAGE_KEY_Theme;
extern uint32_t MESSAGE_KEY_DropAnim;

#define PERSIST_VERSION 1
#define KEY_VERSION 1
#define KEY_TIME_FMT 2
#define KEY_SHOW_DATE 3
#define KEY_DATE_FMT 4
#define KEY_SHOW_PROGRESS 5
#define KEY_THEME 6
#define KEY_DROP_ANIM 7

static FaceSettings s_settings;

static void defaults(FaceSettings *s) {
  s->time_format = TIME_FMT_SYSTEM;
  s->show_date = true;
  s->date_format = DATE_FMT_DOW_DM;
  s->show_progress = true;
  s->theme = THEME_CLASSIC;
  s->drop_anim = true;
}

const FaceSettings *settings_get(void) {
  return &s_settings;
}

void settings_load(void) {
  defaults(&s_settings);
  if (!persist_exists(KEY_VERSION) || persist_read_int(KEY_VERSION) != PERSIST_VERSION) {
    settings_save();
    return;
  }
  s_settings.time_format = (uint8_t)persist_read_int(KEY_TIME_FMT);
  s_settings.show_date = persist_read_bool(KEY_SHOW_DATE);
  s_settings.date_format = (uint8_t)persist_read_int(KEY_DATE_FMT);
  s_settings.show_progress = persist_read_bool(KEY_SHOW_PROGRESS);
  s_settings.theme = (uint8_t)persist_read_int(KEY_THEME);
  s_settings.drop_anim = persist_read_bool(KEY_DROP_ANIM);

  if (s_settings.time_format > TIME_FMT_24) {
    s_settings.time_format = TIME_FMT_SYSTEM;
  }
  if (s_settings.date_format > DATE_FMT_ISO) {
    s_settings.date_format = DATE_FMT_DOW_DM;
  }
  if (s_settings.theme >= THEME_COUNT) {
    s_settings.theme = THEME_CLASSIC;
  }
}

void settings_save(void) {
  persist_write_int(KEY_VERSION, PERSIST_VERSION);
  persist_write_int(KEY_TIME_FMT, s_settings.time_format);
  persist_write_bool(KEY_SHOW_DATE, s_settings.show_date);
  persist_write_int(KEY_DATE_FMT, s_settings.date_format);
  persist_write_bool(KEY_SHOW_PROGRESS, s_settings.show_progress);
  persist_write_int(KEY_THEME, s_settings.theme);
  persist_write_bool(KEY_DROP_ANIM, s_settings.drop_anim);
}

static int32_t tuple_int(const Tuple *t) {
  if (t->type == TUPLE_INT) {
    switch (t->length) {
      case 1: return t->value->int8;
      case 2: return t->value->int16;
      default: return t->value->int32;
    }
  }
  if (t->type == TUPLE_UINT) {
    switch (t->length) {
      case 1: return t->value->uint8;
      case 2: return t->value->uint16;
      default: return (int32_t)t->value->uint32;
    }
  }
  return 0;
}

void settings_apply_tuple(Tuple *t) {
  if (!t) {
    return;
  }
  int32_t v = tuple_int(t);
  if (t->key == MESSAGE_KEY_TimeFormat) {
    s_settings.time_format = (uint8_t)v;
  } else if (t->key == MESSAGE_KEY_ShowDate) {
    s_settings.show_date = v != 0;
  } else if (t->key == MESSAGE_KEY_DateFormat) {
    s_settings.date_format = (uint8_t)v;
  } else if (t->key == MESSAGE_KEY_ShowProgress) {
    s_settings.show_progress = v != 0;
  } else if (t->key == MESSAGE_KEY_Theme) {
    s_settings.theme = (uint8_t)v;
  } else if (t->key == MESSAGE_KEY_DropAnim) {
    s_settings.drop_anim = v != 0;
  }
}

bool settings_use_12h(void) {
  if (s_settings.time_format == TIME_FMT_12) {
    return true;
  }
  if (s_settings.time_format == TIME_FMT_24) {
    return false;
  }
  return !clock_is_24h_style();
}

#if defined(PBL_COLOR)

typedef struct {
  GColor bg;
  GColor filament;
  GColor heat;
  GColor brass;
  GColor bed;
  GColor bed_surface;
  GColor rail;
  GColor crossbar;
  GColor caption;
  GColor date;
} ThemeColors;

static const ThemeColors THEMES[THEME_COUNT] = {
  // Classic — navy workshop, cyan PLA
  { GColorOxfordBlue, GColorCyan, GColorRed, GColorChromeYellow,
    GColorDarkGray, GColorLightGray, GColorPictonBlue, GColorBlueMoon,
    GColorMintGreen, GColorWhite },
  // Forest — dark green, lime filament
  { GColorDarkGreen, GColorBrightGreen, GColorDarkCandyAppleRed, GColorChromeYellow,
    GColorBlack, GColorLightGray, GColorIslamicGreen, GColorJaegerGreen,
    GColorMintGreen, GColorWhite },
  // Ember — black bed, orange ABS
  { GColorBlack, GColorOrange, GColorRed, GColorYellow,
    GColorDarkGray, GColorLightGray, GColorDarkGray, GColorLightGray,
    GColorChromeYellow, GColorWhite },
  // Violet — purple shop, magenta filament
  { GColorImperialPurple, GColorMagenta, GColorRed, GColorChromeYellow,
    GColorDarkGray, GColorLightGray, GColorPurple, GColorJazzberryJam,
    GColorMelon, GColorWhite },
  // Mono — high contrast
  { GColorBlack, GColorWhite, GColorLightGray, GColorWhite,
    GColorDarkGray, GColorLightGray, GColorLightGray, GColorWhite,
    GColorLightGray, GColorWhite },
};

static const ThemeColors *theme(void) {
  uint8_t id = s_settings.theme;
  if (id >= THEME_COUNT) {
    id = THEME_CLASSIC;
  }
  return &THEMES[id];
}

GColor settings_color_bg(void) { return theme()->bg; }
GColor settings_color_filament(void) { return theme()->filament; }
GColor settings_color_heat(void) { return theme()->heat; }
GColor settings_color_brass(void) { return theme()->brass; }
GColor settings_color_bed(void) { return theme()->bed; }
GColor settings_color_bed_surface(void) { return theme()->bed_surface; }
GColor settings_color_rail(void) { return theme()->rail; }
GColor settings_color_crossbar(void) { return theme()->crossbar; }
GColor settings_color_caption(void) { return theme()->caption; }
GColor settings_color_date(void) { return theme()->date; }

#else

GColor settings_color_bg(void) { return GColorBlack; }
GColor settings_color_filament(void) { return GColorWhite; }
GColor settings_color_heat(void) { return GColorWhite; }
GColor settings_color_brass(void) { return GColorWhite; }
GColor settings_color_bed(void) { return GColorBlack; }
GColor settings_color_bed_surface(void) { return GColorWhite; }
GColor settings_color_rail(void) { return GColorWhite; }
GColor settings_color_crossbar(void) { return GColorWhite; }
GColor settings_color_caption(void) { return GColorWhite; }
GColor settings_color_date(void) { return GColorWhite; }

#endif

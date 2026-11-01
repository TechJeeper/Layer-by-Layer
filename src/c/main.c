#include <pebble.h>
#include "face.h"

static Window *s_window;

static void on_tick(struct tm *tick_time, TimeUnits units_changed) {
  face_tick();
}

static void window_load(Window *window) {
  face_init(window);
}

static void window_unload(Window *window) {
  face_deinit();
}

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, on_tick);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

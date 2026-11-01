#include "comm.h"
#include "settings.h"
#include "face.h"

static void inbox(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_read_first(iter);
  bool changed = false;
  while (t) {
    settings_apply_tuple(t);
    changed = true;
    t = dict_read_next(iter);
  }
  if (changed) {
    settings_save();
    face_settings_changed();
  }
}

void comm_init(void) {
  app_message_register_inbox_received(inbox);
  app_message_open(128, 64);
}

void comm_deinit(void) {
  app_message_deregister_callbacks();
}

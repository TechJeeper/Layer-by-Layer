// Layer-by-Layer phone side: Clay settings page (offline, bundled in the .pbw).

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

function toWatchDict(settings) {
  // Clay select values are strings; coerce to ints the watch expects.
  return {
    TimeFormat: parseInt(settings.TimeFormat, 10) || 0,
    ShowDate: settings.ShowDate ? 1 : 0,
    DateFormat: parseInt(settings.DateFormat, 10) || 0,
    ShowProgress: settings.ShowProgress ? 1 : 0,
    Theme: parseInt(settings.Theme, 10) || 0,
    DropAnim: settings.DropAnim ? 1 : 0
  };
}

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) {
    return;
  }
  // convert=false keeps messageKey names as strings for our mapping.
  var settings = clay.getSettings(e.response, false);
  Pebble.sendAppMessage(toWatchDict(settings));
});

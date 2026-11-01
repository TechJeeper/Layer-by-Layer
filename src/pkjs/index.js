// Layer-by-Layer phone side: opens the settings page and pushes options to the watch.

var keys = require('message_keys');
var configPage = require('./config_page');

var STORE_KEY = 'lbl.settings';

var defaults = {
  timeFormat: 0,
  showDate: true,
  dateFormat: 0,
  showProgress: true,
  theme: 0,
  dropAnim: true
};

function load() {
  try {
    var raw = localStorage.getItem(STORE_KEY);
    if (raw) {
      return Object.assign({}, defaults, JSON.parse(raw));
    }
  } catch (e) {}
  return Object.assign({}, defaults);
}

function save(cfg) {
  try {
    localStorage.setItem(STORE_KEY, JSON.stringify(cfg));
  } catch (e) {}
}

function sendSettings(cfg) {
  Pebble.sendAppMessage({
    TimeFormat: cfg.timeFormat | 0,
    ShowDate: cfg.showDate ? 1 : 0,
    DateFormat: cfg.dateFormat | 0,
    ShowProgress: cfg.showProgress ? 1 : 0,
    Theme: cfg.theme | 0,
    DropAnim: cfg.dropAnim ? 1 : 0
  });
}

function configUrl(cfg) {
  var json = JSON.stringify(cfg).replace(/</g, '\\u003c');
  var returnTo = Pebble.platform === 'pypkjs' ? '$$$RETURN_TO$$$' : 'pebblejs://close#';
  var html = configPage
    .replace('"__LAYER_STATE__"', function () { return json; })
    .replace('__LAYER_STATE__', function () { return json; })
    .replace('__RETURN_TO__', function () { return returnTo; });
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

function parseResponse(response) {
  try {
    return JSON.parse(response);
  } catch (e) {
    try {
      return JSON.parse(decodeURIComponent(response));
    } catch (e2) {
      return null;
    }
  }
}

Pebble.addEventListener('ready', function () {
  sendSettings(load());
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(configUrl(load()));
});

Pebble.addEventListener('webviewclosed', function (e) {
  var page = e && e.response ? parseResponse(e.response) : null;
  if (!page) {
    return;
  }
  var cfg = Object.assign({}, defaults, page);
  save(cfg);
  sendSettings(cfg);
});

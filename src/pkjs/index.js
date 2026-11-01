// Layer-by-Layer phone side: opens the settings page and pushes options to the watch.
//
// CloudPebble's emulator opens a blank about:blank for data: config URLs, so we
// serve the page over HTTPS from the GitHub repo (jsDelivr CDN).

var CONFIG_URL = 'https://cdn.jsdelivr.net/gh/TechJeeper/Layer-by-Layer@main/config/index.html';

var STORE_KEY = 'lbl.settings';

var defaults = {
  timeFormat: 0,
  showDate: true,
  dateFormat: 0,
  showProgress: true,
  theme: 0,
  dropAnim: true
};

function merge(a, b) {
  var o = {}, k;
  for (k in a) o[k] = a[k];
  for (k in b || {}) o[k] = b[k];
  return o;
}

function load() {
  try {
    var raw = localStorage.getItem(STORE_KEY);
    if (raw) {
      return merge(defaults, JSON.parse(raw));
    }
  } catch (e) {}
  return merge(defaults, {});
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
  // Cache-bust so CloudPebble picks up HTML changes after a push.
  return CONFIG_URL +
    '?v=1.1.1&cfg=' + encodeURIComponent(JSON.stringify(cfg));
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
  var cfg = merge(defaults, page);
  save(cfg);
  sendSettings(cfg);
});

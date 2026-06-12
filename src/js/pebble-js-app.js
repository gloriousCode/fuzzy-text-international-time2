var VERSION = "1.4.0";
var CONFIG_URL = "https://www.gloriousedge.com/fuzzy-text-two/resources/configure-fuzzy-text-two.html";

var isReady = false;
var callbacks = [];

var alignments = {
  center: 0,
  left:   1,
  right:  2
};

var langs = {
  ca:    0,
  de:    1,
  en_GB: 2,
  en_US: 3,
  es:    4,
  fr:    5,
  no:    6,
  sv:    7
};

var fonts = {
  classic: 0,
  sharp:   1,
  compact: 2,
  tall:    3
};

var colourValues = {
  black:     0x000000,
  white:     0xFFFFFF,
  red:       0xFF0000,
  orange:    0xFF5500,
  yellow:    0xFFFF00,
  green:     0x00AA00,
  mint:      0x55FFAA,
  cyan:      0x00AAFF,
  blue:      0x0055FF,
  purple:    0xAA00FF,
  magenta:   0xFF00AA
};

function readyCallback(event) {
  isReady = true;
  var callback;
  while (callbacks.length > 0) {
    callback = callbacks.shift();
    callback(event);
  }
}

function showConfiguration(event) {
  onReady(function() {
    var opts = getOptions();
    Pebble.openURL(CONFIG_URL + "#v=" + encodeURIComponent(VERSION) + "&options=" + encodeURIComponent(opts));
  });
}

function webviewclosed(event) {
  var resp = event.response;
  console.log('configuration response: '+ resp + ' ('+ typeof resp +')');

  var options = parseConfigurationResponse(resp);
  if (typeof options.invert === 'undefined' &&
      typeof options.text_align === 'undefined' &&
      typeof options.lang === 'undefined' &&
      typeof options.font === 'undefined' &&
      typeof options.foreground_colour === 'undefined' &&
      typeof options.background_colour === 'undefined') {
    return;
  }

  onReady(function() {
    var serializedOptions = JSON.stringify(options);
    setOptions(serializedOptions);

    var message = prepareConfiguration(serializedOptions);
    transmitConfiguration(message);
  });
}

function parseConfigurationResponse(resp) {
  try {
    return JSON.parse(resp);
  }
  catch (err) {
    return JSON.parse(decodeURIComponent(resp));
  }
}

// Retrieves stored configuration from localStorage.
function getOptions() {
  return localStorage.getItem("options") || ("{}");
}

// Stores options in localStorage.
function setOptions(options) {
  localStorage.setItem("options", options);
}

// Takes a string containing serialized JSON as input.  This is the
// format that is sent back from the configuration web UI.  Produces
// a JSON message to send to the watch face.
function prepareConfiguration(serialized_settings) {
  var settings = JSON.parse(serialized_settings);
  var foreground = settings.foreground_colour || (settings.invert ? "black" : "white");
  var background = settings.background_colour || (settings.invert ? "white" : "black");
  return {
    "0": settings.invert ? 1 : 0,
    "1": alignments[settings.text_align || "center"],
    "2": langs[settings.lang || "en_GB"],
    "3": fonts[settings.font || "classic"],
    "4": colourValues[foreground],
    "5": colourValues[background]
  };
}

// Takes a JSON message as input.  Sends the message to the watch.
function transmitConfiguration(settings) {
  console.log('sending message: '+ JSON.stringify(settings));
  Pebble.sendAppMessage(settings, function(event) {
    // Message delivered successfully
  }, logError);
}

function logError(event) {
  console.log('Unable to deliver message with transactionId='+
              event.data.transactionId +' ; Error is'+ event.error.message);
}

function onReady(callback) {
  if (isReady) {
    callback();
  }
  else {
    callbacks.push(callback);
  }
}

Pebble.addEventListener("ready", readyCallback);
Pebble.addEventListener("showConfiguration", showConfiguration);
Pebble.addEventListener("webviewclosed", webviewclosed);

onReady(function(event) {
  var message = prepareConfiguration(getOptions());
  transmitConfiguration(message);
});

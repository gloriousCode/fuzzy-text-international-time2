var VERSION = "1.4.9";
var OPTIONS_STORAGE_KEY = "fuzzy-text-two-options-v2";
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

var datePositions = {
  off:    0,
  top:    1,
  bottom: 2
};

var dateFormats = {
  dd_mm_yy:    0,
  mm_dd_yyyy: 1,
  mon_d_aug:  2,
  dd_slash_mm: 3,
  mm_slash_dd: 4
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

var optionalColourValues = {
  match:     -1,
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

function lookupValue(values, key, fallbackKey) {
  return Object.prototype.hasOwnProperty.call(values, key) ? values[key] : values[fallbackKey];
}

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
  var resp = event && event.response;
  console.log('configuration response: '+ resp + ' ('+ typeof resp +')');

  var options = parseConfigurationResponse(resp);
  if (typeof options.invert === 'undefined' &&
      typeof options.text_align === 'undefined' &&
      typeof options.lang === 'undefined' &&
      typeof options.font === 'undefined' &&
      typeof options.foreground_colour === 'undefined' &&
      typeof options.background_colour === 'undefined' &&
      typeof options.date_position === 'undefined' &&
      typeof options.date_format === 'undefined' &&
      typeof options.row_one_colour === 'undefined' &&
      typeof options.row_two_colour === 'undefined' &&
      typeof options.row_three_colour === 'undefined' &&
      typeof options.date_part_one_colour === 'undefined' &&
      typeof options.date_part_two_colour === 'undefined' &&
      typeof options.date_part_three_colour === 'undefined') {
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
  if (!resp) {
    return {};
  }

  try {
    return JSON.parse(resp);
  }
  catch (err) {
    try {
      return JSON.parse(decodeURIComponent(resp));
    }
    catch (decodeErr) {
      console.log('Unable to parse configuration response: ' + decodeErr.message);
      return {};
    }
  }
}

// Retrieves stored configuration from localStorage.
function getOptions() {
  try {
    return localStorage.getItem(OPTIONS_STORAGE_KEY) || "{}";
  }
  catch (err) {
    console.log('Unable to read options: ' + err.message);
    return "{}";
  }
}

// Stores options in localStorage.
function setOptions(options) {
  try {
    localStorage.setItem(OPTIONS_STORAGE_KEY, options);
  }
  catch (err) {
    console.log('Unable to store options: ' + err.message);
  }
}

// Takes a string containing serialized JSON as input.  This is the
// format that is sent back from the configuration web UI.  Produces
// a JSON message to send to the watch face.
function prepareConfiguration(serialized_settings) {
  var settings = {};
  try {
    settings = JSON.parse(serialized_settings || "{}");
  }
  catch (err) {
    console.log('Unable to parse stored settings: ' + err.message);
  }

  settings.lang = settings.lang || "en_GB";
  settings.text_align = settings.text_align || "center";
  settings.font = settings.font || "classic";
  settings.date_position = settings.date_position || "off";
  settings.date_format = settings.date_format || "dd_mm_yy";

  var foreground = settings.foreground_colour || (settings.invert ? "black" : "white");
  var background = settings.background_colour || (settings.invert ? "white" : "black");
  var rowOne = settings.row_one_colour || settings.minute_colour || "match";
  var rowTwo = settings.row_two_colour || settings.minute_colour || "match";
  var rowThree = settings.row_three_colour || settings.hour_colour || "match";
  var datePartOne = settings.date_part_one_colour || settings.date_colour || "match";
  var datePartTwo = settings.date_part_two_colour || settings.date_colour || "match";
  var datePartThree = settings.date_part_three_colour || settings.date_colour || "match";
  return {
    "0": settings.invert ? 1 : 0,
    "1": lookupValue(alignments, settings.text_align, "center"),
    "2": lookupValue(langs, settings.lang, "en_GB"),
    "3": lookupValue(fonts, settings.font, "classic"),
    "4": lookupValue(colourValues, foreground, "white"),
    "5": lookupValue(colourValues, background, "black"),
    "6": lookupValue(datePositions, settings.date_position, "off"),
    "7": lookupValue(dateFormats, settings.date_format, "dd_mm_yy"),
    "8": lookupValue(optionalColourValues, rowOne, "match"),
    "9": lookupValue(optionalColourValues, rowTwo, "match"),
    "10": lookupValue(optionalColourValues, rowThree, "match"),
    "11": lookupValue(optionalColourValues, datePartOne, "match"),
    "12": lookupValue(optionalColourValues, datePartTwo, "match"),
    "13": lookupValue(optionalColourValues, datePartThree, "match")
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
  var transactionId = event && event.data ? event.data.transactionId : 'unknown';
  var message = event && event.error ? event.error.message : 'unknown';
  console.log('Unable to deliver message with transactionId='+
              transactionId +' ; Error is '+ message);
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

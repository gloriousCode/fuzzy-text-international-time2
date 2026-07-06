var VERSION = "1.5.1";
var OPTIONS_STORAGE_KEY = "gabbroeye-hud-options-v1";
var CONFIG_URL = "https://www.gloriousedge.com/fuzzy-text-two/resources/configure-fuzzy-text-two.html";

var dateFormats = {
  dd_mm_yy: 0,
  mm_dd_yyyy: 1,
  mon_d_aug: 2,
  dd_slash_mm: 3,
  mm_slash_dd: 4
};

var displayModes = {
  digital: 0,
  analogue: 1,
  combined: 2
};

function readyCallback() {
  console.log("GabbroEye HUD " + VERSION + " ready.");
  transmitConfiguration(prepareConfiguration(getOptions()));
}

function showConfiguration() {
  Pebble.openURL(CONFIG_URL + "#v=" + encodeURIComponent(VERSION) +
      "&options=" + encodeURIComponent(getOptions()));
}

function webviewclosed(event) {
  var options = parseConfigurationResponse(event && event.response);
  if (typeof options.display_mode === "undefined" &&
      typeof options.date_format === "undefined" &&
      typeof options.analogue_seconds === "undefined") {
    return;
  }

  var serializedOptions = JSON.stringify(options);
  setOptions(serializedOptions);
  transmitConfiguration(prepareConfiguration(serializedOptions));
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
      console.log("Unable to parse configuration response: " + decodeErr.message);
      return {};
    }
  }
}

function getOptions() {
  try {
    return localStorage.getItem(OPTIONS_STORAGE_KEY) || "{}";
  }
  catch (err) {
    console.log("Unable to read options: " + err.message);
    return "{}";
  }
}

function setOptions(options) {
  try {
    localStorage.setItem(OPTIONS_STORAGE_KEY, options);
  }
  catch (err) {
    console.log("Unable to store options: " + err.message);
  }
}

function prepareConfiguration(serializedSettings) {
  var settings = {};
  try {
    settings = JSON.parse(serializedSettings || "{}");
  }
  catch (err) {
    console.log("Unable to parse stored settings: " + err.message);
  }

  return {
    "7": dateFormats[settings.date_format] || dateFormats.dd_mm_yy,
    "14": settings.analogue_seconds === false ? 0 : 1,
    "15": displayModes[settings.display_mode] || displayModes.digital
  };
}

function transmitConfiguration(settings) {
  Pebble.sendAppMessage(settings, function() {
    console.log("Delivered GabbroEye HUD configuration.");
  }, function(event) {
    var message = event && event.error ? event.error.message : "unknown";
    console.log("Unable to deliver GabbroEye HUD configuration: " + message);
  });
}

Pebble.addEventListener("ready", readyCallback);
Pebble.addEventListener("showConfiguration", showConfiguration);
Pebble.addEventListener("webviewclosed", webviewclosed);

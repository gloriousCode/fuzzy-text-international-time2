#pragma once

#include <pebble.h>
#include <stdbool.h>

typedef struct {
  int hour;
  int minute;
  int second;
  int day;
  int month;
  int weekday;
  int battery_percent;
  bool bluetooth_connected;
  bool backlight_on;
  bool twenty_four_hour_style;
} SpyFaceState;

Layer *spy_face_layer_create(GRect frame);
void spy_face_layer_destroy(Layer *layer);
void spy_face_layer_set_state(Layer *layer, const SpyFaceState *state);
void spy_face_layer_toggle_mode(Layer *layer);

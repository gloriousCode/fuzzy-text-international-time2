#include "spy_face_geometry.h"

static int smaller_int(int a, int b)
{
  return a < b ? a : b;
}

static int larger_int(int a, int b)
{
  return a > b ? a : b;
}

int spy_face_scaled_value(int min_side, int value_at_gabbro)
{
  int scaled = (min_side * value_at_gabbro + 130) / 260;
  return scaled > 0 ? scaled : 1;
}

int spy_face_lit_segments_for_percent(int percent, int segment_count)
{
  if (segment_count <= 0) {
    return 0;
  }
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }

  return (percent * segment_count + 99) / 100;
}

SpyFaceGeometry spy_face_geometry_for_bounds(int screen_width, int screen_height, bool round)
{
  int min_side = smaller_int(screen_width, screen_height);
  int margin = larger_int(spy_face_scaled_value(min_side, 8), 4);
  int outer_radius = (min_side / 2) - margin;
  int ring_width = larger_int(spy_face_scaled_value(min_side, 26), 10);
  int display_width = spy_face_scaled_value(min_side, 166);
  int display_height = spy_face_scaled_value(min_side, 120);
  int bar_width = larger_int(spy_face_scaled_value(min_side, 12), 5);
  int bar_height = spy_face_scaled_value(min_side, 92);
  int bar_offset = spy_face_scaled_value(min_side, 18);

  SpyFaceGeometry geometry = {
    .screen_width = screen_width,
    .screen_height = screen_height,
    .round = round,
    .centre_x = screen_width / 2,
    .centre_y = screen_height / 2,
    .min_side = min_side,
    .outer_radius = outer_radius,
    .inner_radius = outer_radius - ring_width,
    .ring_width = ring_width,
    .display = {
      .x = (screen_width - display_width) / 2,
      .y = (screen_height - display_height) / 2,
      .w = display_width,
      .h = display_height,
    },
    .left_bar = {
      .x = (screen_width / 2) - outer_radius + bar_offset,
      .y = (screen_height / 2) - spy_face_scaled_value(min_side, 62),
      .w = bar_width,
      .h = bar_height,
    },
    .right_bar = {
      .x = (screen_width / 2) + outer_radius - bar_offset - bar_width,
      .y = (screen_height / 2) - spy_face_scaled_value(min_side, 62),
      .w = bar_width,
      .h = bar_height,
    },
  };

  if (geometry.inner_radius < 1) {
    geometry.inner_radius = 1;
  }

  return geometry;
}

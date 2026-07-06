#include "spy_face.h"

#include "spy_face_geometry.h"

#include <stdio.h>
#include <string.h>

#define DEG_TO_TRIG(degrees) ((TRIG_MAX_ANGLE * (degrees)) / 360)

typedef enum {
  SPY_FACE_MODE_DIGITAL = 0,
  SPY_FACE_MODE_ANALOGUE = 1,
} SpyFaceMode;

typedef enum {
  SPY_FACE_DATE_FORMAT_DD_MM_YY = 0,
  SPY_FACE_DATE_FORMAT_MM_DD_YYYY = 1,
  SPY_FACE_DATE_FORMAT_MON_D_AUG = 2,
  SPY_FACE_DATE_FORMAT_DD_SLASH_MM = 3,
  SPY_FACE_DATE_FORMAT_MM_SLASH_DD = 4,
} SpyFaceDateFormat;

typedef struct {
  SpyFaceState state;
  SpyFaceMode mode;
  GFont time_font;
} SpyFaceLayerData;

static GColor spy_colour(int red, int green, int blue)
{
#ifdef PBL_COLOR
  return GColorFromRGB(red, green, blue);
#else
  return (red * 30 + green * 59 + blue * 11) >= 12800 ? GColorWhite : GColorBlack;
#endif
}

static GPoint spy_polar_point(GPoint centre, int radius, int degrees)
{
  int32_t angle = DEG_TO_TRIG(degrees);
  int x = centre.x + (int)((int32_t)radius * sin_lookup(angle) / TRIG_MAX_RATIO);
  int y = centre.y - (int)((int32_t)radius * cos_lookup(angle) / TRIG_MAX_RATIO);
  return GPoint(x, y);
}

static GFont spy_time_font_for_geometry(SpyFaceGeometry geometry, GFont preferred_font)
{
  if (geometry.min_side >= 220 && preferred_font != NULL) {
    return preferred_font;
  }
  if (geometry.min_side >= 220) {
    return fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  }
  return fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
}

static GFont spy_label_font_for_geometry(SpyFaceGeometry geometry)
{
  if (geometry.min_side >= 220) {
    return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  }
  return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
}

static GFont spy_small_font_for_geometry(SpyFaceGeometry geometry)
{
  if (geometry.min_side >= 220) {
    return fonts_get_system_font(FONT_KEY_GOTHIC_18);
  }
  return fonts_get_system_font(FONT_KEY_GOTHIC_14);
}

static SpyFaceMode spy_mode_for_setting(int display_mode)
{
  return display_mode == SPY_FACE_MODE_ANALOGUE
      ? SPY_FACE_MODE_ANALOGUE
      : SPY_FACE_MODE_DIGITAL;
}

static void spy_draw_text(GContext *ctx, const char *text, GFont font, GRect frame,
    GTextAlignment alignment, GColor colour)
{
  graphics_context_set_text_color(ctx, colour);
  graphics_draw_text(ctx, text, font, frame, GTextOverflowModeTrailingEllipsis,
      alignment, NULL);
}

static void spy_draw_moon_icon(GContext *ctx, GPoint centre, int radius,
    GColor moon_colour, GColor shadow_colour)
{
  graphics_context_set_fill_color(ctx, moon_colour);
  graphics_fill_circle(ctx, centre, radius);
  graphics_context_set_fill_color(ctx, shadow_colour);
  graphics_fill_circle(ctx, GPoint(centre.x + (radius / 2), centre.y - (radius / 4)),
      radius);
}

static GPoint spy_hand_point(GPoint centre, int along, int lateral, int degrees)
{
  int32_t angle = DEG_TO_TRIG(degrees);
  int x = centre.x + (int)((int32_t)along * sin_lookup(angle) / TRIG_MAX_RATIO)
      + (int)((int32_t)lateral * cos_lookup(angle) / TRIG_MAX_RATIO);
  int y = centre.y - (int)((int32_t)along * cos_lookup(angle) / TRIG_MAX_RATIO)
      + (int)((int32_t)lateral * sin_lookup(angle) / TRIG_MAX_RATIO);
  return GPoint(x, y);
}

static void spy_draw_shaped_hand(GContext *ctx, GPoint centre, int degrees,
    int length, int body_width, int tip_width, int tip_length, int tail_length,
    int tail_width, GColor fill_colour, GColor outline_colour)
{
  int neck = length - tip_length;
  if (neck < body_width * 3) {
    neck = body_width * 3;
  }

  GPoint points[] = {
    spy_hand_point(centre, length, 0, degrees),
    spy_hand_point(centre, neck, tip_width, degrees),
    spy_hand_point(centre, body_width * 3, body_width, degrees),
    spy_hand_point(centre, -tail_length, tail_width, degrees),
    spy_hand_point(centre, -tail_length, -tail_width, degrees),
    spy_hand_point(centre, body_width * 3, -body_width, degrees),
    spy_hand_point(centre, neck, -tip_width, degrees),
  };
  GPathInfo path_info = {
    .num_points = ARRAY_LENGTH(points),
    .points = points,
  };
  GPath *path = gpath_create(&path_info);
  if (path == NULL) {
    graphics_context_set_stroke_color(ctx, fill_colour);
    graphics_draw_line(ctx, centre, spy_hand_point(centre, length, 0, degrees));
    return;
  }

  graphics_context_set_fill_color(ctx, fill_colour);
  gpath_draw_filled(ctx, path);
  graphics_context_set_stroke_color(ctx, outline_colour);
  gpath_draw_outline(ctx, path);
  gpath_destroy(path);
}

static void spy_draw_analogue_centre_hub(GContext *ctx, SpyFaceGeometry geometry,
    GPoint centre)
{
  graphics_context_set_fill_color(ctx, spy_colour(205, 42, 13));
  graphics_fill_circle(ctx, centre, spy_face_scaled_value(geometry.min_side, 5));
}

static void spy_draw_radial_segment(GContext *ctx, GPoint centre, int outer_radius,
    int ring_width, int start_degrees, int end_degrees, GColor colour)
{
  GRect ring = GRect(centre.x - outer_radius, centre.y - outer_radius,
      outer_radius * 2, outer_radius * 2);

  graphics_context_set_fill_color(ctx, colour);
  graphics_fill_radial(ctx, ring, GOvalScaleModeFitCircle, ring_width,
      DEG_TO_TRIG(start_degrees), DEG_TO_TRIG(end_degrees));
}

static void spy_draw_outlined_radial_segment(GContext *ctx, SpyFaceGeometry geometry,
    int start_degrees, int end_degrees, GColor outline_colour, GColor fill_colour)
{
  GPoint centre = GPoint(geometry.centre_x, geometry.centre_y);
  int inset = spy_face_scaled_value(geometry.min_side, 2);
  int fill_width = geometry.ring_width - (inset * 2);
  if (fill_width < 1) {
    fill_width = 1;
  }

  spy_draw_radial_segment(ctx, centre, geometry.outer_radius, geometry.ring_width,
      start_degrees, end_degrees, outline_colour);
  spy_draw_radial_segment(ctx, centre, geometry.outer_radius - inset, fill_width,
      start_degrees, end_degrees, fill_colour);
}

static void spy_draw_ring(GContext *ctx, SpyFaceGeometry geometry)
{
  static const int warm_starts[] = { 309, 288, 267, 246, 225, 207, 195, 183 };
  static const int cool_starts[] = { 39, 60, 81, 102, 123, 141, 153, 165 };
  static const int warm_colours[][3] = {
    { 255, 28, 18 },
    { 255, 66, 20 },
    { 255, 100, 24 },
    { 255, 139, 31 },
    { 255, 178, 43 },
    { 255, 214, 65 },
  };
  static const int cool_colours[][3] = {
    { 6, 12, 112 },
    { 16, 31, 145 },
    { 30, 57, 176 },
    { 49, 90, 205 },
    { 75, 124, 225 },
    { 111, 168, 238 },
  };
  const int segment_degrees = 12;
  const int ring_segment_count = 6;

  for (int i = 0; i < ring_segment_count; i++) {
    spy_draw_outlined_radial_segment(ctx, geometry, warm_starts[i],
        warm_starts[i] + segment_degrees,
        spy_colour(255, 232, 146),
        spy_colour(warm_colours[i][0], warm_colours[i][1], warm_colours[i][2]));
    spy_draw_outlined_radial_segment(ctx, geometry, cool_starts[i],
        cool_starts[i] + segment_degrees,
        spy_colour(157, 207, 255),
        spy_colour(cool_colours[i][0], cool_colours[i][1], cool_colours[i][2]));
  }
}

static void spy_draw_hardware_slot(GContext *ctx, GRect frame)
{
  int radius = spy_face_scaled_value(frame.size.w < frame.size.h ? frame.size.w : frame.size.h, 2);

  graphics_context_set_fill_color(ctx, spy_colour(236, 238, 229));
  graphics_fill_rect(ctx, frame, radius, GCornersAll);
  graphics_context_set_stroke_color(ctx, spy_colour(120, 124, 120));
  graphics_draw_rect(ctx, frame);
}

static void spy_draw_hardware_rivet(GContext *ctx, GPoint centre, int radius)
{
  graphics_context_set_fill_color(ctx, spy_colour(236, 238, 229));
  graphics_fill_circle(ctx, centre, radius);
  graphics_context_set_stroke_color(ctx, spy_colour(93, 96, 92));
  graphics_draw_circle(ctx, centre, radius);
  graphics_context_set_fill_color(ctx, spy_colour(255, 255, 255));
  graphics_fill_circle(ctx, GPoint(centre.x - (radius / 3), centre.y - (radius / 3)),
      radius / 3);
}

static void spy_draw_hardware_details(GContext *ctx, SpyFaceGeometry geometry)
{
  int slot_width = spy_face_scaled_value(geometry.min_side, 9);
  int slot_height = spy_face_scaled_value(geometry.min_side, 31);
  int slot_gap = spy_face_scaled_value(geometry.min_side, 5);
  int top_y = spy_face_scaled_value(geometry.min_side, 31);
  int bottom_y = geometry.screen_height - top_y - slot_height;
  int rivet_radius = spy_face_scaled_value(geometry.min_side, 7);
  int rivet_y_top = spy_face_scaled_value(geometry.min_side, 51);
  int rivet_y_bottom = geometry.screen_height - rivet_y_top;
  int rivet_x_offset = spy_face_scaled_value(geometry.min_side, 54);
  int side_tab_width = spy_face_scaled_value(geometry.min_side, 37);
  int side_tab_height = spy_face_scaled_value(geometry.min_side, 10);
  int side_tab_y = geometry.centre_y - (side_tab_height / 2);

  spy_draw_hardware_slot(ctx, GRect(geometry.centre_x - slot_gap - slot_width,
      top_y, slot_width, slot_height));
  spy_draw_hardware_slot(ctx, GRect(geometry.centre_x + slot_gap,
      top_y, slot_width, slot_height));
  spy_draw_hardware_slot(ctx, GRect(geometry.centre_x - (slot_width / 2),
      bottom_y, slot_width, slot_height));

  spy_draw_hardware_rivet(ctx, GPoint(geometry.centre_x - rivet_x_offset, rivet_y_top),
      rivet_radius);
  spy_draw_hardware_rivet(ctx, GPoint(geometry.centre_x + rivet_x_offset, rivet_y_top),
      rivet_radius);
  spy_draw_hardware_rivet(ctx, GPoint(geometry.centre_x - rivet_x_offset, rivet_y_bottom),
      rivet_radius);
  spy_draw_hardware_rivet(ctx, GPoint(geometry.centre_x + rivet_x_offset, rivet_y_bottom),
      rivet_radius);

  graphics_context_set_fill_color(ctx, spy_colour(236, 238, 229));
  graphics_fill_rect(ctx, GRect(0, side_tab_y, side_tab_width, side_tab_height),
      side_tab_height / 2, GCornersAll);
  graphics_fill_rect(ctx, GRect(geometry.screen_width - side_tab_width, side_tab_y,
      side_tab_width, side_tab_height), side_tab_height / 2, GCornersAll);
  graphics_context_set_fill_color(ctx, spy_colour(0, 64, 20));
  graphics_fill_rect(ctx, GRect(spy_face_scaled_value(geometry.min_side, 7),
      side_tab_y + 1, side_tab_width - spy_face_scaled_value(geometry.min_side, 14),
      side_tab_height - 2), side_tab_height / 3, GCornersAll);
  graphics_fill_rect(ctx, GRect(geometry.screen_width - side_tab_width
      + spy_face_scaled_value(geometry.min_side, 7), side_tab_y + 1,
      side_tab_width - spy_face_scaled_value(geometry.min_side, 14),
      side_tab_height - 2), side_tab_height / 3, GCornersAll);
}

static void spy_draw_reticle(GContext *ctx, SpyFaceGeometry geometry)
{
  GPoint centre = GPoint(geometry.centre_x, geometry.centre_y);
  int radius = spy_face_scaled_value(geometry.min_side, 34);
  GColor reticle_colour = spy_colour(0, 76, 20);

  graphics_context_set_stroke_color(ctx, reticle_colour);
  graphics_draw_circle(ctx, centre, radius);
  graphics_draw_circle(ctx, centre, radius / 2);
  graphics_draw_line(ctx, GPoint(centre.x - radius, centre.y),
      GPoint(centre.x + radius, centre.y));
  graphics_draw_line(ctx, GPoint(centre.x, centre.y - radius),
      GPoint(centre.x, centre.y + radius));
  graphics_draw_line(ctx, GPoint(centre.x - radius, centre.y - radius),
      GPoint(centre.x + radius, centre.y + radius));
  graphics_draw_line(ctx, GPoint(centre.x - radius, centre.y + radius),
      GPoint(centre.x + radius, centre.y - radius));

}

static void spy_format_time(char *buffer, size_t buffer_size, const SpyFaceState *state)
{
  int hour = state->hour;
  if (!state->twenty_four_hour_style) {
    hour %= 12;
    if (hour == 0) {
      hour = 12;
    }
  }

  snprintf(buffer, buffer_size, "%02d:%02d", hour, state->minute);
  buffer[buffer_size - 1] = '\0';
}

static void spy_format_ampm(char *buffer, size_t buffer_size, const SpyFaceState *state)
{
  if (state->twenty_four_hour_style) {
    buffer[0] = '\0';
    return;
  }

  snprintf(buffer, buffer_size, "%s", state->hour >= 12 ? "PM" : "AM");
  buffer[buffer_size - 1] = '\0';
}

static void spy_format_date(char *buffer, size_t buffer_size, const SpyFaceState *state)
{
  static const char *weekdays[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
  static const char *months[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
  };
  int month = state->month + 1;
  int day = state->day;
  int weekday = state->weekday;
  int year = state->year;
  if (month < 1) {
    month = 1;
  } else if (month > 12) {
    month = 12;
  }
  if (day < 1) {
    day = 1;
  } else if (day > 31) {
    day = 31;
  }
  if (weekday < 0 || weekday > 6) {
    weekday = 0;
  }
  if (year < 0) {
    year = 0;
  } else if (year > 9999) {
    year = 9999;
  }

  switch (state->date_format) {
    case SPY_FACE_DATE_FORMAT_MM_DD_YYYY:
      snprintf(buffer, buffer_size, "%02d-%02d-%04d", month, day, year);
      break;
    case SPY_FACE_DATE_FORMAT_MON_D_AUG:
      snprintf(buffer, buffer_size, "%s %d %s", weekdays[weekday], day,
          months[month - 1]);
      break;
    case SPY_FACE_DATE_FORMAT_DD_SLASH_MM:
      snprintf(buffer, buffer_size, "%02d/%02d", day, month);
      break;
    case SPY_FACE_DATE_FORMAT_MM_SLASH_DD:
      snprintf(buffer, buffer_size, "%02d/%02d", month, day);
      break;
    default:
      snprintf(buffer, buffer_size, "%02d-%02d-%02d", day, month, year % 100);
      break;
  }
  buffer[buffer_size - 1] = '\0';
}

static void spy_draw_display_background(GContext *ctx, SpyFaceGeometry geometry,
    GColor *bright_green, GColor *middle_green)
{
  GRect display = GRect(geometry.display.x, geometry.display.y,
      geometry.display.w, geometry.display.h);
  *bright_green = spy_colour(28, 198, 36);
  *middle_green = spy_colour(0, 105, 28);
  GColor outer_green = spy_colour(0, 34, 8);
  GColor screen_green = spy_colour(0, 68, 18);
  GColor band_green = spy_colour(0, 83, 22);
  GColor border_green = spy_colour(0, 120, 32);
  int corner_radius = spy_face_scaled_value(geometry.min_side, 6);
  int inset = spy_face_scaled_value(geometry.min_side, 3);
  GRect inner_display = grect_inset(display, GEdgeInsets(inset));
  GRect screen_band = GRect(inner_display.origin.x,
      inner_display.origin.y + (inner_display.size.h / 4),
      inner_display.size.w, inner_display.size.h / 2);

  graphics_context_set_fill_color(ctx, outer_green);
  graphics_fill_rect(ctx, display, corner_radius, GCornersAll);
  graphics_context_set_fill_color(ctx, screen_green);
  graphics_fill_rect(ctx, inner_display, corner_radius, GCornersAll);
  graphics_context_set_fill_color(ctx, band_green);
  graphics_fill_rect(ctx, screen_band, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, border_green);
  graphics_draw_rect(ctx, display);
}

static void spy_draw_digital_status_rectangles(GContext *ctx, SpyFaceGeometry geometry,
    GRect display)
{
  const int segment_count = 4;
  int segment_width = spy_face_scaled_value(geometry.min_side, 30);
  int segment_height = spy_face_scaled_value(geometry.min_side, 7);
  int segment_gap = spy_face_scaled_value(geometry.min_side, 8);
  int total_width = (segment_count * segment_width) + ((segment_count - 1) * segment_gap);
  int x = display.origin.x + ((display.size.w - total_width) / 2);
  int y = display.origin.y + display.size.h - spy_face_scaled_value(geometry.min_side, 14);

  for (int i = 0; i < segment_count; i++) {
    GColor fill_colour = i == 0 ? spy_colour(28, 198, 36) : spy_colour(28, 142, 36);
    graphics_context_set_fill_color(ctx, fill_colour);
    graphics_fill_rect(ctx, GRect(x + (i * (segment_width + segment_gap)), y,
        segment_width, segment_height), 1, GCornersAll);
    graphics_context_set_stroke_color(ctx, spy_colour(0, 44, 12));
    graphics_draw_rect(ctx, GRect(x + (i * (segment_width + segment_gap)), y,
        segment_width, segment_height));
  }
}

static void spy_draw_digital_display(GContext *ctx, SpyFaceGeometry geometry,
    const SpyFaceLayerData *data)
{
  const SpyFaceState *state = &data->state;
  GRect display = GRect(geometry.display.x, geometry.display.y,
      geometry.display.w, geometry.display.h);
  GColor bright_green;
  GColor middle_green;

  spy_draw_display_background(ctx, geometry, &bright_green, &middle_green);
  int pad = spy_face_scaled_value(geometry.min_side, 10);
  GFont label_font = spy_label_font_for_geometry(geometry);
  GFont small_font = spy_small_font_for_geometry(geometry);
  GFont time_font = spy_time_font_for_geometry(geometry, data->time_font);
  char time_text[8];
  char ampm_text[4];
  char date_text[16];
  char status_text[16];

  spy_format_time(time_text, sizeof(time_text), state);
  spy_format_ampm(ampm_text, sizeof(ampm_text), state);
  spy_format_date(date_text, sizeof(date_text), state);
  snprintf(status_text, sizeof(status_text), "%s %d%%",
      state->bluetooth_connected ? "LINKED" : "NO LINK", state->battery_percent);
  status_text[sizeof(status_text) - 1] = '\0';

  spy_draw_text(ctx, "WATCH v2.01 BETA", label_font,
      GRect(display.origin.x + pad, display.origin.y + 2,
          display.size.w - (2 * pad), spy_face_scaled_value(geometry.min_side, 20)),
      GTextAlignmentCenter, bright_green);
  spy_draw_text(ctx, status_text, small_font,
      GRect(display.origin.x + pad, display.origin.y + spy_face_scaled_value(geometry.min_side, 18),
          (display.size.w / 2) - pad, spy_face_scaled_value(geometry.min_side, 18)),
      GTextAlignmentLeft, middle_green);
  spy_draw_text(ctx, date_text, small_font,
      GRect(display.origin.x + (display.size.w / 2),
          display.origin.y + spy_face_scaled_value(geometry.min_side, 18),
          (display.size.w / 2) - pad, spy_face_scaled_value(geometry.min_side, 18)),
      GTextAlignmentRight, bright_green);
  if (state->quiet_time_active) {
    spy_draw_moon_icon(ctx,
        GPoint(display.origin.x + spy_face_scaled_value(geometry.min_side, 15),
            display.origin.y + spy_face_scaled_value(geometry.min_side, 28)),
        spy_face_scaled_value(geometry.min_side, 6), bright_green,
        spy_colour(0, 83, 22));
  }

  spy_draw_reticle(ctx, geometry);

  spy_draw_text(ctx, time_text, time_font,
      GRect(display.origin.x + spy_face_scaled_value(geometry.min_side, 4),
          display.origin.y + spy_face_scaled_value(geometry.min_side, 40),
          display.size.w - spy_face_scaled_value(geometry.min_side, 8),
          spy_face_scaled_value(geometry.min_side, 54)),
      GTextAlignmentCenter, bright_green);
  spy_draw_text(ctx, ampm_text, small_font,
      GRect(display.origin.x + display.size.w - spy_face_scaled_value(geometry.min_side, 38),
          display.origin.y + spy_face_scaled_value(geometry.min_side, 78),
          spy_face_scaled_value(geometry.min_side, 30), spy_face_scaled_value(geometry.min_side, 18)),
      GTextAlignmentCenter, middle_green);
  spy_draw_digital_status_rectangles(ctx, geometry, display);
}

static void spy_draw_analogue_digital_readout(GContext *ctx, SpyFaceGeometry geometry,
    const SpyFaceState *state, int radius, GColor bright_green)
{
  char time_text[8];
  int readout_width = spy_face_scaled_value(geometry.min_side, 96);
  int readout_height = spy_face_scaled_value(geometry.min_side, 22);
  if (geometry.min_side >= 200 && readout_width < 88) {
    readout_width = 88;
  }
  if (geometry.min_side >= 200 && readout_height < 22) {
    readout_height = 22;
  }
  int readout_y = geometry.centre_y + radius - spy_face_scaled_value(geometry.min_side, 58);
  GRect readout = GRect(geometry.centre_x - (readout_width / 2), readout_y,
      readout_width, readout_height);

  spy_format_time(time_text, sizeof(time_text), state);

  graphics_context_set_fill_color(ctx, spy_colour(0, 83, 22));
  graphics_fill_rect(ctx, readout, spy_face_scaled_value(geometry.min_side, 3),
      GCornersAll);
  graphics_context_set_stroke_color(ctx, spy_colour(0, 120, 32));
  graphics_draw_rect(ctx, readout);
  spy_draw_text(ctx, time_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(readout.origin.x + 2, readout.origin.y + 1,
          readout.size.w - 4, readout.size.h - 2),
      GTextAlignmentCenter, bright_green);
}

static void spy_draw_analogue_display(GContext *ctx, SpyFaceGeometry geometry,
    const SpyFaceLayerData *data)
{
  const SpyFaceState *state = &data->state;
  GColor bright_green = spy_colour(28, 198, 36);
  GColor middle_green = spy_colour(0, 105, 28);
  GColor dark_green = spy_colour(0, 68, 18);
  GColor band_green = spy_colour(0, 83, 22);

  GPoint centre = GPoint(geometry.centre_x, geometry.centre_y);
  int radius = geometry.inner_radius - spy_face_scaled_value(geometry.min_side, 4);
  graphics_context_set_fill_color(ctx, dark_green);
  graphics_fill_circle(ctx, centre, radius);
  graphics_context_set_fill_color(ctx, band_green);
  graphics_fill_circle(ctx, centre, radius - spy_face_scaled_value(geometry.min_side, 28));
  graphics_context_set_stroke_color(ctx, middle_green);
  graphics_draw_circle(ctx, centre, radius);
  graphics_draw_circle(ctx, centre, radius - spy_face_scaled_value(geometry.min_side, 14));
  graphics_draw_circle(ctx, centre, spy_face_scaled_value(geometry.min_side, 34));
  if (state->quiet_time_active) {
    spy_draw_moon_icon(ctx,
        GPoint(geometry.centre_x + spy_face_scaled_value(geometry.min_side, 40),
            geometry.centre_y - spy_face_scaled_value(geometry.min_side, 47)),
        spy_face_scaled_value(geometry.min_side, 7), bright_green, band_green);
  }

  for (int tick = 0; tick < 60; tick++) {
    int angle = tick * 6;
    int outer_radius = radius - spy_face_scaled_value(geometry.min_side, 2);
    int inner_radius = tick % 5 == 0
        ? radius - spy_face_scaled_value(geometry.min_side, 10)
        : radius - spy_face_scaled_value(geometry.min_side, 6);
    graphics_context_set_stroke_color(ctx, tick % 5 == 0 ? bright_green : middle_green);
    graphics_draw_line(ctx, spy_polar_point(centre, inner_radius, angle),
        spy_polar_point(centre, outer_radius, angle));
  }

  int hour_angle = ((state->hour % 12) * 30) + ((state->minute * 30) / 60);
  int minute_angle = state->minute * 6;

  GColor hand_outline = spy_colour(93, 96, 92);
  GColor hand_fill = spy_colour(236, 238, 229);
  spy_draw_shaped_hand(ctx, centre, hour_angle,
      radius - spy_face_scaled_value(geometry.min_side, 34),
      spy_face_scaled_value(geometry.min_side, 2),
      spy_face_scaled_value(geometry.min_side, 5),
      spy_face_scaled_value(geometry.min_side, 15),
      spy_face_scaled_value(geometry.min_side, 20),
      spy_face_scaled_value(geometry.min_side, 2),
      hand_fill, hand_outline);
  spy_draw_shaped_hand(ctx, centre, minute_angle,
      radius - spy_face_scaled_value(geometry.min_side, 10),
      spy_face_scaled_value(geometry.min_side, 2),
      spy_face_scaled_value(geometry.min_side, 5),
      spy_face_scaled_value(geometry.min_side, 18),
      spy_face_scaled_value(geometry.min_side, 24),
      spy_face_scaled_value(geometry.min_side, 2),
      hand_fill, hand_outline);
  spy_draw_analogue_centre_hub(ctx, geometry, centre);

  if (state->backlight_on && state->analogue_seconds_enabled) {
    int second_angle = state->second * 6;
    graphics_context_set_stroke_color(ctx, spy_colour(205, 42, 13));
    graphics_draw_line(ctx, centre,
        spy_polar_point(centre, radius - spy_face_scaled_value(geometry.min_side, 15), second_angle));
    spy_draw_analogue_centre_hub(ctx, geometry, centre);
  }

  spy_draw_analogue_digital_readout(ctx, geometry, state, radius, bright_green);
}

static void spy_face_layer_update_proc(Layer *layer, GContext *ctx)
{
  SpyFaceLayerData *data = layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  SpyFaceGeometry geometry = spy_face_geometry_for_bounds(bounds.size.w,
      bounds.size.h, PBL_IF_ROUND_ELSE(true, false));

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (data->mode == SPY_FACE_MODE_ANALOGUE) {
    spy_draw_analogue_display(ctx, geometry, data);
  } else {
    spy_draw_digital_display(ctx, geometry, data);
  }
  spy_draw_ring(ctx, geometry);
  spy_draw_hardware_details(ctx, geometry);
}

Layer *spy_face_layer_create(GRect frame)
{
  Layer *layer = layer_create_with_data(frame, sizeof(SpyFaceLayerData));
  if (layer == NULL) {
    return NULL;
  }

  SpyFaceLayerData *data = layer_get_data(layer);
  memset(data, 0, sizeof(*data));
  data->state.battery_percent = 100;
  data->state.bluetooth_connected = true;
  data->mode = SPY_FACE_MODE_DIGITAL;
  data->time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_BOLD_42));
  layer_set_update_proc(layer, spy_face_layer_update_proc);
  return layer;
}

void spy_face_layer_destroy(Layer *layer)
{
  if (layer == NULL) {
    return;
  }

  SpyFaceLayerData *data = layer_get_data(layer);
  if (data->time_font != NULL) {
    fonts_unload_custom_font(data->time_font);
    data->time_font = NULL;
  }
  layer_destroy(layer);
}

void spy_face_layer_set_state(Layer *layer, const SpyFaceState *state)
{
  if (layer == NULL || state == NULL) {
    return;
  }

  SpyFaceLayerData *data = layer_get_data(layer);
  data->state = *state;
  data->mode = spy_mode_for_setting(state->display_mode);
  layer_mark_dirty(layer);
}

bool spy_face_layer_wants_second_ticks(Layer *layer)
{
  if (layer == NULL) {
    return false;
  }

  SpyFaceLayerData *data = layer_get_data(layer);
  return data->mode == SPY_FACE_MODE_ANALOGUE
      && data->state.analogue_seconds_enabled
      && data->state.backlight_on;
}
